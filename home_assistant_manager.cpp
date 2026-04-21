#include "home_assistant_manager.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "debug_logging.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{

#if __has_include("home_assistant_credentials.h")
#include "home_assistant_credentials.h"
constexpr bool kHomeAssistantConfigured = true;
#else
constexpr bool kHomeAssistantConfigured = false;
inline constexpr char HOME_ASSISTANT_HOST[] = "";
inline constexpr uint16_t HOME_ASSISTANT_PORT = 8123;
inline constexpr char HOME_ASSISTANT_TOKEN[] = "";
inline constexpr char HOME_ASSISTANT_ENTITY_ID[] = "";
inline constexpr char HOME_ASSISTANT_SELF_ENTITY_ID[] = "";
#endif

#if __has_include("weather_display_config.h")
#include "weather_display_config.h"
#else
inline constexpr char HOME_ASSISTANT_WEATHER_ENTITY_ID[] = "";
inline constexpr char HOME_ASSISTANT_SUN_ENTITY_ID[] = "";
#endif

/// @brief Normalizes user-editable credential symbols to the repo's internal naming style.
/// @details The local configuration headers intentionally keep explicit
/// `HOME_ASSISTANT_*` names because users edit those files directly. The implementation uses
/// `kCamelCase` aliases so runtime constants read the same way as the rest of the codebase.
inline constexpr const char* kHomeAssistantHost = HOME_ASSISTANT_HOST;
inline constexpr uint16_t kHomeAssistantPort = HOME_ASSISTANT_PORT;
inline constexpr const char* kHomeAssistantToken = HOME_ASSISTANT_TOKEN;
inline constexpr const char* kTrackedEntityId = HOME_ASSISTANT_ENTITY_ID;
inline constexpr const char* kSelfEntityId = HOME_ASSISTANT_SELF_ENTITY_ID;
inline constexpr const char* kWeatherEntityId = HOME_ASSISTANT_WEATHER_ENTITY_ID;
inline constexpr const char* kSunEntityId = HOME_ASSISTANT_SUN_ENTITY_ID;

constexpr uint32_t kResolveTimeoutMs = 4000;
constexpr uint32_t kConnectTimeoutMs = 4000;
constexpr uint32_t kIoTimeoutMs = 4000;
constexpr uint32_t kRetryDelayMs = 10000;
constexpr uint32_t kRefreshIntervalMs = 5 * 60 * 1000;
constexpr uint8_t kTcpPollInterval = 2;
constexpr unsigned long kMaxTcpPortValue = std::numeric_limits<uint16_t>::max();
constexpr char kHttpSchemePrefix[] = "http://";
constexpr size_t kHttpSchemePrefixLength = sizeof(kHttpSchemePrefix) - 1;
constexpr char kHttpsSchemePrefix[] = "https://";
constexpr size_t kHttpsSchemePrefixLength = sizeof(kHttpsSchemePrefix) - 1;
constexpr bool kHomeAssistantRuntimeEnabled = true;

enum class RequestKind : uint8_t
{
    ProbeApi = 0,
    FetchTrackedEntity,
    FetchWeatherEntity,
    FetchWeatherForecast,
    FetchSunEntity,
    PublishSelfEntity,
};

HomeAssistantStatus g_status = {};
ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
tcp_pcb* g_pcb = nullptr;
size_t g_request_sent = 0;
size_t g_response_len = 0;
char g_request[1024] = {};
char g_request_body[256] = {};
char g_response[16384] = {};
char g_configured_host[48] = {};
uint16_t g_configured_port = kHomeAssistantPort;
bool g_config_valid = false;
absolute_time_t g_deadline = nil_time;
absolute_time_t g_next_attempt = nil_time;
bool g_probe_attempted = false;
bool g_sequence_complete = false;
RequestKind g_request_kind = RequestKind::ProbeApi;
char g_weather_temperature_unit = '\0';
char g_weather_wind_source_unit[8] = {};

/// @brief Returns whether the current request kind should drive connection-state updates.
bool request_updates_connection_state()
{
    return g_request_kind == RequestKind::ProbeApi;
}

template <size_t N>
/// @brief Copies text into a fixed-size Home Assistant status buffer.
void copy_text(std::array<char, N>& dst, const char* src)
{
    dst.fill('\0');
    if (!src)
    {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

/// @brief Parses a decimal TCP port string.
bool parse_port(const char* text, uint16_t* out_port)
{
    if (text == nullptr || *text == '\0' || out_port == nullptr)
    {
        return false;
    }

    unsigned long value = 0;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            return false;
        }
    value = (value * 10U) + static_cast<unsigned long>(*p - '0');
        if (value > kMaxTcpPortValue)
        {
            return false;
        }
    }

    if (value == 0U)
    {
        return false;
    }

    *out_port = static_cast<uint16_t>(value);
    return true;
}

/// @brief Returns the log-friendly name for one request kind.
const char* request_kind_name(RequestKind kind)
{
    switch (kind)
    {
    case RequestKind::ProbeApi:
        return "probe";
    case RequestKind::FetchTrackedEntity:
        return "entity";
    case RequestKind::FetchWeatherEntity:
        return "weather";
    case RequestKind::FetchWeatherForecast:
        return "forecast";
    case RequestKind::FetchSunEntity:
        return "sun";
    case RequestKind::PublishSelfEntity:
        return "publish";
    }

    return "unknown";
}

/// @brief Returns a pointer to the start of the current HTTP response body.
const char* response_body()
{
    const char* body = std::strstr(g_response, "\r\n\r\n");
    return body ? (body + 4) : nullptr;
}

/// @brief Returns a pointer to the end of the current HTTP header block.
const char* response_headers_end()
{
    return std::strstr(g_response, "\r\n\r\n");
}

/// @brief Extracts the HTTP status code when only part of the response is available.
int partial_http_status()
{
    const char* line_end = std::strstr(g_response, "\r\n");
    if (line_end == nullptr)
    {
        return 0;
    }

    int http_status = 0;
    if (std::sscanf(g_response, "HTTP/%*d.%*d %d", &http_status) != 1)
    {
        return 0;
    }

    return http_status;
}

/// @brief Performs an ASCII-only case-insensitive character comparison.
bool ascii_iequals(char left, char right)
{
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

/// @brief Returns whether a string starts with the given ASCII prefix ignoring case.
bool ascii_starts_with_case_insensitive(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr)
    {
        return false;
    }

    while (*prefix != '\0')
    {
        if (*text == '\0' || !ascii_iequals(*text, *prefix))
        {
            return false;
        }
        ++text;
        ++prefix;
    }

    return true;
}

/// @brief Returns the start of the next HTTP header line within the buffer.
const char* next_header_line(const char* line, const char* headers_end)
{
    if (line == nullptr || headers_end == nullptr || line >= headers_end)
    {
        return nullptr;
    }

    const char* line_end = std::strstr(line, "\r\n");
    if (line_end == nullptr || line_end >= headers_end)
    {
        return nullptr;
    }

    const char* next = line_end + 2;
    return (next < headers_end) ? next : nullptr;
}

/// @brief Finds the value for a named HTTP response header.
const char* find_response_header_value(const char* header_name)
{
    if (header_name == nullptr || header_name[0] == '\0')
    {
        return nullptr;
    }

    const char* headers_end = response_headers_end();
    const char* line = std::strstr(g_response, "\r\n");
    if (headers_end == nullptr || line == nullptr)
    {
        return nullptr;
    }

    for (line += 2; line != nullptr && line < headers_end;
         line = next_header_line(line, headers_end))
    {
        while (*line == ' ' || *line == '\t')
        {
            ++line;
        }

        if (ascii_starts_with_case_insensitive(line, header_name))
        {
            const char* value = line + std::strlen(header_name);
            while (*value == ' ' || *value == '\t')
            {
                ++value;
            }
            return value;
        }
    }

    return nullptr;
}

/// @brief Parses the `Content-Length` header from the current response.
bool parse_response_content_length(size_t* out_length)
{
    if (out_length == nullptr)
    {
        return false;
    }

    const char* value = find_response_header_value("Content-Length:");
    if (value == nullptr)
    {
        return false;
    }

    char* parse_end = nullptr;
    const unsigned long parsed = std::strtoul(value, &parse_end, 10);
    if (parse_end == value)
    {
        return false;
    }

    while (parse_end != nullptr && (*parse_end == ' ' || *parse_end == '\t'))
    {
        ++parse_end;
    }

    if (parse_end == nullptr || (*parse_end != '\0' && std::strncmp(parse_end, "\r\n", 2) != 0))
    {
        return false;
    }

    *out_length = static_cast<size_t>(parsed);
    return true;
}

/// @brief Returns whether a named header contains the requested token text.
bool response_header_has_token(const char* header_name, const char* token)
{
    if (header_name == nullptr || token == nullptr || token[0] == '\0')
    {
        return false;
    }

    const char* value = find_response_header_value(header_name);
    if (value == nullptr)
    {
        return false;
    }

    const size_t token_len = std::strlen(token);
    const char* headers_end = response_headers_end();
    const char* line_end = std::strstr(value, "\r\n");
    if (headers_end != nullptr && (line_end == nullptr || line_end > headers_end))
    {
        line_end = headers_end;
    }
    if (line_end == nullptr)
    {
        return false;
    }

    for (const char* cursor = value; cursor + token_len <= line_end; ++cursor)
    {
        size_t i = 0;
        while (i < token_len && ascii_iequals(cursor[i], token[i]))
        {
            ++i;
        }
        if (i == token_len)
        {
            return true;
        }
    }

    return false;
}

/// @brief Validates that the current HTTP response is complete and supported.
/// @details This firmware expects a complete header block, a non-chunked body,
/// and a body length that matches `Content-Length` when present. Failing here
/// keeps truncated or unsupported HTTP responses out of the JSON parsing path.
bool validate_http_response(int* http_status, err_t* protocol_error)
{
    if (http_status != nullptr)
    {
        *http_status = 0;
    }
    if (protocol_error != nullptr)
    {
        *protocol_error = ERR_VAL;
    }

    const char* headers_end = response_headers_end();
    if (headers_end == nullptr)
    {
        return false;
    }

    int parsed_status = 0;
    if (std::sscanf(g_response, "HTTP/%*d.%*d %d", &parsed_status) != 1)
    {
        return false;
    }

    if (response_header_has_token("Transfer-Encoding:", "chunked"))
    {
        return false;
    }

    const char* body = headers_end + 4;
    const size_t body_length = g_response_len - static_cast<size_t>(body - g_response);
    size_t content_length = 0;
    if (parse_response_content_length(&content_length) && body_length != content_length)
    {
        if (protocol_error != nullptr)
        {
            *protocol_error = ERR_CLSD;
        }
        return false;
    }

    if (http_status != nullptr)
    {
        *http_status = parsed_status;
    }
    if (protocol_error != nullptr)
    {
        *protocol_error = ERR_OK;
    }
    return true;
}

/// @brief Extracts one JSON string field by key from a simple response body.
bool extract_json_string_value(const char* json, const char* key, char* out, size_t out_size)
{
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    const char* key_pos = std::strstr(json, key);
    if (key_pos == nullptr)
    {
        return false;
    }

    const char* value_start = key_pos + std::strlen(key);
    size_t i = 0;
    bool closed = false;
    for (size_t cursor = 0; value_start[cursor] != '\0'; ++cursor)
    {
        char current = value_start[cursor];
        if (current == '"')
        {
            closed = true;
            break;
        }

        if (current == '\\')
        {
            ++cursor;
            current = value_start[cursor];
            if (current == '\0')
            {
                return false;
            }
        }

        if (i + 1 >= out_size)
        {
            return false;
        }

        out[i++] = current;
    }
    out[i] = '\0';

    // Why this is strict:
    // If the closing quote is missing, the string in the receive buffer is
    // incomplete. Older behavior would accept whatever characters had arrived
    // so far, which can turn a truncated network read into a fake but
    // believable value on screen. Returning false here forces the caller to
    // treat that field as missing instead.
    return closed && i > 0;
}

/// @brief Extracts one non-string JSON scalar field by key.
bool extract_json_scalar_value(const char* json, const char* key, char* out, size_t out_size)
{
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    const char* key_pos = std::strstr(json, key);
    if (key_pos == nullptr)
    {
        return false;
    }

    const char* value = key_pos + std::strlen(key);
    while (*value == ' ' || *value == '\t')
    {
        ++value;
    }

    if (*value == '\0' || *value == '"' || *value == '[' || *value == '{')
    {
        return false;
    }

    size_t i = 0;
    bool terminated = false;
    while (value[i] != '\0' && value[i] != ',' && value[i] != '}' && value[i] != ']' &&
           value[i] != '\r' && value[i] != '\n' && i + 1 < out_size)
    {
        out[i] = value[i];
        ++i;
    }

    if (value[i] == ',' || value[i] == '}' || value[i] == ']' || value[i] == '\r' ||
        value[i] == '\n')
    {
        terminated = true;
    }

    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t'))
    {
        --i;
    }
    out[i] = '\0';
    return terminated && i > 0;
}

/// @brief Trims leading/trailing whitespace and trailing periods in place.
void trim_text_in_place(char* text)
{
    if (text == nullptr)
    {
        return;
    }

    size_t start = 0;
    const size_t length = std::strlen(text);
    while (text[start] != '\0' && std::isspace(static_cast<unsigned char>(text[start])))
    {
        ++start;
    }

    size_t end = length;
    while (end > start &&
           (std::isspace(static_cast<unsigned char>(text[end - 1])) || text[end - 1] == '.'))
    {
        --end;
    }

    if (start >= end)
    {
        text[0] = '\0';
        return;
    }

    if (start > 0)
    {
        std::memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

/// @brief Returns whether a string starts with a prefix ignoring ASCII case.
bool starts_with_ignore_case(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr)
    {
        return false;
    }

    while (*prefix != '\0')
    {
        if (*text == '\0')
        {
            return false;
        }

        if (std::tolower(static_cast<unsigned char>(*text)) !=
            std::tolower(static_cast<unsigned char>(*prefix)))
        {
            return false;
        }

        ++text;
        ++prefix;
    }

    return true;
}

/// @brief Normalizes weather attribution text into a compact provider name.
bool normalize_weather_provider_name(const char* raw_text, char* out, size_t out_size)
{
    if (raw_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    const char* provider = raw_text;
    constexpr const char* kPrefixes[] = {
        "Data provided by ", "Weather forecast from ", "Forecast provided by ", "Provided by ",
        "Powered by ",
    };

    for (const char* prefix : kPrefixes)
    {
        if (starts_with_ignore_case(provider, prefix))
        {
            provider += std::strlen(prefix);
            break;
        }
    }

    std::snprintf(out, out_size, "%s", provider);
    trim_text_in_place(out);
    return out[0] != '\0';
}

/// @brief Normalizes a weather-provider wind-speed unit label.
bool normalize_wind_speed_unit(const char* unit_text, char* out, size_t out_size)
{
    if (unit_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    if (std::strstr(unit_text, "km") != nullptr || std::strstr(unit_text, "KM") != nullptr)
    {
        std::snprintf(out, out_size, "km/h");
        return true;
    }
    if (std::strstr(unit_text, "m/s") != nullptr || std::strstr(unit_text, "M/S") != nullptr)
    {
        std::snprintf(out, out_size, "m/s");
        return true;
    }
    if (std::strstr(unit_text, "mi") != nullptr || std::strstr(unit_text, "MI") != nullptr)
    {
        std::snprintf(out, out_size, "mph");
        return true;
    }
    if (std::strstr(unit_text, "ft/s") != nullptr || std::strstr(unit_text, "FT/S") != nullptr)
    {
        std::snprintf(out, out_size, "ft/s");
        return true;
    }
    if (std::strstr(unit_text, "Beaufort") != nullptr ||
        std::strstr(unit_text, "beaufort") != nullptr)
    {
        std::snprintf(out, out_size, "Bft");
        return true;
    }
    if (std::strstr(unit_text, "kn") != nullptr || std::strstr(unit_text, "KN") != nullptr)
    {
        std::snprintf(out, out_size, "kn");
        return true;
    }

    std::snprintf(out, out_size, "%s", unit_text);
    return out[0] != '\0';
}

/// @brief Formats a scalar text value into a compact display-oriented string.
bool format_compact_scalar_value(const char* scalar_text, char* out, size_t out_size)
{
    if (scalar_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char* parse_end = nullptr;
    const float value = std::strtof(scalar_text, &parse_end);
    if (parse_end != scalar_text && parse_end != nullptr && *parse_end == '\0')
    {
        const int rounded = static_cast<int>(value >= 0.0f ? (value + 0.5f) : (value - 0.5f));
        std::snprintf(out, out_size, "%d", rounded);
        return true;
    }

    std::snprintf(out, out_size, "%s", scalar_text);
    return out[0] != '\0';
}

/// @brief Converts a provider wind-speed reading into mph text.
bool convert_wind_speed_to_mph(const char* speed_text, const char* source_unit, char* out,
                               size_t out_size)
{
    if (speed_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char* parse_end = nullptr;
    const float raw_value = std::strtof(speed_text, &parse_end);
    if (parse_end == speed_text || parse_end == nullptr || *parse_end != '\0')
    {
        return false;
    }

    float mph_value = raw_value;
    if (source_unit != nullptr && source_unit[0] != '\0')
    {
        if (std::strcmp(source_unit, "km/h") == 0)
        {
            mph_value = raw_value * 0.621371f;
        }
        else if (std::strcmp(source_unit, "m/s") == 0)
        {
            mph_value = raw_value * 2.23694f;
        }
        else if (std::strcmp(source_unit, "ft/s") == 0)
        {
            mph_value = raw_value * 0.681818f;
        }
        else if (std::strcmp(source_unit, "kn") == 0)
        {
            mph_value = raw_value * 1.15078f;
        }
        else if (std::strcmp(source_unit, "Bft") == 0)
        {
            static constexpr float kBeaufortToMph[] = {
                0.0f,  1.0f,  4.0f,  8.0f,  13.0f, 19.0f, 25.0f,
                32.0f, 39.0f, 47.0f, 55.0f, 64.0f, 73.0f, 83.0f,
            };
            int index = static_cast<int>(raw_value + 0.5f);
            if (index < 0)
            {
                index = 0;
            }
            else if (index >= static_cast<int>(sizeof(kBeaufortToMph) / sizeof(kBeaufortToMph[0])))
            {
                index = static_cast<int>((sizeof(kBeaufortToMph) / sizeof(kBeaufortToMph[0])) - 1);
            }
            mph_value = kBeaufortToMph[index];
        }
    }

    const int rounded =
        static_cast<int>(mph_value >= 0.0f ? (mph_value + 0.5f) : (mph_value - 0.5f));
    std::snprintf(out, out_size, "%d", rounded);
    return true;
}

/// @brief Formats a wind-bearing value into compass text.
bool format_wind_direction_text(const char* bearing_text, char* out, size_t out_size)
{
    if (bearing_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char* parse_end = nullptr;
    float bearing = std::strtof(bearing_text, &parse_end);
    if (parse_end != bearing_text && parse_end != nullptr && *parse_end == '\0')
    {
        while (bearing < 0.0f)
        {
            bearing += 360.0f;
        }
        while (bearing >= 360.0f)
        {
            bearing -= 360.0f;
        }

        static constexpr const char* kCompass16[] = {
            "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
            "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
        };
        const int index = static_cast<int>((bearing + 11.25f) / 22.5f) % 16;
        std::snprintf(out, out_size, "%s", kCompass16[index]);
        return true;
    }

    size_t out_index = 0;
    for (size_t i = 0; bearing_text[i] != '\0' && out_index + 1 < out_size; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(bearing_text[i]);
        if (std::isspace(ch))
        {
            continue;
        }
        out[out_index++] = static_cast<char>(std::toupper(ch));
    }
    out[out_index] = '\0';
    return out_index > 0;
}

/// @brief Combines wind speed and direction into one compact forecast string.
bool format_compact_wind_text(const char* speed_text, const char* bearing_text, char* out,
                              size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char compact_speed[8] = {};
    char compact_direction[4] = {};
    const bool have_speed = speed_text != nullptr &&
                            convert_wind_speed_to_mph(speed_text, g_weather_wind_source_unit,
                                                      compact_speed, sizeof(compact_speed)) &&
                            compact_speed[0] != '\0';
    const bool have_direction =
        bearing_text != nullptr &&
        format_wind_direction_text(bearing_text, compact_direction, sizeof(compact_direction)) &&
        compact_direction[0] != '\0';

    if (have_speed && have_direction)
    {
        std::snprintf(out, out_size, "%s %s", compact_speed, compact_direction);
        return true;
    }
    if (have_speed)
    {
        std::snprintf(out, out_size, "%s", compact_speed);
        return true;
    }
    if (have_direction)
    {
        std::snprintf(out, out_size, "%s", compact_direction);
        return true;
    }

    return false;
}

/// @brief Updates the user-facing weather source hint from a weather entity payload.
void update_weather_source_hint_from_json(const char* json)
{
    char attribution[96] = {};
    char friendly_name[64] = {};
    char provider_name[64] = {};

    // Attribution is preferred because it usually points at the underlying
    // forecast provider, while the entity friendly name can be user-edited and
    // less useful as provenance text on the display.
    g_status.weather_source_hint.fill('\0');

    if (extract_json_string_value(json, "\"attribution\":\"", attribution, sizeof(attribution)) &&
        normalize_weather_provider_name(attribution, provider_name, sizeof(provider_name)))
    {
        copy_text(g_status.weather_source_hint, provider_name);
        return;
    }

    if (extract_json_string_value(json, "\"friendly_name\":\"", friendly_name,
                                  sizeof(friendly_name)))
    {
        trim_text_in_place(friendly_name);
        if (friendly_name[0] != '\0')
        {
            copy_text(g_status.weather_source_hint, friendly_name);
        }
    }
}

/// @brief Maps raw Home Assistant weather condition codes to friendly labels.
const char* friendly_weather_condition(const char* raw_condition)
{
    if (raw_condition == nullptr || raw_condition[0] == '\0')
    {
        return "";
    }

    if (std::strcmp(raw_condition, "clear-night") == 0)
    {
        return "CLEAR NIGHT";
    }
    if (std::strcmp(raw_condition, "cloudy") == 0)
    {
        return "CLOUDY";
    }
    if (std::strcmp(raw_condition, "exceptional") == 0)
    {
        return "EXCEPTIONAL";
    }
    if (std::strcmp(raw_condition, "fog") == 0)
    {
        return "FOG";
    }
    if (std::strcmp(raw_condition, "hail") == 0)
    {
        return "HAIL";
    }
    if (std::strcmp(raw_condition, "lightning") == 0)
    {
        return "LIGHTNING";
    }
    if (std::strcmp(raw_condition, "lightning-rainy") == 0)
    {
        return "LTNG RAIN";
    }
    if (std::strcmp(raw_condition, "partlycloudy") == 0)
    {
        return "PARTLY CLOUDY";
    }
    if (std::strcmp(raw_condition, "pouring") == 0)
    {
        return "POURING";
    }
    if (std::strcmp(raw_condition, "rainy") == 0)
    {
        return "RAIN";
    }
    if (std::strcmp(raw_condition, "snowy") == 0)
    {
        return "SNOW";
    }
    if (std::strcmp(raw_condition, "snowy-rainy") == 0)
    {
        return "SLEET";
    }
    if (std::strcmp(raw_condition, "sunny") == 0)
    {
        return "SUNNY";
    }
    if (std::strcmp(raw_condition, "windy") == 0)
    {
        return "WINDY";
    }
    if (std::strcmp(raw_condition, "windy-variant") == 0)
    {
        return "BREEZY";
    }
    if (std::strcmp(raw_condition, "unavailable") == 0)
    {
        return "UNAVAILABLE";
    }

    return raw_condition;
}

/// @brief Extracts a normalized `C` or `F` temperature unit marker.
char normalized_temperature_unit(const char* unit_text)
{
    if (unit_text == nullptr)
    {
        return '\0';
    }

    for (const char* p = unit_text; *p != '\0'; ++p)
    {
        if (*p == 'C' || *p == 'c')
        {
            return 'C';
        }
        if (*p == 'F' || *p == 'f')
        {
            return 'F';
        }
    }

    return '\0';
}

/// @brief Extracts `HH:MM` text from an ISO datetime string.
bool format_hour_text(const char* iso_datetime, char* out, size_t out_size)
{
    if (iso_datetime == nullptr || out == nullptr || out_size < 6)
    {
        return false;
    }

    const char* time_sep = std::strchr(iso_datetime, 'T');
    if (time_sep == nullptr || std::strlen(time_sep + 1) < 5)
    {
        return false;
    }

    std::snprintf(out, out_size, "%.5s", time_sep + 1);
    return true;
}

/// @brief Updates sunrise and sunset display strings from the sun entity payload.
void update_sun_times_from_json(const char* json)
{
    char next_rising[40] = {};
    char next_setting[40] = {};

    g_status.sunrise_text.fill('\0');
    g_status.sunset_text.fill('\0');

    if (extract_json_string_value(json, "\"next_rising\":\"", next_rising, sizeof(next_rising)))
    {
        format_hour_text(next_rising, g_status.sunrise_text.data(), g_status.sunrise_text.size());
    }

    if (extract_json_string_value(json, "\"next_setting\":\"", next_setting, sizeof(next_setting)))
    {
        format_hour_text(next_setting, g_status.sunset_text.data(), g_status.sunset_text.size());
    }
}

/// @brief Clears the cached hourly weather forecast rows.
void clear_weather_forecast()
{
    g_status.weather_forecast_count = 0;
    for (auto& entry : g_status.weather_forecast)
    {
        entry.time_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
    }
}

// Why this helper is needed:
// The forecast parser does not use a full JSON library. It first isolates one
// forecast object, then re-parses just that slice. A plain "find the next }"
// approach is not safe enough because JSON strings can legally contain braces,
// for example in descriptive text. This matcher keeps track of whether we are
// inside a quoted string and whether the current character is escaped, so only
// real structural braces count.
/// @brief Finds the matching closing brace for one JSON object.
const char* find_matching_json_object_end(const char* object_start)
{
    if (object_start == nullptr || *object_start != '{')
    {
        return nullptr;
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (const char* p = object_start; *p != '\0'; ++p)
    {
        const char current = *p;
        if (escape)
        {
            escape = false;
            continue;
        }

        if (current == '\\' && in_string)
        {
            escape = true;
            continue;
        }

        if (current == '"')
        {
            in_string = !in_string;
            continue;
        }

        if (in_string)
        {
            continue;
        }

        if (current == '{')
        {
            ++depth;
        }
        else if (current == '}')
        {
            --depth;
            if (depth == 0)
            {
                return p;
            }
        }
    }

    return nullptr;
}

/// @brief Parses the hourly weather forecast response into display rows.
bool parse_hourly_forecast_response(const char* json)
{
    if (json == nullptr)
    {
        return false;
    }

    const char* forecast_key = std::strstr(json, "\"forecast\":[");
    if (forecast_key == nullptr)
    {
        return false;
    }

    const char* cursor = std::strchr(forecast_key, '[');
    if (cursor == nullptr)
    {
        return false;
    }
    ++cursor;

    // Start from a blank forecast so a partial parse never leaves stale rows
    // from an earlier successful response on screen.
    clear_weather_forecast();

    while (*cursor != '\0' && *cursor != ']' &&
           g_status.weather_forecast_count < kWeatherForecastEntryCount)
    {
        const char* object_start = std::strchr(cursor, '{');
        if (object_start == nullptr)
        {
            break;
        }

        const char* object_end = find_matching_json_object_end(object_start);
        if (object_end == nullptr)
        {
            break;
        }

        char object_json[384] = {};
        const size_t object_len = static_cast<size_t>(object_end - object_start + 1);
        if (object_len >= sizeof(object_json))
        {
            // Reason for skipping:
            // Copying only part of the object would create invalid JSON and the
            // parser below could then read a half-object as if it were real
            // data. Dropping the single oversized entry is safer than trying to
            // salvage a truncated copy.
            cursor = object_end + 1;
            continue;
        }
        std::memcpy(object_json, object_start, object_len);
        object_json[object_len] = '\0';

        // Each forecast object is re-parsed into the compact, display-friendly
        // fields used by the weather page rather than mirroring the raw JSON.
        char datetime_text[32] = {};
        char temperature_text[16] = {};
        char wind_speed_text[16] = {};
        char wind_bearing_text[16] = {};
        char condition_text[24] = {};

        if (!extract_json_string_value(object_json, "\"datetime\":\"", datetime_text,
                                       sizeof(datetime_text)) ||
            !format_hour_text(
                datetime_text,
                g_status.weather_forecast[g_status.weather_forecast_count].time_text.data(),
                g_status.weather_forecast[g_status.weather_forecast_count].time_text.size()))
        {
            cursor = object_end + 1;
            continue;
        }

        if (extract_json_scalar_value(object_json, "\"temperature\":", temperature_text,
                                      sizeof(temperature_text)))
        {
            if (g_weather_temperature_unit != '\0')
            {
                std::snprintf(g_status.weather_forecast[g_status.weather_forecast_count]
                                  .temperature_text.data(),
                              g_status.weather_forecast[g_status.weather_forecast_count]
                                  .temperature_text.size(),
                              "%s %c", temperature_text, g_weather_temperature_unit);
            }
            else
            {
                copy_text(
                    g_status.weather_forecast[g_status.weather_forecast_count].temperature_text,
                    temperature_text);
            }
        }
        else
        {
            copy_text(g_status.weather_forecast[g_status.weather_forecast_count].temperature_text,
                      "-");
        }

        const bool have_wind_speed = extract_json_scalar_value(
            object_json, "\"wind_speed\":", wind_speed_text, sizeof(wind_speed_text));
        const bool have_wind_bearing =
            extract_json_string_value(object_json, "\"wind_bearing\":\"", wind_bearing_text,
                                      sizeof(wind_bearing_text)) ||
            extract_json_scalar_value(object_json, "\"wind_bearing\":", wind_bearing_text,
                                      sizeof(wind_bearing_text));

        if (format_compact_wind_text(
                have_wind_speed ? wind_speed_text : nullptr,
                have_wind_bearing ? wind_bearing_text : nullptr,
                g_status.weather_forecast[g_status.weather_forecast_count].wind_text.data(),
                g_status.weather_forecast[g_status.weather_forecast_count].wind_text.size()))
        {
        }
        else
        {
            copy_text(g_status.weather_forecast[g_status.weather_forecast_count].wind_text, "-");
        }

        if (extract_json_string_value(object_json, "\"condition\":\"", condition_text,
                                      sizeof(condition_text)))
        {
            copy_text(g_status.weather_forecast[g_status.weather_forecast_count].condition_text,
                      friendly_weather_condition(condition_text));
        }
        else
        {
            copy_text(g_status.weather_forecast[g_status.weather_forecast_count].condition_text,
                      "?");
        }

        ++g_status.weather_forecast_count;
        cursor = object_end + 1;
    }

    return g_status.weather_forecast_count > 0;
}

/// @brief Clears all runtime data fetched from Home Assistant.
void clear_runtime_data()
{
    // Runtime data is cleared independently from static configuration so losing
    // connectivity wipes stale weather/entity values without forgetting what to
    // request once the connection returns.
    g_status.self_entity_published = false;
    g_status.tracked_entity_state.fill('\0');
    g_status.weather_source_hint.fill('\0');
    g_status.weather_condition.fill('\0');
    g_status.weather_temperature.fill('\0');
    g_status.weather_wind_unit.fill('\0');
    g_status.sunrise_text.fill('\0');
    g_status.sunset_text.fill('\0');
    clear_weather_forecast();
    g_weather_temperature_unit = '\0';
    g_weather_wind_source_unit[0] = '\0';
}

/// @brief Returns whether the supplied HTTP status counts as success for a request kind.
bool request_kind_success(RequestKind kind, int http_status)
{
    switch (kind)
    {
    case RequestKind::ProbeApi:
    case RequestKind::FetchTrackedEntity:
    case RequestKind::FetchWeatherEntity:
    case RequestKind::FetchWeatherForecast:
    case RequestKind::FetchSunEntity:
        return http_status == 200;
    case RequestKind::PublishSelfEntity:
        return http_status == 200 || http_status == 201;
    }

    return false;
}

/// @brief Advances the serialized Home Assistant polling sequence to its next step.
void advance_request_kind()
{
    // The Home Assistant session is intentionally serialized: prove the API is
    // reachable first, then fetch optional entities in priority order, then
    // wait for the next refresh interval.
    switch (g_request_kind)
    {
    case RequestKind::ProbeApi:
        if (g_status.tracked_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::FetchTrackedEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.weather_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::FetchWeatherEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::FetchTrackedEntity:
        if (g_status.weather_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::FetchWeatherEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::FetchWeatherEntity:
        g_request_kind = RequestKind::FetchWeatherForecast;
        g_sequence_complete = false;
        g_next_attempt = get_absolute_time();
        return;
    case RequestKind::FetchWeatherForecast:
        if (kSunEntityId[0] != '\0')
        {
            g_request_kind = RequestKind::FetchSunEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::FetchSunEntity:
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::PublishSelfEntity:
        break;
    }

    g_request_kind = RequestKind::ProbeApi;
    g_sequence_complete = true;
    g_next_attempt = make_timeout_time_ms(kRefreshIntervalMs);
}

/// @brief Parses and normalizes the configured Home Assistant endpoint.
bool parse_home_assistant_endpoint()
{
    g_configured_host[0] = '\0';
    g_configured_port = kHomeAssistantPort;

    // Endpoint normalization happens once at init because this client only
    // supports plain HTTP host[:port] targets, not arbitrary base paths.
    if (kHomeAssistantHost[0] == '\0')
    {
        return false;
    }

    const char* host_start = kHomeAssistantHost;
    if (std::strncmp(host_start, kHttpSchemePrefix, kHttpSchemePrefixLength) == 0)
    {
        host_start += kHttpSchemePrefixLength;
    }
    else if (std::strncmp(host_start, kHttpsSchemePrefix, kHttpsSchemePrefixLength) == 0)
    {
        std::printf("HA config uses https:// but only plain HTTP is supported\n");
        return false;
    }

    const char* host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/')
    {
        ++host_end;
    }

    const size_t host_len = static_cast<size_t>(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(g_configured_host))
    {
        std::printf("HA config host is empty or too long\n");
        return false;
    }

    std::memcpy(g_configured_host, host_start, host_len);
    g_configured_host[host_len] = '\0';

    if (*host_end == ':')
    {
        const char* port_start = host_end + 1;
        const char* port_end = port_start;
        while (*port_end != '\0' && *port_end != '/')
        {
            ++port_end;
        }

        char port_text[8] = {};
        const size_t port_len = static_cast<size_t>(port_end - port_start);
        if (port_len == 0 || port_len >= sizeof(port_text))
        {
            std::printf("HA config port is invalid\n");
            return false;
        }

        std::memcpy(port_text, port_start, port_len);
        port_text[port_len] = '\0';
        if (!parse_port(port_text, &g_configured_port))
        {
            std::printf("HA config port is invalid: %s\n", port_text);
            return false;
        }

        host_end = port_end;
    }

    if (*host_end == '/' && std::strcmp(host_end, "/") != 0)
    {
        std::printf("HA config should not include a path; using host root only\n");
    }

    return true;
}

/// @brief Schedules the next Home Assistant attempt after a delay.
void schedule_retry(uint32_t delay_ms)
{
    g_next_attempt = make_timeout_time_ms(delay_ms);
}

/// @brief Updates the public Home Assistant status snapshot.
void set_status(HomeAssistantConnectionState state, int last_error, int last_http_status)
{
    g_status.state = state;
    g_status.last_error = last_error;
    g_status.last_http_status = last_http_status;
}

/// @brief Removes all lwIP callbacks from a TCP control block.
void clear_tcp_callbacks(tcp_pcb* pcb)
{
    if (pcb == nullptr)
    {
        return;
    }

    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
}

/// @brief Closes the current TCP control block if one is active.
void close_pcb()
{
    if (g_pcb == nullptr)
    {
        return;
    }

    tcp_pcb* pcb = g_pcb;
    g_pcb = nullptr;
    cyw43_arch_lwip_begin();
    clear_tcp_callbacks(pcb);
    const err_t close_rc = tcp_close(pcb);
    if (close_rc != ERR_OK)
    {
        tcp_abort(pcb);
    }
    cyw43_arch_lwip_end();
}

/// @brief Clears all transient request, socket, and response state.
void reset_attempt_state()
{
    close_pcb();
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0;
    g_response_len = 0;
    g_request_body[0] = '\0';
    g_response[0] = '\0';
    g_deadline = nil_time;
}

/// @brief Finishes the current request and schedules the next retry or refresh.
void finish_request(HomeAssistantConnectionState state, int last_error, int last_http_status,
                    uint32_t retry_ms)
{
    set_status(state, last_error, last_http_status);
    g_next_attempt = (retry_ms > 0) ? make_timeout_time_ms(retry_ms) : nil_time;
    reset_attempt_state();
}

/// @brief Finalizes a successful request and advances the polling sequence.
void finish_request_success(int http_status)
{
    set_status(HomeAssistantConnectionState::Connected, 0, http_status);
    reset_attempt_state();
    advance_request_kind();
}

/// @brief Converts an optional request failure into cleared data and continued polling.
void finish_optional_request_soft_failure(err_t err)
{
    const int http_status = partial_http_status();

    switch (g_request_kind)
    {
    case RequestKind::FetchTrackedEntity:
        g_status.tracked_entity_state.fill('\0');
        break;
    case RequestKind::FetchWeatherEntity:
        g_status.weather_source_hint.fill('\0');
        g_status.weather_condition.fill('\0');
        g_status.weather_temperature.fill('\0');
        g_status.weather_wind_unit.fill('\0');
        g_weather_wind_source_unit[0] = '\0';
        break;
    case RequestKind::FetchWeatherForecast:
        if (!parse_hourly_forecast_response(response_body()))
        {
            clear_weather_forecast();
        }
        break;
    case RequestKind::FetchSunEntity:
        g_status.sunrise_text.fill('\0');
        g_status.sunset_text.fill('\0');
        break;
    case RequestKind::PublishSelfEntity:
        g_status.self_entity_published = false;
        break;
    case RequestKind::ProbeApi:
        break;
    }

    reset_attempt_state();
    set_status(HomeAssistantConnectionState::Connected, 0,
               http_status > 0 ? http_status : g_status.last_http_status);
    advance_request_kind();
    std::printf("HA %s soft-failed err=%d status=%d\n", request_kind_name(g_request_kind),
                static_cast<int>(err), http_status);
}

/// @brief Builds the HTTP request buffer for the current request kind.
bool build_request()
{
    g_request_body[0] = '\0';
    const char* method = "GET";
    const char* target = "/api/";
    char target_buffer[96] = {};

    // Each request kind maps to one explicit REST call so the receive path can
    // infer how to parse the response from the current state machine position.
    if (g_request_kind == RequestKind::FetchTrackedEntity)
    {
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.tracked_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }
    else if (g_request_kind == RequestKind::FetchWeatherEntity)
    {
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.weather_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }
    else if (g_request_kind == RequestKind::FetchWeatherForecast)
    {
        method = "POST";
        target = "/api/services/weather/get_forecasts?return_response";

        const int body_len = std::snprintf(g_request_body, sizeof(g_request_body),
                                           "{\"entity_id\":\"%s\",\"type\":\"hourly\"}",
                                           g_status.weather_entity_id.data());
        if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(g_request_body))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
    }
    else if (g_request_kind == RequestKind::FetchSunEntity)
    {
        const int target_len =
            std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s", kSunEntityId);
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }
    else if (g_request_kind == RequestKind::PublishSelfEntity)
    {
        method = "POST";
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.self_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }

        const int body_len =
            std::snprintf(g_request_body, sizeof(g_request_body),
                          "{\"state\":\"online\",\"attributes\":{\"friendly_name\":\"MerlinCCU\","
                          "\"integration\":\"rest_api\",\"status\":\"connected\"}}");
        if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(g_request_body))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }

    // The request buffer is assembled as one contiguous HTTP message because
    // the send path may need to write it in multiple TCP chunks later.
    const int len = std::snprintf(
        g_request, sizeof(g_request),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        method, target, g_configured_host, static_cast<unsigned>(g_configured_port),
        kHomeAssistantToken, static_cast<unsigned>(std::strlen(g_request_body)), g_request_body);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(g_request))
    {
        set_status(HomeAssistantConnectionState::Error, -1, 0);
        g_sequence_complete = true;
        g_next_attempt = nil_time;
        return false;
    }
    return true;
}

/// @brief Writes as much of the pending HTTP request as lwIP currently allows.
err_t try_send_request();

/// @brief Handles completion of the TCP connect step.
err_t on_tcp_connected(void* arg, tcp_pcb* pcb, err_t err)
{
    (void)arg;

    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, err, 0, kRetryDelayMs);
        std::printf("HA connect failed err=%d\n", static_cast<int>(err));
        return ERR_OK;
    }

    if (request_updates_connection_state())
    {
        set_status(HomeAssistantConnectionState::Authorizing, 0, 0);
    }
    else
    {
        g_status.last_error = 0;
    }
    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return try_send_request();
}

/// @brief Handles asynchronous lwIP TCP errors for the active request.
void on_tcp_error(void* arg, err_t err)
{
    (void)arg;
    g_pcb = nullptr;
    set_status(HomeAssistantConnectionState::Error, err, g_status.last_http_status);
    g_sequence_complete = true;
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0;
    g_response_len = 0;
    g_response[0] = '\0';
    g_deadline = nil_time;
    g_next_attempt = make_timeout_time_ms(kRetryDelayMs);
    std::printf("HA tcp error err=%d\n", static_cast<int>(err));
}

/// @brief Handles the completed HTTP status for the current request kind.
void handle_http_status(int http_status)
{
    // Successful responses update only the fields associated with the current
    // request kind, then advance the serialized polling sequence.
    if (request_kind_success(g_request_kind, http_status))
    {
        if (g_request_kind == RequestKind::FetchTrackedEntity)
        {
            char entity_state[sizeof(g_status.tracked_entity_state)] = {};
            if (extract_json_string_value(response_body(), "\"state\":\"", entity_state,
                                          sizeof(entity_state)))
            {
                copy_text(g_status.tracked_entity_state, entity_state);
            }
            else
            {
                copy_text(g_status.tracked_entity_state, "?");
            }
            PERIODIC_LOG("HA entity state %s=%s\n", g_status.tracked_entity_id.data(),
                         g_status.tracked_entity_state.data());
        }
        else if (g_request_kind == RequestKind::FetchWeatherEntity)
        {
            char raw_condition[24] = {};
            char raw_temperature[12] = {};
            char raw_unit[8] = {};
            char raw_wind_unit[16] = {};

            update_weather_source_hint_from_json(response_body());

            if (extract_json_string_value(response_body(), "\"state\":\"", raw_condition,
                                          sizeof(raw_condition)))
            {
                copy_text(g_status.weather_condition, friendly_weather_condition(raw_condition));
            }
            else
            {
                copy_text(g_status.weather_condition, "?");
            }

            if (extract_json_scalar_value(response_body(), "\"temperature\":", raw_temperature,
                                          sizeof(raw_temperature)))
            {
                if (extract_json_string_value(response_body(), "\"temperature_unit\":\"", raw_unit,
                                              sizeof(raw_unit)))
                {
                    const char unit = normalized_temperature_unit(raw_unit);
                    g_weather_temperature_unit = unit;
                    if (unit != '\0')
                    {
                        char formatted_temperature[sizeof(g_status.weather_temperature)] = {};
                        std::snprintf(formatted_temperature, sizeof(formatted_temperature), "%s %c",
                                      raw_temperature, unit);
                        copy_text(g_status.weather_temperature, formatted_temperature);
                    }
                    else
                    {
                        copy_text(g_status.weather_temperature, raw_temperature);
                    }
                }
                else
                {
                    copy_text(g_status.weather_temperature, raw_temperature);
                }
            }
            else
            {
                g_status.weather_temperature.fill('\0');
            }

            if (extract_json_string_value(response_body(), "\"wind_speed_unit\":\"", raw_wind_unit,
                                          sizeof(raw_wind_unit)) &&
                normalize_wind_speed_unit(raw_wind_unit, g_weather_wind_source_unit,
                                          sizeof(g_weather_wind_source_unit)))
            {
                copy_text(g_status.weather_wind_unit, "mph");
            }
            else
            {
                g_weather_wind_source_unit[0] = '\0';
                g_status.weather_wind_unit.fill('\0');
            }

            PERIODIC_LOG("HA weather %s=%s %s\n", g_status.weather_entity_id.data(),
                         g_status.weather_condition[0] ? g_status.weather_condition.data() : "?",
                         g_status.weather_temperature[0] ? g_status.weather_temperature.data()
                                                         : "-");
        }
        else if (g_request_kind == RequestKind::FetchWeatherForecast)
        {
            if (parse_hourly_forecast_response(response_body()))
            {
                PERIODIC_LOG("HA hourly forecast %s count=%u\n", g_status.weather_entity_id.data(),
                             static_cast<unsigned>(g_status.weather_forecast_count));
            }
            else
            {
                clear_weather_forecast();
                PERIODIC_LOG("HA hourly forecast parse failed %s\n",
                             g_status.weather_entity_id.data());
            }
        }
        else if (g_request_kind == RequestKind::FetchSunEntity)
        {
            update_sun_times_from_json(response_body());
            PERIODIC_LOG("HA sun %s rise=%s set=%s\n", kSunEntityId,
                         g_status.sunrise_text[0] ? g_status.sunrise_text.data() : "-",
                         g_status.sunset_text[0] ? g_status.sunset_text.data() : "-");
        }
        else if (g_request_kind == RequestKind::PublishSelfEntity)
        {
            g_status.self_entity_published = true;
            PERIODIC_LOG("HA self entity posted %s status=%d\n", g_status.self_entity_id.data(),
                         http_status);
        }
        else
        {
            PERIODIC_LOG("HA API probe ok host=%s port=%u status=%d\n", g_configured_host,
                         static_cast<unsigned>(g_configured_port), http_status);
        }

        g_status.last_http_status = http_status;
        finish_request_success(http_status);
        return;
    }

    // Authorization failure is treated as terminal because retrying with the
    // same bearer token would only spam the server and hide the real problem.
    if (http_status == 401)
    {
        g_status.last_http_status = http_status;
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Unauthorized, 0, http_status, 0);
        PERIODIC_LOG("HA %s request unauthorized host=%s port=%u\n",
                     request_kind_name(g_request_kind), g_configured_host,
                     static_cast<unsigned>(g_configured_port));
        return;
    }

    // Optional requests fail soft so the rest of the sequence can still
    // complete and the UI can keep whatever data remains available.
    if (g_request_kind == RequestKind::FetchTrackedEntity)
    {
        g_status.tracked_entity_state.fill('\0');
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA tracked entity unavailable %s status=%d\n",
                     g_status.tracked_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::FetchWeatherEntity)
    {
        g_status.weather_source_hint.fill('\0');
        g_status.weather_condition.fill('\0');
        g_status.weather_temperature.fill('\0');
        g_status.weather_wind_unit.fill('\0');
        g_weather_wind_source_unit[0] = '\0';
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA weather entity unavailable %s status=%d\n",
                     g_status.weather_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::FetchWeatherForecast)
    {
        clear_weather_forecast();
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA hourly forecast unavailable %s status=%d\n",
                     g_status.weather_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::FetchSunEntity)
    {
        g_status.sunrise_text.fill('\0');
        g_status.sunset_text.fill('\0');
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA sun entity unavailable %s status=%d\n", kSunEntityId, http_status);
        return;
    }

    if (g_request_kind == RequestKind::PublishSelfEntity)
    {
        g_status.self_entity_published = false;
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA self entity unavailable %s status=%d\n", g_status.self_entity_id.data(),
                     http_status);
        return;
    }

    g_status.last_http_status = http_status;
    g_sequence_complete = true;
    finish_request(HomeAssistantConnectionState::Error, 0, http_status, kRetryDelayMs);
    PERIODIC_LOG("HA %s request unexpected status=%d host=%s port=%u\n",
                 request_kind_name(g_request_kind), http_status, g_configured_host,
                 static_cast<unsigned>(g_configured_port));
}

/// @brief Handles inbound TCP data and connection close events.
err_t on_tcp_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t err)
{
    (void)arg;

    if (pcb != g_pcb)
    {
        if (p != nullptr)
        {
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        if (p != nullptr)
        {
            pbuf_free(p);
        }
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, err, g_status.last_http_status,
                       kRetryDelayMs);
        return ERR_OK;
    }

    if (p == nullptr)
    {
        int http_status = 0;
        err_t protocol_error = ERR_VAL;
        if (!validate_http_response(&http_status, &protocol_error))
        {
            // Why we fail here:
            // This is the boundary between raw network input and application
            // state. If the response is malformed, incomplete, or uses an HTTP
            // feature this client does not understand, we stop here and treat
            // it like a failed request. That is easier to reason about than
            // letting deeper parsing code guess what the response meant.
            if (request_updates_connection_state())
            {
                g_sequence_complete = true;
                finish_request(HomeAssistantConnectionState::Error, protocol_error,
                               partial_http_status(), kRetryDelayMs);
            }
            else
            {
                finish_optional_request_soft_failure(protocol_error);
            }
            return ERR_OK;
        }

        handle_http_status(http_status);
        return ERR_OK;
    }

    // Accumulate the full HTTP response into one buffer so the parsers can work
    // with a stable, contiguous string after the connection closes.
    const uint16_t received_len = p->tot_len;
    const size_t space_left = sizeof(g_response) - g_response_len - 1;
    const uint16_t copy_len =
        static_cast<uint16_t>((received_len < space_left) ? received_len : space_left);

    if (copy_len > 0)
    {
        pbuf_copy_partial(p, g_response + g_response_len, copy_len, 0);
        g_response_len += copy_len;
        g_response[g_response_len] = '\0';
        g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    }

    tcp_recved(pcb, received_len);
    pbuf_free(p);
    if (g_response_len + 1 >= sizeof(g_response))
    {
        if (request_updates_connection_state())
        {
            g_sequence_complete = true;
            finish_request(HomeAssistantConnectionState::Error, ERR_BUF, partial_http_status(),
                           kRetryDelayMs);
        }
        else
        {
            finish_optional_request_soft_failure(ERR_BUF);
        }
        return ERR_OK;
    }

    return ERR_OK;
}

/// @brief Writes as much of the pending HTTP request as lwIP currently allows.
err_t try_send_request()
{
    if (g_pcb == nullptr)
    {
        return ERR_CLSD;
    }

    const size_t request_len = std::strlen(g_request);
    // TCP send buffer space can be smaller than the request, so the request is
    // streamed out in chunks and resumed by later callbacks when necessary.
    while (g_request_sent < request_len)
    {
        const u16_t sndbuf = tcp_sndbuf(g_pcb);
        if (sndbuf == 0)
        {
            return ERR_OK;
        }

        const size_t remaining = request_len - g_request_sent;
        const u16_t chunk = static_cast<u16_t>((remaining < sndbuf) ? remaining : sndbuf);
        if (chunk == 0)
        {
            return ERR_OK;
        }

        const err_t write_rc =
            tcp_write(g_pcb, g_request + g_request_sent, chunk,
                      TCP_WRITE_FLAG_COPY |
                          ((g_request_sent + chunk < request_len) ? TCP_WRITE_FLAG_MORE : 0));
        if (write_rc != ERR_OK)
        {
            if (write_rc == ERR_MEM)
            {
                return ERR_OK;
            }

            g_sequence_complete = true;
            finish_request(HomeAssistantConnectionState::Error, write_rc, 0, kRetryDelayMs);
            return write_rc;
        }

        g_request_sent += chunk;
    }

    const err_t output_rc = tcp_output(g_pcb);
    if (output_rc != ERR_OK)
    {
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, output_rc, 0, kRetryDelayMs);
        return output_rc;
    }

    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return ERR_OK;
}

/// @brief Handles lwIP send acknowledgements for the active request.
err_t on_tcp_sent(void* arg, tcp_pcb* pcb, u16_t len)
{
    (void)arg;
    (void)len;

    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return try_send_request();
}

/// @brief Handles lwIP poll callbacks for the active request.
err_t on_tcp_poll(void* arg, tcp_pcb* pcb)
{
    (void)arg;

    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    if (g_status.state == HomeAssistantConnectionState::Authorizing &&
        g_request_sent < std::strlen(g_request))
    {
        return try_send_request();
    }

    return ERR_OK;
}

/// @brief Handles completion of the Home Assistant DNS lookup.
void dns_found(const char* name, const ip_addr_t* ipaddr, void* arg)
{
    (void)name;
    (void)arg;

    if (!g_dns_pending)
    {
        return;
    }

    g_dns_pending = false;
    if (ipaddr == nullptr)
    {
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, 0, kRetryDelayMs);
        std::printf("HA DNS resolution failed for host=%s\n", g_configured_host);
        return;
    }

    g_resolved_ip = *ipaddr;
    g_dns_resolved = true;
}

/// @brief Opens the TCP socket and starts the HTTP request connect phase.
bool start_socket_connect()
{
    if (!build_request())
    {
        return false;
    }

    cyw43_arch_lwip_begin();
    g_pcb = tcp_new_ip_type(IP_GET_TYPE(&g_resolved_ip));
    if (g_pcb == nullptr)
    {
        cyw43_arch_lwip_end();
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, ERR_MEM, 0, kRetryDelayMs);
        return false;
    }

    tcp_arg(g_pcb, nullptr);
    tcp_recv(g_pcb, on_tcp_recv);
    tcp_sent(g_pcb, on_tcp_sent);
    tcp_err(g_pcb, on_tcp_error);
    tcp_poll(g_pcb, on_tcp_poll, kTcpPollInterval);

    const err_t rc = tcp_connect(g_pcb, &g_resolved_ip, g_configured_port, on_tcp_connected);
    cyw43_arch_lwip_end();
    if (rc == ERR_OK)
    {
        if (request_updates_connection_state())
        {
            set_status(HomeAssistantConnectionState::Connecting, 0, 0);
        }
        else
        {
            g_status.last_error = 0;
        }
        g_deadline = make_timeout_time_ms(kConnectTimeoutMs);
        return true;
    }

    g_sequence_complete = true;
    finish_request(HomeAssistantConnectionState::Error, rc, 0, kRetryDelayMs);
    std::printf("HA connect start failed err=%d\n", static_cast<int>(rc));
    return false;
}

/// @brief Starts the next Home Assistant request in the polling sequence.
bool start_probe()
{
    g_probe_attempted = true;
    reset_attempt_state();
    g_status.last_error = 0;
    if (request_updates_connection_state())
    {
        g_status.last_http_status = 0;
    }
    PERIODIC_LOG("HA starting %s request\n", request_kind_name(g_request_kind));

    // Literal IP targets skip DNS so local integration testing can work even
    // when name resolution is not ready on the current network.
    ip_addr_t parsed = {};
    if (ipaddr_aton(g_configured_host, &parsed))
    {
        g_resolved_ip = parsed;
        g_dns_resolved = true;
        return start_socket_connect();
    }

    cyw43_arch_lwip_begin();
    const err_t dns_rc = dns_gethostbyname(g_configured_host, &g_resolved_ip, dns_found, nullptr);
    cyw43_arch_lwip_end();
    if (dns_rc == ERR_OK)
    {
        g_dns_resolved = true;
        if (request_updates_connection_state())
        {
            set_status(HomeAssistantConnectionState::Resolving, 0, 0);
        }
        return start_socket_connect();
    }

    if (dns_rc == ERR_INPROGRESS)
    {
        g_dns_pending = true;
        if (request_updates_connection_state())
        {
            set_status(HomeAssistantConnectionState::Resolving, 0, 0);
        }
        g_deadline = make_timeout_time_ms(kResolveTimeoutMs);
        PERIODIC_LOG("HA resolving host=%s\n", g_configured_host);
        return true;
    }

    g_sequence_complete = true;
    finish_request(HomeAssistantConnectionState::Error, dns_rc, 0, kRetryDelayMs);
    std::printf("HA dns_gethostbyname failed err=%d host=%s\n", static_cast<int>(dns_rc),
                g_configured_host);
    return false;
}

} // namespace

namespace home_assistant_manager
{

/// @brief Initializes Home Assistant configuration and runtime state.
void init()
{
    // Configuration is normalized once here so the periodic update loop only
    // has to decide whether to wait, connect, or parse responses.
    g_config_valid = parse_home_assistant_endpoint();
    g_status = {};
    g_status.configured =
        kHomeAssistantConfigured && g_config_valid && kHomeAssistantToken[0] != '\0';
    copy_text(g_status.host, g_configured_host);
    copy_text(g_status.tracked_entity_id, kTrackedEntityId);
    copy_text(g_status.weather_entity_id, kWeatherEntityId);
    copy_text(g_status.self_entity_id, kSelfEntityId);
    clear_runtime_data();
    g_status.last_error = 0;
    g_status.last_http_status = 0;
    g_status.state = !g_status.configured ? HomeAssistantConnectionState::Unconfigured
                                          : (kHomeAssistantRuntimeEnabled
                                                 ? HomeAssistantConnectionState::WaitingForWifi
                                                 : HomeAssistantConnectionState::Disabled);
    reset_attempt_state();
    g_next_attempt = nil_time;
    g_probe_attempted = false;
    g_sequence_complete = false;
    g_request_kind = RequestKind::ProbeApi;
}

/// @brief Advances the Home Assistant resolve/connect/request state machine.
bool update(const WifiStatus& wifi_status)
{
    const HomeAssistantStatus previous = g_status;

    // Handle disabled and unconfigured cases first so the remaining logic can
    // assume Home Assistant is supposed to be active.
    if (!g_status.configured)
    {
        g_status.state = HomeAssistantConnectionState::Unconfigured;
        return previous.state != g_status.state || previous.configured != g_status.configured;
    }

    if (!kHomeAssistantRuntimeEnabled)
    {
        g_status.state = HomeAssistantConnectionState::Disabled;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        clear_runtime_data();
        reset_attempt_state();
        g_next_attempt = nil_time;
        g_probe_attempted = false;
        g_sequence_complete = false;
        g_request_kind = RequestKind::ProbeApi;
        return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
    }

    const bool wifi_ready = wifi_status.ip_address[0] != '\0';
    if (!wifi_ready)
    {
        reset_attempt_state();
        g_status.state = HomeAssistantConnectionState::WaitingForWifi;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        clear_runtime_data();
        g_next_attempt = nil_time;
        g_probe_attempted = false;
        g_sequence_complete = false;
        g_request_kind = RequestKind::ProbeApi;
        return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
    }

    // Resolver and socket operations are asynchronous, so deadlines are checked
    // here in the update loop rather than buried inside lwIP callbacks.
    if (g_dns_pending && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        g_dns_pending = false;
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, 0, kRetryDelayMs);
    }

    // After DNS completes, the next update tick actually opens the socket so
    // the callback path stays simple and one-way.
    if (g_dns_resolved && g_pcb == nullptr)
    {
        g_dns_resolved = false;
        start_socket_connect();
    }

    // Socket operations also share the same deadline mechanism so hung connects
    // and hung reads both converge on the same retry path.
    if (g_pcb != nullptr && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, g_status.last_http_status,
                       kRetryDelayMs);
    }

    // New requests are only launched when the current step is idle and either
    // the initial probe has not happened yet or the retry/refresh timer expired.
    if (!g_dns_pending && g_pcb == nullptr &&
        (!g_probe_attempted || (!is_nil_time(g_next_attempt) &&
                                absolute_time_diff_us(get_absolute_time(), g_next_attempt) <= 0)))
    {
        g_next_attempt = nil_time;
        start_probe();
    }

    return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
}

const HomeAssistantStatus& status()
{
    return g_status;
}

} // namespace home_assistant_manager

#include "web_config_server.h"

#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config_manager.h"
#include "console_controller.h"
#include "debug_logging.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"

namespace web_config_server
{

namespace
{

constexpr uint16_t kHttpPort = 80;
constexpr size_t kRequestCapacity = 4096;
constexpr size_t kResponseCapacity = 12000;
constexpr u16_t kTcpWriteChunkMax = 512;
constexpr char kHttpOkHeader[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n";
constexpr char kHttpBadRequestHeader[] =
    "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n";

struct WebSession
{
    tcp_pcb* pcb;
    size_t request_len;
    size_t response_len;
    size_t response_sent;
    bool close_pending;
    char request[kRequestCapacity];
};

tcp_pcb* g_listener = nullptr;
WebSession g_session = {};
char g_response[kResponseCapacity] = {};

/// @brief Clears callbacks and closes a completed HTTP session.
/// @details lwIP raw TCP can reject `tcp_close` while buffers are still tight.
/// In that case the session is marked pending and the sent callback retries
/// once ACKs free enough space.
err_t close_session(WebSession* session, tcp_pcb* pcb)
{
    if (session == nullptr || pcb == nullptr)
    {
        return ERR_ARG;
    }

    const err_t rc = tcp_close(pcb);
    if (rc == ERR_OK)
    {
        if (g_session.pcb == pcb)
        {
            g_session = {};
        }
    }
    else
    {
        session->close_pending = true;
    }

    return rc;
}

/// @brief Queues as much of the pending HTTP response as lwIP can accept.
/// @details Pico W builds commonly have a TCP send buffer smaller than the
/// full configuration page, so the response must be dribbled out over sent
/// callbacks rather than written in a single `tcp_write` call.
err_t send_pending_response(WebSession* session, tcp_pcb* pcb)
{
    if (session == nullptr || pcb == nullptr)
    {
        return ERR_ARG;
    }

    while (session->response_sent < session->response_len)
    {
        const u16_t available = tcp_sndbuf(pcb);
        if (available == 0)
        {
            break;
        }

        const size_t remaining = session->response_len - session->response_sent;
        u16_t chunk = static_cast<u16_t>(remaining < available ? remaining : available);
        if (chunk > kTcpWriteChunkMax)
        {
            chunk = kTcpWriteChunkMax;
        }

        const err_t rc =
            tcp_write(pcb, g_response + session->response_sent, chunk, TCP_WRITE_FLAG_COPY);
        if (rc == ERR_MEM)
        {
            PERIODIC_LOG("Web config tcp_write waiting for buffer space at %u/%u bytes\n",
                         static_cast<unsigned>(session->response_sent),
                         static_cast<unsigned>(session->response_len));
            break;
        }
        if (rc != ERR_OK)
        {
            PERIODIC_LOG("Web config tcp_write failed: %d\n", static_cast<int>(rc));
            (void)tcp_abort(pcb);
            if (g_session.pcb == pcb)
            {
                g_session = {};
            }
            return ERR_ABRT;
        }

        session->response_sent += chunk;
    }

    (void)tcp_output(pcb);
    if (session->response_sent >= session->response_len)
    {
        return close_session(session, pcb);
    }

    return ERR_OK;
}

err_t on_sent(void* arg, tcp_pcb* pcb, u16_t len)
{
    (void)len;
    WebSession* session = static_cast<WebSession*>(arg);
    if (session == nullptr)
    {
        return ERR_OK;
    }

    if (session->close_pending)
    {
        (void)close_session(session, pcb);
        return ERR_OK;
    }

    return send_pending_response(session, pcb);
}

/// @brief Copies a C string into one fixed-size configuration field.
template <size_t N>
void copy_text(std::array<char, N>& dest, const char* src)
{
    dest.fill('\0');
    if (src == nullptr)
    {
        return;
    }

    std::snprintf(dest.data(), dest.size(), "%s", src);
}

/// @brief Returns a lower-case hexadecimal value or -1 for non-hex characters.
int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

/// @brief URL-decodes a form fragment in place.
void url_decode_in_place(char* text)
{
    char* out = text;
    for (char* in = text; *in != '\0'; ++in)
    {
        if (*in == '+')
        {
            *out++ = ' ';
            continue;
        }
        if (*in == '%' && in[1] != '\0' && in[2] != '\0')
        {
            const int hi = hex_value(in[1]);
            const int lo = hex_value(in[2]);
            if (hi >= 0 && lo >= 0)
            {
                *out++ = static_cast<char>((hi << 4) | lo);
                in += 2;
                continue;
            }
        }
        *out++ = *in;
    }
    *out = '\0';
}

/// @brief Appends formatted text to the response buffer.
bool append(char*& cursor, size_t& remaining, const char* format, ...)
{
    if (remaining == 0)
    {
        return false;
    }

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(cursor, remaining, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= remaining)
    {
        return false;
    }

    cursor += written;
    remaining -= static_cast<size_t>(written);
    return true;
}

/// @brief Escapes a string for safe HTML attribute/body use.
void html_escape(const char* input, char* output, size_t output_size)
{
    if (output_size == 0)
    {
        return;
    }

    output[0] = '\0';
    size_t out = 0;
    const char* source = input != nullptr ? input : "";
    for (size_t i = 0; source[i] != '\0' && out + 1 < output_size; ++i)
    {
        const char* replacement = nullptr;
        switch (source[i])
        {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        default:
            output[out++] = source[i];
            output[out] = '\0';
            continue;
        }

        const size_t repl_len = std::strlen(replacement);
        if (out + repl_len >= output_size)
        {
            break;
        }
        std::memcpy(output + out, replacement, repl_len);
        out += repl_len;
        output[out] = '\0';
    }
}

/// @brief Returns the machine-readable token for one enum value.
const char* weather_token(WeatherSource source)
{
    switch (source)
    {
    case WeatherSource::HomeAssistant:
        return "home_assistant";
    case WeatherSource::MetOffice:
        return "met_office";
    case WeatherSource::BbcWeather:
        return "bbc_weather";
    }
    return "home_assistant";
}

const char* time_zone_token(TimeZoneSelection zone)
{
    switch (zone)
    {
    case TimeZoneSelection::AtlanticStandard:
        return "atlantic";
    case TimeZoneSelection::ArgentinaStandard:
        return "argentina";
    case TimeZoneSelection::SouthGeorgia:
        return "south_georgia";
    case TimeZoneSelection::Azores:
        return "azores";
    case TimeZoneSelection::EuropeLondon:
        return "london";
    case TimeZoneSelection::CentralEuropean:
        return "central_european";
    case TimeZoneSelection::EasternEuropean:
        return "eastern_european";
    case TimeZoneSelection::ArabiaStandard:
        return "arabia";
    case TimeZoneSelection::GulfStandard:
        return "gulf";
    }
    return "london";
}

const char* saver_token(ScreenSaverSelection saver)
{
    switch (saver)
    {
    case ScreenSaverSelection::Life:
        return "life";
    case ScreenSaverSelection::Clock:
        return "clock";
    case ScreenSaverSelection::Starfield:
        return "starfield";
    case ScreenSaverSelection::Matrix:
        return "matrix";
    case ScreenSaverSelection::Radar:
        return "radar";
    case ScreenSaverSelection::Rain:
        return "rain";
    case ScreenSaverSelection::Worms:
        return "worms";
    case ScreenSaverSelection::Random:
        return "random";
    }
    return "life";
}

/// @brief Appends one HTML option and marks it selected when appropriate.
bool append_option(char*& cursor, size_t& remaining, const char* value, const char* label,
                   const char* selected)
{
    return append(cursor, remaining, "<option value=\"%s\"%s>%s</option>", value,
                  std::strcmp(value, selected) == 0 ? " selected" : "", label);
}

/// @brief Builds the professional single-page configuration UI.
void build_config_page(const char* message)
{
    const RuntimeConfig& cfg = config_manager::settings();
    char* cursor = g_response;
    size_t remaining = sizeof(g_response);
    char device_name[64] = {};
    char device_label[64] = {};
    char location[64] = {};
    char room[64] = {};
    char wifi_ssid[80] = {};
    char ha_host[96] = {};
    char ha_entity[96] = {};
    char ha_self[96] = {};
    char weather_entity[96] = {};
    char sun_entity[96] = {};
    char mqtt_host[96] = {};
    char mqtt_user[96] = {};
    char mqtt_prefix[64] = {};
    char mqtt_topic[96] = {};
    char escaped_message[160] = {};

    html_escape(cfg.device_name.data(), device_name, sizeof(device_name));
    html_escape(cfg.device_label.data(), device_label, sizeof(device_label));
    html_escape(cfg.location.data(), location, sizeof(location));
    html_escape(cfg.room.data(), room, sizeof(room));
    html_escape(cfg.wifi_ssid.data(), wifi_ssid, sizeof(wifi_ssid));
    html_escape(cfg.home_assistant_host.data(), ha_host, sizeof(ha_host));
    html_escape(cfg.home_assistant_entity_id.data(), ha_entity, sizeof(ha_entity));
    html_escape(cfg.home_assistant_self_entity_id.data(), ha_self, sizeof(ha_self));
    html_escape(cfg.weather_entity_id.data(), weather_entity, sizeof(weather_entity));
    html_escape(cfg.sun_entity_id.data(), sun_entity, sizeof(sun_entity));
    html_escape(cfg.mqtt_host.data(), mqtt_host, sizeof(mqtt_host));
    html_escape(cfg.mqtt_username.data(), mqtt_user, sizeof(mqtt_user));
    html_escape(cfg.mqtt_discovery_prefix.data(), mqtt_prefix, sizeof(mqtt_prefix));
    html_escape(cfg.mqtt_base_topic.data(), mqtt_topic, sizeof(mqtt_topic));
    html_escape(message, escaped_message, sizeof(escaped_message));

    (void)append(cursor, remaining,
                 "%s"
                 "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,"
                 "initial-scale=1\"><title>Merlin CCU</title><style>"
                 ":root{color-scheme:dark;--bg:#08110f;--panel:#111f1b;--panel2:#162a24;"
                 "--line:#2b4a41;--text:#eefbf3;--muted:#8eb5a7;--accent:#b7ff57;"
                 "--warn:#ffcf5a}*{box-sizing:border-box}body{margin:0;background:"
                 "radial-gradient(circle at 20%% 0,#193c31 0,#08110f 42%%,#050807 100%%);"
                 "font:15px/1.45 'Segoe UI',Tahoma,sans-serif;color:var(--text)}"
                 ".wrap{max-width:1080px;margin:0 auto;padding:32px 18px 48px}.hero{display:flex;"
                 "justify-content:space-between;gap:18px;align-items:flex-end;margin-bottom:22px}"
                 "h1{font-size:34px;margin:0;letter-spacing:.04em}.tag{color:var(--accent);"
                 "font-weight:700;text-transform:uppercase;font-size:12px;letter-spacing:.18em}"
                 ".sub{color:var(--muted);max-width:620px}.pill{border:1px solid var(--line);"
                 "border-radius:999px;padding:10px 14px;color:var(--accent);background:#0d1815}"
                 ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}"
                 ".card{background:linear-gradient(180deg,var(--panel),#0c1714);border:1px solid"
                 " var(--line);border-radius:18px;padding:18px;box-shadow:0 20px 70px #0008}"
                 ".card h2{margin:0 0 12px;font-size:15px;letter-spacing:.12em;text-transform:"
                 "uppercase;color:var(--accent)}label{display:block;color:var(--muted);font-size:"
                 "12px;text-transform:uppercase;letter-spacing:.08em;margin:11px 0 5px}"
                 "input,select{width:100%%;border:1px solid #315348;background:#0a1411;color:"
                 "var(--text);border-radius:10px;padding:10px 11px;font:inherit}input:focus,"
                 "select:focus{outline:2px solid #8fdc4b66;border-color:var(--accent)}"
                 ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.check{display:flex;"
                 "gap:9px;align-items:center;margin-top:12px;color:var(--muted)}.check input{"
                 "width:auto}.actions{position:sticky;bottom:0;margin-top:18px;background:"
                 "linear-gradient(180deg,#08110f00,#08110fee 26%%,#08110f);padding-top:18px;"
                 "display:flex;justify-content:space-between;gap:12px;align-items:center}"
                 "button{border:0;border-radius:999px;background:var(--accent);color:#09110d;"
                 "font-weight:800;padding:12px 20px;letter-spacing:.05em}.msg{color:var(--warn);"
                 "font-weight:700}.hint{font-size:12px;color:var(--muted);margin-top:6px}"
                 "@media(max-width:760px){.grid,.row{grid-template-columns:1fr}.hero{display:block}"
                 "h1{font-size:28px}}</style></head><body><main class=\"wrap\"><div class=\"hero\">"
                 "<div><div class=\"tag\">Merlin CCU Control Surface</div><h1>%s</h1><p class=\"sub\">"
                 "Configure this CCU from your local network. Settings are saved in reserved onboard "
                 "flash sectors, not RAM, and loaded at boot. Display settings apply immediately; "
                 "Wi-Fi, Home Assistant and MQTT changes take effect after reboot. A full flash erase "
                 "or firmware image that overwrites the reserved area will remove saved settings.</p></div>"
                 "<div class=\"pill\">Local intranet configuration</div></div>",
                 kHttpOkHeader, device_label[0] ? device_label : device_name);

    if (escaped_message[0] != '\0')
    {
        (void)append(cursor, remaining, "<p class=\"msg\">%s</p>", escaped_message);
    }

    (void)append(cursor, remaining,
                 "<form method=\"post\" action=\"/config\"><div class=\"grid\"><section class=\"card\">"
                 "<h2>Device Identity</h2><label>Device name</label><input name=\"device_name\" "
                 "maxlength=\"31\" value=\"%s\"><label>Display label</label><input name=\"device_label\" "
                 "maxlength=\"31\" value=\"%s\"><div class=\"row\"><div><label>Location</label><input "
                 "name=\"location\" maxlength=\"31\" value=\"%s\"></div><div><label>Room</label><input "
                 "name=\"room\" maxlength=\"31\" value=\"%s\"></div></div><p class=\"hint\">Use names like "
                 "CCU1, CCU2, GroundFloorCCU, KitchenCCU.</p><label>Current admin password</label><input "
                 "name=\"admin_password_current\" type=\"password\" autocomplete=\"current-password\">"
                 "<p class=\"hint\">Factory default is merlin; change it before using the page routinely.</p>"
                 "<label>New admin password</label><input name=\"admin_password_new\" type=\"password\" "
                 "autocomplete=\"new-password\"><label class=\"check\"><input type=\"checkbox\" "
                 "name=\"require_admin\" %s>Require admin password for saves</label><label "
                 "class=\"check\"><input type=\"checkbox\" name=\"remote_config\" %s>Enable local web "
                 "configuration server</label><p class=\"hint\">Disable this if you only want setup changes "
                 "to come from the front panel.</p></section>",
                 device_name, device_label, location, room,
                 cfg.require_admin_password ? "checked" : "",
                 cfg.remote_config_enabled ? "checked" : "");

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>Network</h2><label>Wi-Fi SSID</label><input "
                 "name=\"wifi_ssid\" maxlength=\"32\" value=\"%s\"><label>Wi-Fi password</label>"
                 "<input name=\"wifi_password\" type=\"password\" placeholder=\"Leave blank to keep current\">"
                 "<p class=\"hint\">The web server starts once the CCU joins Wi-Fi. Changing Wi-Fi requires "
                 "a reboot.</p></section>", wifi_ssid);

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>Home Assistant</h2><label class=\"check\"><input "
                 "type=\"checkbox\" name=\"ha_enabled\" %s>Enable REST integration</label><label>Host</label>"
                 "<input name=\"ha_host\" maxlength=\"63\" value=\"%s\"><div class=\"row\"><div><label>Port</label>"
                 "<input name=\"ha_port\" inputmode=\"numeric\" value=\"%u\"></div><div><label>Weather source</label>"
                 "<select name=\"weather_source\">",
                 cfg.home_assistant_enabled ? "checked" : "", ha_host,
                 static_cast<unsigned>(cfg.home_assistant_port));
    const char* selected_weather = weather_token(cfg.weather_source);
    (void)append_option(cursor, remaining, "home_assistant", "Home Assistant", selected_weather);
    (void)append_option(cursor, remaining, "met_office", "Met Office", selected_weather);
    (void)append_option(cursor, remaining, "bbc_weather", "BBC Weather", selected_weather);
    (void)append(cursor, remaining,
                 "</select></div></div><label>Access token</label><input name=\"ha_token\" "
                 "type=\"password\" placeholder=\"Leave blank to keep current\"><label>Tracked entity</label>"
                 "<input name=\"ha_entity\" maxlength=\"63\" value=\"%s\"><label>Self entity</label><input "
                 "name=\"ha_self\" maxlength=\"63\" value=\"%s\"><div class=\"row\"><div><label>Weather entity</label>"
                 "<input name=\"weather_entity\" maxlength=\"63\" value=\"%s\"></div><div><label>Sun entity</label>"
                 "<input name=\"sun_entity\" maxlength=\"63\" value=\"%s\"></div></div></section>",
                 ha_entity, ha_self, weather_entity, sun_entity);

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>MQTT Discovery</h2><label class=\"check\"><input "
                 "type=\"checkbox\" name=\"mqtt_enabled\" %s>Enable MQTT</label><label>Broker host</label>"
                 "<input name=\"mqtt_host\" maxlength=\"63\" value=\"%s\"><div class=\"row\"><div><label>Port</label>"
                 "<input name=\"mqtt_port\" inputmode=\"numeric\" value=\"%u\"></div><div><label>Username</label>"
                 "<input name=\"mqtt_username\" maxlength=\"63\" value=\"%s\"></div></div><label>Password</label>"
                 "<input name=\"mqtt_password\" type=\"password\" placeholder=\"Leave blank to keep current\">"
                 "<div class=\"row\"><div><label>Discovery prefix</label><input name=\"mqtt_prefix\" maxlength=\"31\" "
                 "value=\"%s\"></div><div><label>Base topic</label><input name=\"mqtt_topic\" maxlength=\"63\" "
                 "value=\"%s\"></div></div></section>",
                 cfg.mqtt_enabled ? "checked" : "", mqtt_host,
                 static_cast<unsigned>(cfg.mqtt_port), mqtt_user, mqtt_prefix, mqtt_topic);

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>Display & Time</h2><label>Time zone</label><select name=\"time_zone\">");
    const char* selected_zone = time_zone_token(cfg.time_zone);
    (void)append_option(cursor, remaining, "atlantic", "Atlantic Standard Time", selected_zone);
    (void)append_option(cursor, remaining, "argentina", "Argentina Time", selected_zone);
    (void)append_option(cursor, remaining, "south_georgia", "South Georgia Time", selected_zone);
    (void)append_option(cursor, remaining, "azores", "Azores Time", selected_zone);
    (void)append_option(cursor, remaining, "london", "Europe/London", selected_zone);
    (void)append_option(cursor, remaining, "central_european", "Central European Time", selected_zone);
    (void)append_option(cursor, remaining, "eastern_european", "Eastern European Time", selected_zone);
    (void)append_option(cursor, remaining, "arabia", "Arabia Standard Time", selected_zone);
    (void)append_option(cursor, remaining, "gulf", "Gulf Standard Time", selected_zone);
    (void)append(cursor, remaining,
                 "</select><label>Screen saver</label><select name=\"screen_saver\">");
    const char* selected_saver = saver_token(cfg.screen_saver);
    (void)append_option(cursor, remaining, "life", "Life", selected_saver);
    (void)append_option(cursor, remaining, "clock", "Clock", selected_saver);
    (void)append_option(cursor, remaining, "starfield", "Starfield", selected_saver);
    (void)append_option(cursor, remaining, "matrix", "Matrix", selected_saver);
    (void)append_option(cursor, remaining, "radar", "Radar", selected_saver);
    (void)append_option(cursor, remaining, "rain", "Rain", selected_saver);
    (void)append_option(cursor, remaining, "worms", "Worms", selected_saver);
    (void)append_option(cursor, remaining, "random", "Random", selected_saver);
    (void)append(cursor, remaining,
                 "</select><label>Screen-saver timeout minutes</label><input name=\"screen_timeout\" "
                 "inputmode=\"numeric\" value=\"%u\"><p class=\"hint\">0 disables timeout. Valid range: 0-120."
                 "</p></section></div><div class=\"actions\"><span class=\"hint\">Configuration is saved locally "
                 "on this CCU.</span><button type=\"submit\">Save Configuration</button></div></form></main></body></html>",
                 static_cast<unsigned>(cfg.screen_saver_timeout_minutes));
}

/// @brief Finds one form field value in a mutable URL-encoded body.
bool form_value(char* body, const char* key, char* output, size_t output_size)
{
    if (body == nullptr || key == nullptr || output == nullptr || output_size == 0)
    {
        return false;
    }

    output[0] = '\0';
    const size_t key_len = std::strlen(key);
    char* token = body;
    while (token != nullptr && *token != '\0')
    {
        char* next = std::strchr(token, '&');
        if (next != nullptr)
        {
            *next++ = '\0';
        }

        char* equals = std::strchr(token, '=');
        if (equals != nullptr)
        {
            *equals++ = '\0';
            url_decode_in_place(token);
            url_decode_in_place(equals);
            if (std::strlen(token) == key_len && std::strcmp(token, key) == 0)
            {
                std::snprintf(output, output_size, "%s", equals);
                return true;
            }
        }

        token = next;
    }

    return false;
}

/// @brief Reads a form value without mutating the original POST body.
bool get_form_value(const char* body, const char* key, char* output, size_t output_size)
{
    char copy[kRequestCapacity] = {};
    std::snprintf(copy, sizeof(copy), "%s", body != nullptr ? body : "");
    return form_value(copy, key, output, output_size);
}

bool form_has_key(const char* body, const char* key)
{
    char value[8] = {};
    return get_form_value(body, key, value, sizeof(value));
}

uint16_t parse_u16_or_default(const char* text, uint16_t fallback, uint16_t max_value)
{
    if (text == nullptr || text[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > max_value)
    {
        return fallback;
    }
    return static_cast<uint16_t>(value);
}

WeatherSource parse_weather_source(const char* text, WeatherSource fallback)
{
    if (std::strcmp(text, "met_office") == 0)
    {
        return WeatherSource::MetOffice;
    }
    if (std::strcmp(text, "bbc_weather") == 0)
    {
        return WeatherSource::BbcWeather;
    }
    if (std::strcmp(text, "home_assistant") == 0)
    {
        return WeatherSource::HomeAssistant;
    }
    return fallback;
}

TimeZoneSelection parse_time_zone(const char* text, TimeZoneSelection fallback)
{
    if (std::strcmp(text, "atlantic") == 0)
    {
        return TimeZoneSelection::AtlanticStandard;
    }
    if (std::strcmp(text, "argentina") == 0)
    {
        return TimeZoneSelection::ArgentinaStandard;
    }
    if (std::strcmp(text, "south_georgia") == 0)
    {
        return TimeZoneSelection::SouthGeorgia;
    }
    if (std::strcmp(text, "azores") == 0)
    {
        return TimeZoneSelection::Azores;
    }
    if (std::strcmp(text, "central_european") == 0)
    {
        return TimeZoneSelection::CentralEuropean;
    }
    if (std::strcmp(text, "eastern_european") == 0)
    {
        return TimeZoneSelection::EasternEuropean;
    }
    if (std::strcmp(text, "arabia") == 0)
    {
        return TimeZoneSelection::ArabiaStandard;
    }
    if (std::strcmp(text, "gulf") == 0)
    {
        return TimeZoneSelection::GulfStandard;
    }
    if (std::strcmp(text, "london") == 0)
    {
        return TimeZoneSelection::EuropeLondon;
    }
    return fallback;
}

ScreenSaverSelection parse_screen_saver(const char* text, ScreenSaverSelection fallback)
{
    if (std::strcmp(text, "clock") == 0)
    {
        return ScreenSaverSelection::Clock;
    }
    if (std::strcmp(text, "starfield") == 0)
    {
        return ScreenSaverSelection::Starfield;
    }
    if (std::strcmp(text, "matrix") == 0)
    {
        return ScreenSaverSelection::Matrix;
    }
    if (std::strcmp(text, "radar") == 0)
    {
        return ScreenSaverSelection::Radar;
    }
    if (std::strcmp(text, "rain") == 0)
    {
        return ScreenSaverSelection::Rain;
    }
    if (std::strcmp(text, "worms") == 0)
    {
        return ScreenSaverSelection::Worms;
    }
    if (std::strcmp(text, "random") == 0)
    {
        return ScreenSaverSelection::Random;
    }
    if (std::strcmp(text, "life") == 0)
    {
        return ScreenSaverSelection::Life;
    }
    return fallback;
}

/// @brief Updates settings from a POST body and returns a user-facing message.
const char* handle_config_post(const char* body)
{
    char current_password[40] = {};
    (void)get_form_value(body, "admin_password_current", current_password, sizeof(current_password));
    if (!config_manager::admin_password_matches(current_password))
    {
        return "Configuration not saved: admin password did not match.";
    }

    RuntimeConfig cfg = config_manager::settings();
    char value[160] = {};

    if (get_form_value(body, "device_name", value, sizeof(value)))
    {
        copy_text(cfg.device_name, value);
    }
    if (get_form_value(body, "device_label", value, sizeof(value)))
    {
        copy_text(cfg.device_label, value);
    }
    if (get_form_value(body, "location", value, sizeof(value)))
    {
        copy_text(cfg.location, value);
    }
    if (get_form_value(body, "room", value, sizeof(value)))
    {
        copy_text(cfg.room, value);
    }
    if (get_form_value(body, "admin_password_new", value, sizeof(value)) && value[0] != '\0')
    {
        copy_text(cfg.admin_password, value);
    }
    cfg.require_admin_password = form_has_key(body, "require_admin");
    cfg.remote_config_enabled = form_has_key(body, "remote_config");

    if (get_form_value(body, "wifi_ssid", value, sizeof(value)))
    {
        copy_text(cfg.wifi_ssid, value);
    }
    if (get_form_value(body, "wifi_password", value, sizeof(value)) && value[0] != '\0')
    {
        copy_text(cfg.wifi_password, value);
    }

    cfg.home_assistant_enabled = form_has_key(body, "ha_enabled");
    if (get_form_value(body, "ha_host", value, sizeof(value)))
    {
        copy_text(cfg.home_assistant_host, value);
    }
    if (get_form_value(body, "ha_port", value, sizeof(value)))
    {
        cfg.home_assistant_port = parse_u16_or_default(value, cfg.home_assistant_port, 65535);
    }
    if (get_form_value(body, "ha_token", value, sizeof(value)) && value[0] != '\0')
    {
        copy_text(cfg.home_assistant_token, value);
    }
    if (get_form_value(body, "ha_entity", value, sizeof(value)))
    {
        copy_text(cfg.home_assistant_entity_id, value);
    }
    if (get_form_value(body, "ha_self", value, sizeof(value)))
    {
        copy_text(cfg.home_assistant_self_entity_id, value);
    }
    if (get_form_value(body, "weather_entity", value, sizeof(value)))
    {
        copy_text(cfg.weather_entity_id, value);
    }
    if (get_form_value(body, "sun_entity", value, sizeof(value)))
    {
        copy_text(cfg.sun_entity_id, value);
    }
    if (get_form_value(body, "weather_source", value, sizeof(value)))
    {
        cfg.weather_source = parse_weather_source(value, cfg.weather_source);
    }

    cfg.mqtt_enabled = form_has_key(body, "mqtt_enabled");
    if (get_form_value(body, "mqtt_host", value, sizeof(value)))
    {
        copy_text(cfg.mqtt_host, value);
    }
    if (get_form_value(body, "mqtt_port", value, sizeof(value)))
    {
        cfg.mqtt_port = parse_u16_or_default(value, cfg.mqtt_port, 65535);
    }
    if (get_form_value(body, "mqtt_username", value, sizeof(value)))
    {
        copy_text(cfg.mqtt_username, value);
    }
    if (get_form_value(body, "mqtt_password", value, sizeof(value)) && value[0] != '\0')
    {
        copy_text(cfg.mqtt_password, value);
    }
    if (get_form_value(body, "mqtt_prefix", value, sizeof(value)))
    {
        copy_text(cfg.mqtt_discovery_prefix, value);
    }
    if (get_form_value(body, "mqtt_topic", value, sizeof(value)))
    {
        copy_text(cfg.mqtt_base_topic, value);
    }
    if (get_form_value(body, "time_zone", value, sizeof(value)))
    {
        cfg.time_zone = parse_time_zone(value, cfg.time_zone);
    }
    if (get_form_value(body, "screen_saver", value, sizeof(value)))
    {
        cfg.screen_saver = parse_screen_saver(value, cfg.screen_saver);
    }
    if (get_form_value(body, "screen_timeout", value, sizeof(value)))
    {
        cfg.screen_saver_timeout_minutes = parse_u16_or_default(
            value, cfg.screen_saver_timeout_minutes, 120);
    }

    if (!config_manager::save(cfg))
    {
        return "Configuration not saved: flash write failed.";
    }

    console_controller::apply_runtime_config(config_manager::settings());
    console_controller::request_redraw();
    return "Configuration saved. Display settings and local web-access policy apply now; Wi-Fi and integration transport changes take effect after reboot.";
}

/// @brief Sends one response and closes the TCP session.
/// @brief Starts sending the prepared HTTP response for this session.
void send_response(WebSession* session, tcp_pcb* pcb, const char* response)
{
    if (session == nullptr || pcb == nullptr || response == nullptr)
    {
        return;
    }

    session->response_len = std::strlen(response);
    session->response_sent = 0;
    session->close_pending = false;
    tcp_sent(pcb, on_sent);
    (void)send_pending_response(session, pcb);
}

/// @brief Handles a complete HTTP request from the local config page.
void handle_request(WebSession* session, tcp_pcb* pcb, const char* request)
{
    const char* message = "";
    if (std::strncmp(request, "POST /config ", 13) == 0)
    {
        const char* body = std::strstr(request, "\r\n\r\n");
        message = handle_config_post(body != nullptr ? body + 4 : "");
    }
    else if (std::strncmp(request, "GET /ping ", 10) == 0)
    {
        std::snprintf(g_response, sizeof(g_response),
                      "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                      "Connection: close\r\n\r\nMerlinCCU OK\n");
        send_response(session, pcb, g_response);
        return;
    }
    else if (std::strncmp(request, "GET / ", 6) != 0 &&
             std::strncmp(request, "GET /config ", 12) != 0)
    {
        std::snprintf(g_response, sizeof(g_response), "%sUnknown MerlinCCU configuration path\n",
                      kHttpBadRequestHeader);
        send_response(session, pcb, g_response);
        return;
    }

    build_config_page(message);
    send_response(session, pcb, g_response);
}

/// @brief Returns whether the accumulated request has all bytes needed.
bool request_is_complete(const char* request, size_t request_len)
{
    const char* body = std::strstr(request, "\r\n\r\n");
    if (body == nullptr)
    {
        return false;
    }

    if (std::strncmp(request, "POST ", 5) != 0)
    {
        return true;
    }

    const char* length_header = std::strstr(request, "Content-Length:");
    if (length_header == nullptr || length_header > body)
    {
        return true;
    }

    length_header += std::strlen("Content-Length:");
    while (*length_header == ' ')
    {
        ++length_header;
    }

    const unsigned long content_length = std::strtoul(length_header, nullptr, 10);
    const size_t header_bytes = static_cast<size_t>((body + 4) - request);
    return request_len >= header_bytes + content_length;
}

err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t err)
{
    WebSession* session = static_cast<WebSession*>(arg);
    if (err != ERR_OK || p == nullptr || session == nullptr)
    {
        if (p != nullptr)
        {
            pbuf_free(p);
        }
        (void)tcp_close(pcb);
        return ERR_OK;
    }

    const size_t copy_len =
        (session->request_len + p->tot_len < sizeof(session->request) - 1)
            ? p->tot_len
            : (sizeof(session->request) - 1 - session->request_len);
    if (copy_len > 0)
    {
        pbuf_copy_partial(p, session->request + session->request_len, copy_len, 0);
        session->request_len += copy_len;
        session->request[session->request_len] = '\0';
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (request_is_complete(session->request, session->request_len))
    {
        handle_request(session, pcb, session->request);
    }
    return ERR_OK;
}

void on_error(void* arg, err_t err)
{
    (void)arg;
    (void)err;
    g_session = {};
}

err_t on_accept(void* arg, tcp_pcb* new_pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || new_pcb == nullptr)
    {
        return ERR_VAL;
    }

    if (g_session.pcb != nullptr)
    {
        (void)tcp_close(new_pcb);
        return ERR_OK;
    }

    g_session = {};
    g_session.pcb = new_pcb;
    tcp_arg(new_pcb, &g_session);
    tcp_recv(new_pcb, on_recv);
    tcp_sent(new_pcb, on_sent);
    tcp_err(new_pcb, on_error);
    return ERR_OK;
}

/// @brief Starts listening for local HTTP configuration requests.
bool start_server()
{
    if (g_listener != nullptr || !config_manager::settings().remote_config_enabled)
    {
        return g_listener != nullptr;
    }

    cyw43_arch_lwip_begin();
    tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb == nullptr)
    {
        cyw43_arch_lwip_end();
        return false;
    }

    err_t rc = tcp_bind(pcb, IP_ANY_TYPE, kHttpPort);
    if (rc == ERR_OK)
    {
        g_listener = tcp_listen(pcb);
        if (g_listener != nullptr)
        {
            tcp_accept(g_listener, on_accept);
        }
        else
        {
            rc = ERR_MEM;
        }
    }

    if (rc != ERR_OK)
    {
        tcp_close(pcb);
        g_listener = nullptr;
    }
    cyw43_arch_lwip_end();

    PERIODIC_LOG("Web config server %s on port %u\n", g_listener ? "started" : "failed",
                 static_cast<unsigned>(kHttpPort));
    return g_listener != nullptr;
}

/// @brief Stops the HTTP listener and any active session.
void stop_server()
{
    cyw43_arch_lwip_begin();
    if (g_session.pcb != nullptr)
    {
        tcp_arg(g_session.pcb, nullptr);
        tcp_recv(g_session.pcb, nullptr);
        tcp_err(g_session.pcb, nullptr);
        (void)tcp_close(g_session.pcb);
        g_session = {};
    }
    if (g_listener != nullptr)
    {
        tcp_arg(g_listener, nullptr);
        tcp_accept(g_listener, nullptr);
        (void)tcp_close(g_listener);
        g_listener = nullptr;
    }
    cyw43_arch_lwip_end();
}

} // namespace

void init()
{
    g_listener = nullptr;
    g_session = {};
    g_response[0] = '\0';
}

bool update(const WifiStatus& wifi_status)
{
    const bool should_run =
        config_manager::settings().remote_config_enabled &&
        wifi_status.state == WifiConnectionState::Connected && wifi_status.ip_address[0] != '\0';

    if (g_session.pcb != nullptr && g_session.response_len > 0 &&
        g_session.response_sent < g_session.response_len)
    {
        cyw43_arch_lwip_begin();
        (void)send_pending_response(&g_session, g_session.pcb);
        cyw43_arch_lwip_end();
    }

    if (should_run && g_listener == nullptr)
    {
        return start_server();
    }

    if (!should_run && g_listener != nullptr)
    {
        // Let the current browser request finish cleanly before the listener
        // and session are torn down by a newly disabled local-web policy.
        if (g_session.pcb != nullptr)
        {
            return false;
        }

        stop_server();
        return true;
    }

    return false;
}

} // namespace web_config_server

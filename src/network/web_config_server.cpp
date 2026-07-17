#include "web_config_server.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config_manager.h"
#include "console_controller.h"
#include "date_time_math.h"
#include "debug_logging.h"
#include "framebuffer.h"
#include "geo_coordinates.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "panel_config.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "text_utils.h"

namespace web_config_server
{

namespace
{

constexpr uint16_t kHttpPort = 80;
constexpr size_t kRequestCapacity = 4096;
constexpr size_t kResponseCapacity = 24576;
constexpr u16_t kTcpWriteChunkMax = 1024;
// lwIP's own TCP retransmission/error detection can take tens of seconds to
// give up on a silently-vanished peer (a dropped Wi-Fi packet, or a browser
// abandoning a fetch without a clean FIN/RST reaching us). Nothing else frees
// a stuck singleton session in that case, since this server registers no
// tcp_poll callback today. kSessionIdleTimeoutMs bounds that with an
// app-level watchdog well below lwIP's own timeout, so one stalled preview
// poll can't block every other request for tens of seconds.
// 1.2s rather than something looser: last_activity is refreshed on every
// real recv/send progress (see on_recv/send_pending_response), not just on
// connect, so this only fires for a session making truly zero progress --
// a merely slow-but-advancing transfer over Wi-Fi keeps resetting the clock
// and is never falsely aborted.
constexpr uint32_t kSessionIdleTimeoutMs = 1200U;
// tcp_poll's interval is in units of lwIP's coarse TCP timer (~500 ms), not
// milliseconds directly; 1 is the finest granularity available without
// changing lwIP's own TCP_SLOW_INTERVAL.
constexpr u8_t kSessionPollInterval = 1U;
constexpr char kHttpOkHeader[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store, "
    "max-age=0\r\nPragma: no-cache\r\nConnection: close\r\n\r\n";
constexpr char kHttpBadRequestHeader[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; "
                                         "charset=utf-8\r\nConnection: close\r\n\r\n";
constexpr char kHttpBinaryHeader[] =
    "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\n"
    "Connection: close\r\n\r\n";
constexpr char kHttpPbmHeader[] =
    "HTTP/1.1 200 OK\r\nContent-Type: image/x-portable-bitmap\r\nCache-Control: no-store\r\n"
    "Connection: close\r\n\r\n";
constexpr char kHttpTextHeader[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n";
constexpr char kHttpBusyResponse[] =
    "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\nRetry-After: 1\r\n\r\nMerlinCCU web server is busy, retry shortly.\n";

struct WebSession
{
    tcp_pcb* pcb;
    size_t request_len;
    size_t response_len;
    size_t response_sent;
    bool close_pending;
    bool low_priority_response;
    // Updated on accept and on any recv/send progress; on_poll aborts the
    // session once this is older than kSessionIdleTimeoutMs.
    absolute_time_t last_activity;
    char request[kRequestCapacity];
};

tcp_pcb* g_listener = nullptr;
WebSession g_session = {};
char g_response[kResponseCapacity] = {};

/// @brief Maps web button identifiers to logical firmware button ids.
struct WebButtonBinding
{
    const char* web_id;
    ButtonId button_id;
};

constexpr size_t kLegacyDigitWebBindingCount = 10U;
constexpr size_t kWebButtonBindingCount =
    static_cast<size_t>(ButtonId::Count) + kLegacyDigitWebBindingCount;

constexpr std::array<WebButtonBinding, kWebButtonBindingCount> kWebButtonBindings = {{
    {"LeftTop", ButtonId::LeftTop},         {"LeftUpper", ButtonId::LeftUpper},
    {"LeftMiddle", ButtonId::LeftMiddle},   {"LeftLower", ButtonId::LeftLower},
    {"LeftBottom", ButtonId::LeftBottom},   {"RightTop", ButtonId::RightTop},
    {"RightUpper", ButtonId::RightUpper},   {"RightMiddle", ButtonId::RightMiddle},
    {"RightLower", ButtonId::RightLower},   {"RightBottom", ButtonId::RightBottom},
    {"Alert", ButtonId::Alert},             {"Test", ButtonId::Test},
    {"Brt", ButtonId::Brt},                 {"Dim", ButtonId::Dim},
    {"Ltrs", ButtonId::Ltrs},               {"BackStep", ButtonId::BackStep},
    {"CursorLeft", ButtonId::CursorLeft},   {"CursorRight", ButtonId::CursorRight},
    {"Slash", ButtonId::Slash},             {"Clr", ButtonId::Clr},
    {"AlphaA", ButtonId::AlphaA},           {"AlphaB", ButtonId::AlphaB},
    {"AlphaC", ButtonId::AlphaC},           {"AlphaD", ButtonId::AlphaD},
    {"AlphaE", ButtonId::AlphaE},           {"AlphaF", ButtonId::AlphaF},
    {"AlphaG", ButtonId::AlphaG},           {"AlphaH", ButtonId::AlphaH},
    {"AlphaI", ButtonId::AlphaI},           {"AlphaJ", ButtonId::AlphaJ},
    {"AlphaK", ButtonId::AlphaK},           {"AlphaL", ButtonId::AlphaL},
    {"AlphaM", ButtonId::AlphaM},           {"AlphaN", ButtonId::AlphaN},
    {"AlphaO", ButtonId::AlphaO},           {"AlphaP", ButtonId::AlphaP},
    {"AlphaQ", ButtonId::AlphaQ},           {"AlphaR", ButtonId::AlphaR},
    {"AlphaS", ButtonId::AlphaS},           {"AlphaT", ButtonId::AlphaT},
    {"AlphaU", ButtonId::AlphaU},           {"AlphaV", ButtonId::AlphaV},
    {"AlphaW", ButtonId::AlphaW},           {"AlphaX", ButtonId::AlphaX},
    {"AlphaY", ButtonId::AlphaY},           {"AlphaZ", ButtonId::AlphaZ},
    {"TFunc", ButtonId::TFunc},             {"Dot", ButtonId::Dot},
    {"Zero", ButtonId::Zero},               {"Spc", ButtonId::Spc},
    // Keep old preview pages and cached browser scripts working after the
    // internal model switched from number IDs to physical key IDs.
    {"Digit1", ButtonId::AlphaJ},           {"Digit2", ButtonId::AlphaK},
    {"Digit3", ButtonId::AlphaL},           {"Digit4", ButtonId::AlphaP},
    {"Digit5", ButtonId::AlphaQ},           {"Digit6", ButtonId::AlphaR},
    {"Digit7", ButtonId::AlphaV},           {"Digit8", ButtonId::AlphaW},
    {"Digit9", ButtonId::AlphaX},           {"Digit0", ButtonId::Zero},
}};

constexpr size_t kAlertLampIndex = static_cast<size_t>(LampId::AlertLamp);
constexpr size_t kTestLampIndex = static_cast<size_t>(LampId::TestLamp);

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
        // Disarm callbacks now rather than leaving them wired: tcp_close can
        // leave the pcb lingering in FIN_WAIT/TIME_WAIT with tcp_err/tcp_poll
        // still armed. If the singleton slot gets reused for a new session
        // before this pcb is fully gone, a late callback firing on the old
        // pcb would otherwise still see arg == &g_session and could clobber
        // the new session's state.
        tcp_arg(pcb, nullptr);
        tcp_recv(pcb, nullptr);
        tcp_sent(pcb, nullptr);
        tcp_err(pcb, nullptr);
        tcp_poll(pcb, nullptr, 0);
        if (g_session.pcb == pcb)
        {
            g_session = {};
        }
    }
    else
    {
        // tcp_close can reject while send buffers are still tight; on_sent
        // retries once ACKs free space. Refresh last_activity here so the
        // idle-timeout poll doesn't mistake a legitimate close-retry wait for
        // a stalled connection and abort it (which would surface to the
        // browser as an RST instead of a clean close).
        session->close_pending = true;
        session->last_activity = get_absolute_time();
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

    bool wrote_any = false;

    while (session->response_sent < session->response_len)
    {
        const u16_t available = tcp_sndbuf(pcb);
        if (available == 0)
        {
            break;
        }

        const size_t remaining = session->response_len - session->response_sent;
        const auto writable =
            static_cast<u16_t>(std::min(remaining, static_cast<size_t>(available)));
        const u16_t chunk = std::min(writable, kTcpWriteChunkMax);

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
            tcp_abort(pcb);
            if (g_session.pcb == pcb)
            {
                g_session = {};
            }
            return ERR_ABRT;
        }

        session->response_sent += chunk;
        wrote_any = true;
    }

    // Only refresh last_activity when bytes actually moved. This is called
    // once per main-loop tick from update() whenever a response is still
    // pending, not just from real lwIP send events; stamping unconditionally
    // would mask a genuinely stalled send (tcp_sndbuf staying at 0) from the
    // idle-timeout poll, reintroducing the hang this fix was meant to catch.
    if (wrote_any)
    {
        session->last_activity = get_absolute_time();
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
    auto* session = static_cast<WebSession*>(arg);
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

/// @brief Aborts a preview-style transfer so control requests can take priority.
/// @details The preview framebuffer is deliberately lossy: the browser can
/// request another frame. Button/control requests should not wait behind a slow
/// frame response on the single-session Pico HTTP server.
void abort_active_session()
{
    if (g_session.pcb == nullptr)
    {
        return;
    }

    tcp_arg(g_session.pcb, nullptr);
    tcp_recv(g_session.pcb, nullptr);
    tcp_sent(g_session.pcb, nullptr);
    tcp_err(g_session.pcb, nullptr);
    tcp_abort(g_session.pcb);
    g_session = {};
}

using text_utils::copy_text;

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
    case WeatherSource::OpenMeteo:
    case WeatherSource::MetNorway:
        return "open_meteo";
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

/// @brief Emits a latitude/longitude entry pair (magnitude + hemisphere
/// select, one row per axis) for one coordinate field, pre-filled from its
/// current stored "lat,lon" text. Mirrors the on-device coordinate editor so
/// the two entry paths can never disagree on format or ordering (issue #87).
bool append_coordinate_fields(char*& cursor, size_t& remaining, const char* field_prefix,
                              const char* label_prefix, const char* stored_text)
{
    double lat = 0.0;
    double lon = 0.0;
    double lat_magnitude = 0.0;
    double lon_magnitude = 0.0;
    char lat_hemisphere = 'N';
    char lon_hemisphere = 'E';
    if (geo_coordinates::parse_coordinates_text(stored_text, &lat, &lon))
    {
        (void)geo_coordinates::split_hemisphere(lat, /*is_latitude=*/true, &lat_magnitude,
                                                &lat_hemisphere);
        (void)geo_coordinates::split_hemisphere(lon, /*is_latitude=*/false, &lon_magnitude,
                                                &lon_hemisphere);
    }

    const char lat_hemi_text[2] = {lat_hemisphere, '\0'};
    const char lon_hemi_text[2] = {lon_hemisphere, '\0'};

    bool ok = append(cursor, remaining,
                     "<div class=\"row\"><div><label>%s latitude</label><input name=\"%s_lat\" "
                     "type=\"number\" step=\"0.0001\" min=\"0\" max=\"90\" value=\"%.4f\">"
                     "</div><div><label>N/S</label><select name=\"%s_lat_hemi\">",
                     label_prefix, field_prefix, lat_magnitude, field_prefix);
    ok = ok && append_option(cursor, remaining, "N", "North", lat_hemi_text);
    ok = ok && append_option(cursor, remaining, "S", "South", lat_hemi_text);
    ok = ok && append(cursor, remaining, "</select></div></div>");

    ok = ok && append(cursor, remaining,
                      "<div class=\"row\"><div><label>%s longitude</label><input name=\"%s_lon\" "
                      "type=\"number\" step=\"0.0001\" min=\"0\" max=\"180\" value=\"%.4f\">"
                      "</div><div><label>E/W</label><select name=\"%s_lon_hemi\">",
                      label_prefix, field_prefix, lon_magnitude, field_prefix);
    ok = ok && append_option(cursor, remaining, "E", "East", lon_hemi_text);
    ok = ok && append_option(cursor, remaining, "W", "West", lon_hemi_text);
    ok = ok && append(cursor, remaining, "</select></div></div>");
    return ok;
}

/// @brief Identifies which of the CCU's web pages is currently being rendered.
enum class ConfigWebPage : uint8_t
{
    Config,
    PinterActivity,
    Preview,
};

/// @brief Emits a consistent top navigation bar linking between the CCU's web pages.
/// @details Only links to the other pages, not back to the current one, so the
/// same three pages stay reachable from every page without an inconsistent mix
/// of nav placement/styling per page (previously each page reinvented its own
/// subset of cross-links).
bool append_page_nav(char*& cursor, size_t& remaining, ConfigWebPage current_page)
{
    bool ok = append(cursor, remaining, "<nav class=\"tools\">");
    if (current_page != ConfigWebPage::Config)
    {
        ok = ok && append(cursor, remaining, "<a class=\"ghost\" href=\"/config\">Config</a>");
    }
    if (current_page != ConfigWebPage::PinterActivity)
    {
        ok = ok &&
             append(cursor, remaining, "<a class=\"ghost\" href=\"/pinter\">Pinter Activity</a>");
    }
    if (current_page != ConfigWebPage::Preview)
    {
        ok = ok &&
             append(cursor, remaining, "<a class=\"ghost\" href=\"/preview\">Display Preview</a>");
    }
    return ok && append(cursor, remaining, "</nav>");
}

/// @brief Builds the professional single-page configuration UI.
void build_config_page(const char* message)
{
    const RuntimeConfig& cfg = config_manager::settings();
    char* cursor = g_response;
    size_t remaining = sizeof(g_response);
    std::array<char, 64> device_name = {};
    std::array<char, 64> device_label = {};
    std::array<char, 64> location = {};
    std::array<char, 64> room = {};
    std::array<char, 80> wifi_ssid = {};
    std::array<char, 96> ha_host = {};
    std::array<char, 96> ha_entity = {};
    std::array<char, 96> ha_self = {};
    std::array<char, 96> weather_entity = {};
    std::array<char, 96> sun_entity = {};
    std::array<char, 96> mqtt_host = {};
    std::array<char, 96> mqtt_user = {};
    std::array<char, 64> mqtt_prefix = {};
    std::array<char, 96> mqtt_topic = {};
    std::array<char, 96> air_traffic_host = {};
    std::array<char, 160> escaped_message = {};

    html_escape(cfg.device_name.data(), device_name.data(), device_name.size());
    html_escape(cfg.device_label.data(), device_label.data(), device_label.size());
    html_escape(cfg.location.data(), location.data(), location.size());
    html_escape(cfg.room.data(), room.data(), room.size());
    html_escape(cfg.wifi_ssid.data(), wifi_ssid.data(), wifi_ssid.size());
    html_escape(cfg.home_assistant_host.data(), ha_host.data(), ha_host.size());
    html_escape(cfg.home_assistant_entity_id.data(), ha_entity.data(), ha_entity.size());
    html_escape(cfg.home_assistant_self_entity_id.data(), ha_self.data(), ha_self.size());
    html_escape(cfg.weather_entity_id.data(), weather_entity.data(), weather_entity.size());
    html_escape(cfg.sun_entity_id.data(), sun_entity.data(), sun_entity.size());
    html_escape(cfg.mqtt_host.data(), mqtt_host.data(), mqtt_host.size());
    html_escape(cfg.mqtt_username.data(), mqtt_user.data(), mqtt_user.size());
    html_escape(cfg.mqtt_discovery_prefix.data(), mqtt_prefix.data(), mqtt_prefix.size());
    html_escape(cfg.mqtt_base_topic.data(), mqtt_topic.data(), mqtt_topic.size());
    html_escape(cfg.air_traffic_host.data(), air_traffic_host.data(), air_traffic_host.size());
    html_escape(message, escaped_message.data(), escaped_message.size());

    (void)append(
        cursor, remaining,
        "%s"
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,"
        "initial-scale=1\"><title>Merlin CCU</title><style>"
        ":root{color-scheme:dark;--bg:#08110f;--panel:#111f1b;--panel2:#162a24;"
        "--line:#2b4a41;--text:#eefbf3;--muted:#8eb5a7;--accent:#b7ff57}"
        "*{box-sizing:border-box}body{margin:0;background:"
        "radial-gradient(circle at 20%% 0,#193c31 0,#08110f 42%%,#050807 100%%);"
        "font:15px/1.45 'Segoe UI',Tahoma,sans-serif;color:var(--text)}"
        ".wrap{max-width:1080px;margin:0 auto;padding:32px 18px 48px}.hero{display:flex;"
        "justify-content:space-between;gap:18px;align-items:flex-end;margin-bottom:22px}"
        ".hero-tools{display:flex;flex-direction:column;gap:10px;align-items:flex-end}"
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
        "fieldset{border:0;margin:0;padding:0}fieldset.disabled{opacity:.38}"
        "fieldset.disabled input,fieldset.disabled select{cursor:not-allowed;background:#101916;"
        "color:#6f8a80;border-color:#20362f}"
        ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.check{display:flex;"
        "gap:9px;align-items:center;margin-top:12px;color:var(--muted)}.check input{"
        "width:auto}.actions{position:sticky;bottom:0;margin-top:18px;background:"
        "linear-gradient(180deg,#08110f00,#08110fee 26%%,#08110f);padding-top:18px;"
        "display:flex;justify-content:space-between;gap:12px;align-items:center}"
        ".tools{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
        ".ghost{border:1px solid var(--line);border-radius:999px;padding:11px 16px;"
        "color:var(--text);text-decoration:none;background:#0d1815;font-weight:700;"
        "letter-spacing:.04em;font-size:12px;text-transform:uppercase}"
        "button{border:0;border-radius:999px;background:var(--accent);color:#09110d;"
        "font-weight:800;padding:12px 20px;letter-spacing:.05em}.msg{font-weight:700}"
        ".msg.ok{color:var(--accent)}.msg.error{color:#ff6b6b}"
        ".hint{font-size:12px;color:var(--muted);margin-top:6px}"
        "@media(max-width:760px){.grid,.row{grid-template-columns:1fr}.hero{display:block}"
        ".actions{align-items:flex-start;flex-direction:column}"
        "h1{font-size:28px}}</style></head><body><main class=\"wrap\"><div class=\"hero\">"
        "<div><div class=\"tag\">Merlin CCU Control Surface</div><h1>%s</h1><p class=\"sub\">"
        "Configure this CCU from your local network. Settings are saved in reserved onboard "
        "flash sectors, not RAM, and loaded at boot. Display settings apply immediately; "
        "Wi-Fi, Home Assistant and MQTT changes take effect after reboot. A full flash erase "
        "or firmware image that overwrites the reserved area will remove saved settings.</p></div>"
        "<div class=\"hero-tools\">",
        kHttpOkHeader, device_label[0] ? device_label.data() : device_name.data());
    (void)append_page_nav(cursor, remaining, ConfigWebPage::Config);
    (void)append(cursor, remaining,
                "<div class=\"pill\">Local intranet configuration</div></div></div>");

    if (escaped_message[0] != '\0')
    {
        const bool is_error = std::strncmp(message, "Configuration not saved", 23) == 0;
        (void)append(cursor, remaining, "<p class=\"msg %s\">%s</p>", is_error ? "error" : "ok",
                    escaped_message.data());
    }

    (void)append(
        cursor, remaining,
        "<form method=\"post\" action=\"/config\"><input type=\"hidden\" "
        "name=\"config_form_marker\" value=\"1\"><div class=\"grid\"><section class=\"card\">"
        "<h2>Device Identity</h2><label>Device name</label><input name=\"device_name\" "
        "maxlength=\"31\" required pattern=\"[A-Za-z0-9_-]+\" value=\"%s\"><label>"
        "Display label</label><input name=\"device_label\" "
        "maxlength=\"31\" value=\"%s\"><div class=\"row\"><div><label>Location</label><input "
        "name=\"location\" maxlength=\"31\" value=\"%s\"></div><div><label>Room</label><input "
        "name=\"room\" maxlength=\"31\" value=\"%s\"></div></div><p class=\"hint\">Use names like "
        "CCU1, CCU2, GroundFloorCCU, KitchenCCU.</p></section>",
        device_name.data(), device_label.data(), location.data(), room.data());

    (void)append(
        cursor, remaining,
        "<section class=\"card\"><h2>Remote Access</h2><input type=\"hidden\" "
        "name=\"remote_config_present\" value=\"1\"><label class=\"check\"><input "
        "type=\"checkbox\" name=\"remote_config\" %s>Enable local web configuration "
        "server</label><p class=\"hint\">Disable this if you only want setup changes to "
        "come from the front panel.</p></section>",
        cfg.remote_config_enabled ? "checked" : "");

    (void)append(
        cursor, remaining,
        "<section class=\"card\"><h2>Network</h2><label>Wi-Fi SSID</label><input "
        "name=\"wifi_ssid\" maxlength=\"32\" value=\"%s\"><label>Wi-Fi password</label>"
        "<input name=\"wifi_password\" type=\"password\" placeholder=\"Leave blank to keep "
        "current\">"
        "<p class=\"hint\">The web server starts once the CCU joins Wi-Fi. Changing Wi-Fi requires "
        "a reboot.</p></section>",
        wifi_ssid.data());

    (void)append(
        cursor, remaining,
        "<section class=\"card\"><h2>Home Assistant</h2><input type=\"hidden\" "
        "name=\"ha_enabled_present\" value=\"1\"><label class=\"check\"><input "
        "type=\"checkbox\" name=\"ha_enabled\" data-controls=\"ha_fields\" %s>Enable "
        "REST integration</label><fieldset id=\"ha_fields\" class=\"%s\"><label>Host</label>"
        "<input name=\"ha_host\" maxlength=\"63\" value=\"%s\"><label>Port</label>"
        "<input name=\"ha_port\" type=\"number\" min=\"1\" max=\"65535\" "
        "inputmode=\"numeric\" value=\"%u\"><label>Access token</label><input "
        "name=\"ha_token\" type=\"password\" placeholder=\"Leave blank to keep current\">"
        "<label>Tracked entity</label><input name=\"ha_entity\" maxlength=\"63\" "
        "value=\"%s\"><label>Self entity</label><input name=\"ha_self\" maxlength=\"63\" "
        "value=\"%s\"></fieldset></section>",
        cfg.home_assistant_enabled ? "checked" : "", cfg.home_assistant_enabled ? "" : "disabled",
        ha_host.data(), static_cast<unsigned>(cfg.home_assistant_port), ha_entity.data(),
        ha_self.data());

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>MQTT Discovery</h2><input type=\"hidden\" "
                 "name=\"mqtt_enabled_present\" value=\"1\"><label class=\"check\"><input "
                 "type=\"checkbox\" name=\"mqtt_enabled\" data-controls=\"mqtt_fields\" %s>Enable "
                 "MQTT</label><fieldset id=\"mqtt_fields\" class=\"%s\"><label>Broker host</label>"
                 "<input name=\"mqtt_host\" maxlength=\"63\" value=\"%s\"><div "
                 "class=\"row\"><div><label>Port</label>"
                 "<input name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" "
                 "inputmode=\"numeric\" value=\"%u\"></div><div><label>Username</label>"
                 "<input name=\"mqtt_username\" maxlength=\"63\" "
                 "value=\"%s\"></div></div><label>Password</label>"
                 "<input name=\"mqtt_password\" type=\"password\" placeholder=\"Leave blank to "
                 "keep current\">"
                 "<div class=\"row\"><div><label>Discovery prefix</label><input "
                 "name=\"mqtt_prefix\" maxlength=\"31\" "
                 "value=\"%s\"></div><div><label>Base topic</label><input name=\"mqtt_topic\" "
                 "maxlength=\"63\" "
                 "value=\"%s\"></div></div></fieldset></section>",
                 cfg.mqtt_enabled ? "checked" : "", cfg.mqtt_enabled ? "" : "disabled",
                 mqtt_host.data(), static_cast<unsigned>(cfg.mqtt_port), mqtt_user.data(),
                 mqtt_prefix.data(), mqtt_topic.data());

    (void)append(
        cursor, remaining,
        "<section class=\"card\"><h2>Local Air Traffic</h2><input type=\"hidden\" "
        "name=\"air_traffic_enabled_present\" value=\"1\"><label class=\"check\"><input "
        "type=\"checkbox\" name=\"air_traffic_enabled\" data-controls=\"air_traffic_fields\" "
        "%s>Enable Local Traffic</label><fieldset id=\"air_traffic_fields\" class=\"%s\">"
        "<label>Provider host</label><input name=\"air_traffic_host\" maxlength=\"63\" "
        "value=\"%s\"><div class=\"row\"><div><label>Port</label><input name=\"air_traffic_port\" "
        "type=\"number\" min=\"1\" max=\"65535\" inputmode=\"numeric\" value=\"%u\"></div>"
        "<div><label>Search radius (nm)</label><input name=\"air_traffic_radius\" type=\"number\" "
        "min=\"1\" max=\"250\" inputmode=\"numeric\" value=\"%u\"></div></div>",
        cfg.air_traffic_enabled ? "checked" : "", cfg.air_traffic_enabled ? "" : "disabled",
        air_traffic_host.data(), static_cast<unsigned>(cfg.air_traffic_port),
        static_cast<unsigned>(cfg.air_traffic_radius_nm));
    (void)append_coordinate_fields(cursor, remaining, "air_traffic", "Home",
                                   cfg.air_traffic_coordinates.data());
    (void)append(cursor, remaining,
                 "<p class=\"hint\">Used as the search centre -- see N/S/E/W above.</p>"
                 "<label>API key</label><input name=\"air_traffic_api_key\" type=\"password\" "
                 "placeholder=\"Leave blank to keep current\"><p class=\"hint\">Not required for "
                 "adsb.lol today; only needed if a provider starts gating access.</p>"
                 "</fieldset></section>");

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>Weather Source</h2><label>Weather source</label>"
                 "<select name=\"weather_source\" id=\"weather_source_select\">");
    const char* selected_weather = weather_token(cfg.weather_source);
    (void)append_option(cursor, remaining, "home_assistant", "Home Assistant", selected_weather);
    (void)append_option(cursor, remaining, "open_meteo", "Open-Meteo", selected_weather);
    const bool weather_is_home_assistant = cfg.weather_source == WeatherSource::HomeAssistant;
    (void)append(
        cursor, remaining,
        "</select><fieldset id=\"weather_ha_fields\" class=\"%s\"><div class=\"row\"><div><label>"
        "Home Assistant weather entity</label><input name=\"weather_entity\" maxlength=\"63\" "
        "value=\"%s\"></div><div><label>Home Assistant sun entity</label><input "
        "name=\"sun_entity\" maxlength=\"63\" value=\"%s\"></div></div></fieldset>"
        "<fieldset id=\"weather_direct_fields\" class=\"%s\">",
        weather_is_home_assistant ? "" : "disabled", weather_entity.data(), sun_entity.data(),
        weather_is_home_assistant ? "disabled" : "");
    (void)append_coordinate_fields(cursor, remaining, "weather", "Direct weather",
                                   cfg.weather_coordinates.data());
    (void)append(cursor, remaining,
                 "<p class=\"hint\">Used by direct weather sources, independently of Home "
                 "Assistant.</p></fieldset></section>");

    (void)append(cursor, remaining,
                 "<section class=\"card\"><h2>Display & Time</h2><label>Time zone</label><select "
                 "name=\"time_zone\">");
    const char* selected_zone = time_zone_token(cfg.time_zone);
    (void)append_option(cursor, remaining, "atlantic", "Atlantic Standard Time", selected_zone);
    (void)append_option(cursor, remaining, "argentina", "Argentina Time", selected_zone);
    (void)append_option(cursor, remaining, "south_georgia", "South Georgia Time", selected_zone);
    (void)append_option(cursor, remaining, "azores", "Azores Time", selected_zone);
    (void)append_option(cursor, remaining, "london", "Europe/London", selected_zone);
    (void)append_option(cursor, remaining, "central_european", "Central European Time",
                        selected_zone);
    (void)append_option(cursor, remaining, "eastern_european", "Eastern European Time",
                        selected_zone);
    (void)append_option(cursor, remaining, "arabia", "Arabia Standard Time", selected_zone);
    (void)append_option(cursor, remaining, "gulf", "Gulf Standard Time", selected_zone);
    (void)append(cursor, remaining,
                 "</select></section><section class=\"card\"><h2>Screen Saver</h2><label>Screen "
                 "saver</label><select name=\"screen_saver\">");
    const char* selected_saver = saver_token(cfg.screen_saver);
    (void)append_option(cursor, remaining, "life", "Life", selected_saver);
    (void)append_option(cursor, remaining, "clock", "Clock", selected_saver);
    (void)append_option(cursor, remaining, "starfield", "Starfield", selected_saver);
    (void)append_option(cursor, remaining, "matrix", "Matrix", selected_saver);
    (void)append_option(cursor, remaining, "radar", "Radar", selected_saver);
    (void)append_option(cursor, remaining, "rain", "Rain", selected_saver);
    (void)append_option(cursor, remaining, "worms", "Worms", selected_saver);
    (void)append_option(cursor, remaining, "random", "Random", selected_saver);
    (void)append(
        cursor, remaining,
        "</select><label>Screen-saver timeout minutes</label><input name=\"screen_timeout\" "
        "type=\"number\" min=\"0\" max=\"120\" inputmode=\"numeric\" value=\"%u\">"
        "<p class=\"hint\">0 disables timeout. Valid range: 0-120."
        "</p></section></div><div class=\"actions\"><span class=\"hint\">Configuration is saved "
        "locally on this CCU.</span>"
        "<button type=\"submit\">Save Configuration</button></div></form>"
        "<script>"
        "function byId(id){return document.getElementById(id)}"
        "function setDisabled(fs,off){if(!fs)return;var els=fs.querySelectorAll('input,select');"
        "fs.className=off?'disabled':'';for(var i=0;i<els.length;i++){els[i].disabled=off}}"
        "function syncCheck(box){setDisabled(byId(box.getAttribute('data-controls')),!box.checked)}"
        "var toggles=document.querySelectorAll('input[data-controls]');"
        "for(var i=0;i<toggles.length;i++){(function(box){box.onchange=function(){syncCheck(box)};"
        "syncCheck(box)})(toggles[i])}"
        "var weather=byId('weather_source_select'),weatherHaFields=byId('weather_ha_fields'),"
        "weatherDirectFields=byId('weather_direct_fields');"
        "function syncWeather(){var isHa=weather.value==='home_assistant';"
        "setDisabled(weatherHaFields,!isHa);setDisabled(weatherDirectFields,isHa)}"
        "if(weather){weather.onchange=syncWeather;syncWeather()}"
        "var preview=document.querySelector('nav.tools a[href=\"/preview\"]');"
        "if(preview){preview.addEventListener('click',function(event){"
        "if(event.ctrlKey||event.metaKey||event.shiftKey||event.altKey){return;}"
        "event.preventDefault();"
        "var popup=window.open('/preview?popup=1','merlin_preview_window',"
        "'popup=yes,width=900,height=1550,resizable=yes,scrollbars=yes');"
        "if(!popup){window.location.href='/preview';}"
        "});}"
        "</script></main></body></html>",
        static_cast<unsigned>(cfg.screen_saver_timeout_minutes));
}

enum class PinterTimelineStage : uint8_t
{
    Brew,
    Crash,
    Condition,
    Ready,
};

/// @brief Returns a web-facing lifecycle label for one manually tracked Pinter.
const char* pinter_state_text(PinterState state)
{
    switch (state)
    {
    case PinterState::Idle:
        return "Idle";
    case PinterState::Brewing:
        return "Brewing";
    case PinterState::ColdCrash:
        return "Cold crash";
    case PinterState::Conditioning:
        return "Conditioning";
    case PinterState::Ready:
        return "Ready";
    case PinterState::Consumed:
        return "Consumed";
    }

    return "Unknown";
}

/// @brief Chooses the visual state for a planned Pinter timeline segment.
const char* pinter_stage_tone(PinterState state, PinterTimelineStage stage)
{
    switch (state)
    {
    case PinterState::Idle:
        return "future";
    case PinterState::Brewing:
        return stage == PinterTimelineStage::Brew ? "current" : "future";
    case PinterState::ColdCrash:
        if (stage == PinterTimelineStage::Brew)
        {
            return "done";
        }
        return stage == PinterTimelineStage::Crash ? "current" : "future";
    case PinterState::Conditioning:
        if (stage == PinterTimelineStage::Brew || stage == PinterTimelineStage::Crash)
        {
            return "done";
        }
        return stage == PinterTimelineStage::Condition ? "current" : "future";
    case PinterState::Ready:
        return stage == PinterTimelineStage::Ready ? "current" : "done";
    case PinterState::Consumed:
        return "done";
    }

    return "future";
}

/// @brief Formats an epoch day for compact timeline axis labels.
void format_epoch_day_label(uint32_t epoch_day, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return;
    }

    if (epoch_day == 0U)
    {
        std::snprintf(out, out_size, "-");
        return;
    }

    const date_time_math::DateTimeParts parts = date_time_math::civil_from_epoch_day(epoch_day);
    const char* month = (parts.month >= 1 && parts.month <= 12)
                            ? date_time_math::kMonthAbbreviations[static_cast<size_t>(parts.month - 1)]
                            : "---";
    std::snprintf(out, out_size, "%02d %s", parts.day, month);
}

/// @brief Returns the active start day, or zero when a Pinter has no dated activity.
uint32_t pinter_activity_start_day(const PinterStatus& pinter, uint32_t today)
{
    if (pinter.brew_start_day != 0U)
    {
        return pinter.brew_start_day;
    }

    if (pinter.state == PinterState::Idle)
    {
        return 0U;
    }

    return today != 0U ? today : 1U;
}

/// @brief Appends one proportional bar segment to a Pinter timeline row.
bool append_pinter_stage(char*& cursor, size_t& remaining, const char* stage_class,
                         const char* tone_class, const char* label, uint32_t start_day,
                         uint32_t duration_days, uint32_t window_start_day,
                         unsigned timeline_days)
{
    if (duration_days == 0U || timeline_days == 0U)
    {
        return true;
    }

    const uint32_t window_end_day = window_start_day + timeline_days;
    const uint32_t stage_start_day = start_day;
    const uint32_t stage_end_day = stage_start_day + duration_days;
    if (stage_end_day <= window_start_day || stage_start_day >= window_end_day)
    {
        return true;
    }

    const uint32_t visible_start_day = std::max(stage_start_day, window_start_day);
    const uint32_t visible_end_day = std::min(stage_end_day, window_end_day);
    const double left_percent = (static_cast<double>(visible_start_day - window_start_day) * 100.0) /
                                static_cast<double>(timeline_days);
    const double width_percent = (static_cast<double>(visible_end_day - visible_start_day) * 100.0) /
                                 static_cast<double>(timeline_days);
    return append(cursor, remaining,
                  "<div class=\"stage %s %s\" style=\"left:%.2f%%;width:%.2f%%\"><span>%s</span>"
                  "</div>",
                  stage_class, tone_class, left_percent, width_percent, label);
}

/// @brief Appends one Pinter activity row using real event dates where available.
bool append_pinter_activity_row(char*& cursor, size_t& remaining, const PinterStatus& pinter,
                                uint32_t window_start_day, unsigned timeline_days,
                                uint32_t today_day)
{
    constexpr unsigned kReadyHoldDisplayDays = 2U;
    std::array<char, 24> label = {};
    std::array<char, 96> brew_name = {};
    std::array<char, 24> brew_label = {};
    std::array<char, 16> crash_label = {};
    std::array<char, 24> condition_label = {};
    std::array<char, 16> ready_label = {};
    std::array<char, 24> timing_text = {};
    std::array<char, 48> start_text = {};
    std::array<char, 16> start_date = {};

    const console_controller::PinterBrewTiming& timing =
        console_controller::pinter_brew_timing(pinter.brew_index);
    const unsigned brew_days = pinter.planned_brewing_days != 0U
                                   ? pinter.planned_brewing_days
                                   : timing.recommended_brewing_days;
    const unsigned crash_days = pinter.planned_cold_crash_days;
    const unsigned conditioning_days = pinter.planned_conditioning_days != 0U
                                           ? pinter.planned_conditioning_days
                                           : timing.recommended_conditioning_days;
    html_escape(pinter.label.data(), label.data(), label.size());
    html_escape(timing.name, brew_name.data(), brew_name.size());
    std::snprintf(brew_label.data(), brew_label.size(), "Brew %ud", brew_days);
    std::snprintf(crash_label.data(), crash_label.size(), "Crash %ud", crash_days);
    std::snprintf(condition_label.data(), condition_label.size(), "Condition %ud",
                  conditioning_days);
    std::snprintf(ready_label.data(), ready_label.size(), "Ready");
    std::snprintf(timing_text.data(), timing_text.size(), "Plan %ud",
                  static_cast<unsigned>(brew_days + crash_days + conditioning_days));
    const uint32_t activity_start_day = pinter_activity_start_day(pinter, today_day);
    if (activity_start_day != 0U)
    {
        if (today_day != 0U || pinter.brew_start_day != 0U)
        {
            format_epoch_day_label(activity_start_day, start_date.data(), start_date.size());
            std::snprintf(start_text.data(), start_text.size(), "Start %s, %s", start_date.data(),
                          timing_text.data());
        }
        else
        {
            std::snprintf(start_text.data(), start_text.size(), "Undated, %s", timing_text.data());
        }
    }
    else
    {
        std::snprintf(start_text.data(), start_text.size(), "%s", timing_text.data());
    }

    bool ok = append(cursor, remaining,
                     "<section class=\"pinter-row\"><div class=\"pinter-name\"><strong>%s</strong>"
                     "<span>%s</span></div><div class=\"track\">",
                     label.data(), brew_name.data());
    if (!ok)
    {
        return false;
    }

    const bool today_visible =
        today_day != 0U && today_day >= window_start_day &&
        today_day <= window_start_day + static_cast<uint32_t>(timeline_days);
    if (today_visible)
    {
        const double today_percent =
            (static_cast<double>(today_day - window_start_day) * 100.0) /
            static_cast<double>(timeline_days);
        ok = append(cursor, remaining,
                    "<div class=\"today-line\" style=\"left:%.2f%%\" aria-hidden=\"true\"></div>",
                    today_percent);
    }

    if (pinter.state == PinterState::Idle || activity_start_day == 0U)
    {
        ok = ok && append(cursor, remaining,
                          "<div class=\"empty-track\">No active brew on this Pinter</div>");
    }
    else
    {
        uint32_t start_day = activity_start_day;
        ok = append_pinter_stage(cursor, remaining, "brew",
                                 pinter_stage_tone(pinter.state, PinterTimelineStage::Brew),
                                 brew_label.data(), start_day, brew_days, window_start_day,
                                 timeline_days);
        start_day += brew_days;

        if (crash_days > 0U)
        {
            if (pinter.cold_crash_start_day != 0U)
            {
                start_day = pinter.cold_crash_start_day;
            }
            ok = ok && append_pinter_stage(cursor, remaining, "crash",
                                           pinter_stage_tone(pinter.state,
                                                             PinterTimelineStage::Crash),
                                           crash_label.data(), start_day, crash_days,
                                           window_start_day, timeline_days);
            start_day += crash_days;
        }

        if (pinter.conditioning_start_day != 0U)
        {
            start_day = pinter.conditioning_start_day;
        }
        ok = ok && append_pinter_stage(cursor, remaining, "condition",
                                       pinter_stage_tone(pinter.state,
                                                         PinterTimelineStage::Condition),
                                       condition_label.data(), start_day, conditioning_days,
                                       window_start_day,
                                       timeline_days);
        start_day += conditioning_days;

        if (pinter.ready_day != 0U)
        {
            start_day = pinter.ready_day;
        }
        ok = ok && append_pinter_stage(cursor, remaining, "ready",
                                       pinter_stage_tone(pinter.state,
                                                         PinterTimelineStage::Ready),
                                       ready_label.data(), start_day, kReadyHoldDisplayDays,
                                       window_start_day,
                                       timeline_days);
    }

    ok = ok && append(cursor, remaining,
                      "</div><div class=\"pinter-status\"><strong>%s</strong><span>%s</span>"
                      "</div></section>",
                      pinter_state_text(pinter.state), start_text.data());
    return ok;
}

/// @brief Builds the Pinter activity timeline page.
bool build_pinter_activity_page()
{
    constexpr unsigned kTimelineDays = 28U;
    constexpr unsigned kDefaultPastContextDays = 7U;
    const ConsoleState& state = console_controller::state();
    char* cursor = g_response;
    size_t remaining = sizeof(g_response);
    const uint32_t today_day =
        state.time_status.synced ? state.time_status.local_epoch_day : 0U;
    uint32_t earliest_start_day = 0U;
    bool real_start_seen = false;

    unsigned brewing = 0U;
    unsigned conditioning = 0U;
    unsigned ready = 0U;
    for (const PinterStatus& pinter : state.pinters)
    {
        const uint32_t start_day = pinter_activity_start_day(pinter, today_day);
        if (start_day != 0U && (earliest_start_day == 0U || start_day < earliest_start_day))
        {
            earliest_start_day = start_day;
        }
        real_start_seen = real_start_seen || pinter.brew_start_day != 0U;

        switch (pinter.state)
        {
        case PinterState::Brewing:
        case PinterState::ColdCrash:
            ++brewing;
            break;
        case PinterState::Conditioning:
            ++conditioning;
            break;
        case PinterState::Ready:
            ++ready;
            break;
        case PinterState::Idle:
        case PinterState::Consumed:
            break;
        }
    }

    uint32_t window_start_day = today_day != 0U ? today_day : earliest_start_day;
    if (earliest_start_day != 0U)
    {
        window_start_day =
            today_day != 0U ? std::min(earliest_start_day, today_day) : earliest_start_day;
    }
    if (today_day != 0U && window_start_day + kTimelineDays < today_day)
    {
        window_start_day = today_day > kDefaultPastContextDays ? today_day - kDefaultPastContextDays
                                                               : today_day;
    }
    if (window_start_day == 0U)
    {
        window_start_day = 1U;
    }

    std::array<char, 16> axis_0 = {};
    std::array<char, 16> axis_7 = {};
    std::array<char, 16> axis_14 = {};
    std::array<char, 16> axis_21 = {};
    std::array<char, 16> axis_28 = {};
    std::array<char, 32> today_label = {};
    const bool has_real_dates = today_day != 0U || real_start_seen;
    if (has_real_dates)
    {
        format_epoch_day_label(window_start_day, axis_0.data(), axis_0.size());
        format_epoch_day_label(window_start_day + 7U, axis_7.data(), axis_7.size());
        format_epoch_day_label(window_start_day + 14U, axis_14.data(), axis_14.size());
        format_epoch_day_label(window_start_day + 21U, axis_21.data(), axis_21.size());
        format_epoch_day_label(window_start_day + 28U, axis_28.data(), axis_28.size());
    }
    else
    {
        std::snprintf(axis_0.data(), axis_0.size(), "0d");
        std::snprintf(axis_7.data(), axis_7.size(), "7d");
        std::snprintf(axis_14.data(), axis_14.size(), "14d");
        std::snprintf(axis_21.data(), axis_21.size(), "21d");
        std::snprintf(axis_28.data(), axis_28.size(), "28d");
    }
    if (today_day != 0U)
    {
        std::array<char, 16> today_date = {};
        format_epoch_day_label(today_day, today_date.data(), today_date.size());
        std::snprintf(today_label.data(), today_label.size(), "Today %s", today_date.data());
    }
    else
    {
        std::snprintf(today_label.data(), today_label.size(), "Waiting for network time");
    }

    const bool today_visible =
        today_day != 0U && today_day >= window_start_day &&
        today_day <= window_start_day + static_cast<uint32_t>(kTimelineDays);
    const double today_percent =
        today_visible ? (static_cast<double>(today_day - window_start_day) * 100.0) /
                            static_cast<double>(kTimelineDays)
                      : 0.0;

    bool ok = append(
        cursor, remaining,
        "%s"
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,"
        "initial-scale=1\"><link rel=\"icon\" href=\"data:,\"><title>Merlin CCU Pinter"
        " Activity</title><style>"
        ":root{color-scheme:dark;--bg:#07100d;--panel:#111e1a;--line:#2d493f;"
        "--text:#eefbf3;--muted:#92b8aa;--accent:#b7ff57;--warn:#ffcf5a;"
        "--blue:#77b7ff;--ice:#96e8ff;--amber:#ffd05a;--green:#77e8a3}"
        "*{box-sizing:border-box}body{margin:0;background:linear-gradient(180deg,#10241d,#07100d "
        "54%%,#040706);font:14px/1.45 'Segoe UI',Tahoma,sans-serif;color:var(--text)}"
        ".wrap{max-width:1120px;margin:0 auto;padding:28px 18px 44px}.top{display:flex;"
        "justify-content:space-between;gap:16px;align-items:flex-end;margin-bottom:18px}"
        "h1{margin:0;font-size:30px;letter-spacing:.03em}.sub{color:var(--muted);max-width:680px}"
        ".tools{display:flex;gap:10px;flex-wrap:wrap}.ghost{border:1px solid var(--line);"
        "border-radius:999px;padding:9px 13px;color:var(--text);text-decoration:none;"
        "background:#0b1512;font-weight:700;font-size:12px;text-transform:uppercase;"
        "letter-spacing:.05em}"
        ".summary{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));"
        "gap:10px;margin:0 0 16px}.tile{border:1px solid var(--line);border-radius:8px;"
        "background:#0d1815;padding:12px}.tile strong{display:block;font-size:22px;color:"
        "var(--accent)}.tile span{color:var(--muted);text-transform:uppercase;font-size:11px;"
        "letter-spacing:.1em}.timeline{border:1px solid var(--line);border-radius:10px;"
        "background:linear-gradient(180deg,var(--panel),#0a1310);overflow:hidden}.axis,"
        ".pinter-row{display:grid;grid-template-columns:150px minmax(520px,1fr) 120px;gap:14px;"
        "align-items:center}.axis{padding:14px 16px 6px;color:var(--muted);font-size:12px}"
        ".scale{position:relative;height:30px;border-bottom:1px solid #355b4e}.scale span{"
        "position:absolute;bottom:4px;transform:translateX(-50%%);white-space:nowrap}"
        ".scale .today-label{top:0;bottom:auto;color:var(--accent);font-weight:800}.pinter-row{"
        "padding:14px 16px;"
        "border-top:1px solid #20392f}.pinter-name strong,.pinter-status strong{display:block;"
        "font-size:15px}.pinter-name span,.pinter-status span{display:block;color:var(--muted);"
        "font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.track{"
        "position:relative;height:58px;border:1px solid #29473d;border-radius:8px;background:"
        "repeating-linear-gradient(90deg,#13231e 0,#13231e 1px,transparent 1px,transparent "
        "25%%),linear-gradient(180deg,#08120f,#0d1915)}.empty-track{position:absolute;inset:0;"
        "display:flex;align-items:center;padding:0 14px;color:#6f8f83}.stage{position:absolute;"
        "top:10px;height:36px;border-radius:6px;display:flex;align-items:center;min-width:34px;"
        "z-index:1;"
        "box-shadow:inset 0 1px 0 #ffffff40}.stage span{padding:0 8px;white-space:nowrap;"
        "overflow:hidden;text-overflow:ellipsis;font-size:12px;font-weight:800;color:#07100d}"
        ".today-line{position:absolute;top:0;bottom:0;width:2px;background:var(--accent);"
        "box-shadow:0 0 12px #b7ff57aa;z-index:3}"
        ".brew{background:var(--blue)}.crash{background:var(--ice)}.condition{background:"
        "var(--amber)}.ready{background:var(--green)}.future{opacity:.32}.done{opacity:.72}"
        ".current{opacity:1;outline:2px solid #eefbf380;box-shadow:0 0 18px #b7ff5740,"
        "inset 0 1px 0 #ffffff55}.legend{display:flex;gap:10px;flex-wrap:wrap;color:"
        "var(--muted);font-size:12px;margin-top:12px}.swatch{display:inline-block;width:12px;"
        "height:12px;border-radius:3px;margin-right:5px;vertical-align:-2px}.note{margin-top:14px;"
        "color:var(--muted);font-size:12px}@media(max-width:760px){.top{display:block}.summary{"
        "grid-template-columns:repeat(2,minmax(0,1fr))}.timeline{overflow-x:auto}.axis,"
        ".pinter-row{grid-template-columns:130px 620px 100px}}</style></head><body><main "
        "class=\"wrap\"><div class=\"top\"><div><h1>Pinter Activity</h1><p class=\"sub\">"
        "Each Pinter has its own row. Time runs left to right using real dates; today's date is "
        "shown by the vertical green indicator.</p></div>",
        kHttpOkHeader);
    ok = ok && append_page_nav(cursor, remaining, ConfigWebPage::PinterActivity);
    ok = ok && append(
                   cursor, remaining,
                   "</div><section class=\"summary\"><div class=\"tile\"><strong>%u</strong><span>"
                   "Brewing</span></div><div "
                   "class=\"tile\"><strong>%u</strong><span>Conditioning</span></div><div class=\"tile\">"
                   "<strong>%u</strong><span>Ready</span></div></section><section class=\"timeline\">"
                   "<div class=\"axis\"><div></div><div class=\"scale\"><span style=\"left:0%%\">%s</span>"
                   "<span style=\"left:25%%\">%s</span><span style=\"left:50%%\">%s</span><span "
                   "style=\"left:75%%\">%s</span><span style=\"left:100%%\">%s</span>",
                   brewing, conditioning,
                   ready, axis_0.data(), axis_7.data(), axis_14.data(), axis_21.data(),
                   axis_28.data());

    if (ok && today_visible)
    {
        ok = append(cursor, remaining,
                    "<span class=\"today-label\" style=\"left:%.2f%%\">%s</span>"
                    "<div class=\"today-line\" style=\"left:%.2f%%\" aria-hidden=\"true\"></div>",
                    today_percent, today_label.data(), today_percent);
    }
    ok = ok && append(cursor, remaining, "</div><div>%s</div></div>", today_label.data());

    for (const PinterStatus& pinter : state.pinters)
    {
        ok = ok && append_pinter_activity_row(cursor, remaining, pinter, window_start_day,
                                              kTimelineDays, today_day);
    }

    ok = ok && append(
                   cursor, remaining,
                   "</section><div class=\"legend\"><span><i class=\"swatch brew\"></i>Brew</span>"
                   "<span><i class=\"swatch crash\"></i>Cold crash</span><span><i class=\"swatch "
                   "condition\"></i>Condition</span><span><i class=\"swatch ready\"></i>Ready</span>"
                   "</div><p class=\"note\">Start, fridge, and ready dates are stamped from the "
                   "CCU clock when those Pinter actions are pressed. Rows without a start date use "
                   "today until the next real event is recorded.</p></main></body></html>");

    if (!ok)
    {
        std::snprintf(g_response, sizeof(g_response),
                      "HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n"
                      "Pinter activity page exceeded response buffer.\n");
        return false;
    }

    return true;
}

/// @brief Builds the live display preview page rendered in-browser from raw framebuffer data.
/// @returns True when the page fits in the response buffer.
bool build_preview_page()
{
    char* cursor = g_response;
    size_t remaining = sizeof(g_response);

    bool ok = append(
        cursor, remaining,
        "%s"
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,"
        "initial-scale=1\"><title>Merlin CCU Display Preview</title><style>"
        ":root{color-scheme:dark;--bg:#070b0a;--panel:#11211c;--line:#315348;--text:#eaf8ef;"
        "--muted:#8eb5a7;--accent:#b7ff57;--key-line:#5d6972;--key-face:#12181d;--key-top:#202831;"
        "--live:#ffe184;--rect-key-w:78px;--rect-key-h:56px;--font-small:12px;--font-large:18px}*{"
        "box-sizing:border-box}"
        "body{margin:0;background:radial-gradient(circle at 20%% -10%%,#27342f 0%%,#090e0d "
        "48%%,#050707 100%%);"
        "font:15px/1.45 'Trebuchet MS','Gill Sans MT','Segoe UI',sans-serif;color:var(--text)}"
        ".wrap{max-width:920px;margin:0 auto;padding:18px 14px 24px}"
        "h1{margin:0 0 6px;font-size:28px;letter-spacing:.03em;text-transform:uppercase}"
        "p{margin:0 0 12px;color:var(--muted)}"
        ".card{background:linear-gradient(180deg,var(--panel),#0b1613);border:1px solid "
        "var(--line);"
        "border-radius:14px;padding:12px;box-shadow:0 18px 42px #0007;overflow-x:auto}"
        ".meta{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:10px}"
        ".badge{border:1px solid var(--line);border-radius:999px;padding:7px "
        "11px;color:var(--accent);"
        "font-size:12px;letter-spacing:.06em;text-transform:uppercase}"
        "button,a{border:1px solid "
        "var(--line);background:#0a1411;color:var(--text);border-radius:10px;"
        "padding:8px 11px;text-decoration:none;cursor:pointer}"
        ".intro{display:flex;justify-content:space-between;gap:16px;align-items:flex-end;"
        "flex-wrap:wrap;margin-bottom:14px}"
        ".tools{display:flex;gap:10px;flex-wrap:wrap}"
        ".ghost{border:1px solid var(--line);border-radius:999px;padding:9px 13px;color:"
        "var(--text);text-decoration:none;background:#0a1411;font-weight:700;font-size:12px;"
        "text-transform:uppercase;letter-spacing:.05em}"
        ".state{font-size:12px;color:var(--muted);display:inline-flex;align-items:center;min-width:"
        "88px;white-space:nowrap}"
        ".ccu-fixed{width:560px;min-width:560px;max-width:560px;margin:0 auto}"
        ".ccu{position:relative;width:560px;padding:14px;border:1px solid "
        "#2f3336;border-radius:26px;"
        "background:linear-gradient(160deg,#2d3134 0%%,#121417 44%%,#0b0d0e 100%%);"
        "box-shadow:inset 0 1px 0 #59606766,0 14px 36px #0009}"
        ".ccu:before{content:'';position:absolute;inset:8px;border-radius:20px;border:1px solid "
        "#454d55;pointer-events:none;opacity:.75}"
        ".ccu:after{content:'';position:absolute;inset:0;border-radius:26px;pointer-events:none;"
        "box-shadow:inset 0 18px 26px #ffffff08,inset 0 -14px 22px #000a}"
        ".row{display:grid}"
        ".top-row{grid-template-columns:repeat(6,var(--rect-key-w));gap:8px;justify-content:center}"
        ".top-gap{border:1px solid #2e3438;border-radius:12px;background:#151b1f;opacity:.85}"
        ".top-gap.led-mask{position:relative;overflow:hidden;border-color:#394047;background:"
        "linear-gradient(180deg,#1a2025,#0f1418)}"
        ".top-gap.led-mask:before{content:'';position:absolute;inset:3px;border-radius:9px;"
        "background:#0a0f12;"
        "box-shadow:inset 0 0 0 1px #2d3338,inset 0 8px 10px #0009}"
        ".top-gap.led-mask:after{content:'';position:absolute;inset:6px;border-radius:7px;"
        "background:#161206;opacity:.28;"
        "box-shadow:inset 0 0 12px #000c}"
        ".led-label{position:absolute;inset:0;display:flex;align-items:center;justify-content:"
        "center;z-index:2;"
        "font-size:15px;font-weight:800;letter-spacing:.08em;color:#31342e;text-shadow:0 1px 0 "
        "#000}"
        ".top-gap.led-mask.on .led-label{color:#f2cd41;text-shadow:0 0 7px #e5b416cc,0 0 2px "
        "#fff2a0cc}"
        ".top-gap.led-mask.flash-slow .led-label{animation:labelBlinkSlow 1.4s steps(1,end) "
        "infinite}"
        ".top-gap.led-mask.flash-fast .led-label{animation:labelBlinkFast .55s steps(1,end) "
        "infinite}"
        "@keyframes labelBlinkSlow{0%%,45%%{color:#f2cd41;text-shadow:0 0 7px #e5b416cc,0 0 2px "
        "#fff2a0cc}"
        "46%%,100%%{color:#31342e;text-shadow:0 1px 0 #000}}"
        "@keyframes labelBlinkFast{0%%,40%%{color:#f2cd41;text-shadow:0 0 7px #e5b416cc,0 0 2px "
        "#fff2a0cc}"
        "41%%,100%%{color:#31342e;text-shadow:0 1px 0 #000}}"
        ".display-bay{margin-top:10px;padding:8px 7px "
        "6px;border-radius:22px;background:linear-gradient(170deg,#1e2429,#0c1013);"
        "box-shadow:inset 0 1px 0 #6c758042,inset 0 -1px 0 #050709}"
        ".display-shell{display:grid;grid-template-columns:68px 358px "
        "68px;gap:10px;align-items:start}"
        ".screen-wrap{width:358px;height:450px;border:2px solid "
        "#5a6268;border-radius:20px;overflow:hidden;background:linear-gradient(180deg,#1a2126,#"
        "0a0e11);"
        "padding:7px;position:relative;box-shadow:inset 0 2px 3px #ffffff1f,inset 0 -2px 4px "
        "#000a,0 1px 0 #000}"
        ".screen-wrap "
        "canvas{display:block;width:340px;height:432px;image-rendering:pixelated;image-rendering:"
        "crisp-edges;background:#070d0a}"
        ".soft-column{position:relative;height:450px}"
        ".soft-cell{position:absolute;left:0;right:0;display:grid;align-items:center}"
        ".soft-cell.r0{top:44px}.soft-cell.r1{top:122px}.soft-cell.r2{top:200px}.soft-cell.r3{top:"
        "278px}.soft-cell.r4{top:356px}"
        ".left-soft .soft-cell{grid-template-columns:auto 14px;column-gap:4px}"
        ".right-soft .soft-cell{grid-template-columns:14px auto;column-gap:4px}"
        ".soft-cell .tick{height:2px;background:#d9ddd7;border-radius:2px;opacity:.9;box-shadow:0 "
        "0 3px #ffffff80}"
        ".soft-cell .key{width:52px;height:52px;padding:6px 2px;border-radius:13px}"
        ".keybed{margin-top:10px;padding:8px;border-radius:18px;background:linear-gradient(170deg,#"
        "1d2328,#0c1013);"
        "box-shadow:inset 0 1px 0 #6d778244,inset 0 -1px 0 #06090b}"
        ".nav-row{grid-template-columns:repeat(6,var(--rect-key-w));gap:8px;justify-content:center;"
        "margin-top:0}"
        ".keypad{display:grid;grid-template-columns:repeat(6,var(--rect-key-w));gap:8px;justify-"
        "content:center;margin-top:8px}"
        ".key{border:1px solid var(--key-line);border-radius:12px;padding:8px 6px;"
        "background:linear-gradient(180deg,var(--key-top),var(--key-face));color:#edf1dc;width:var("
        "--rect-key-w);height:var(--rect-key-h);"
        "font-weight:700;letter-spacing:.03em;text-transform:uppercase;display:flex;flex-direction:"
        "column;"
        "align-items:center;justify-content:center;text-align:center;gap:1px;box-shadow:inset 0 "
        "1px 0 #ffffff22,0 2px 0 #020304;"
        "transition:transform .08s ease,box-shadow .08s ease,border-color .08s ease}"
        ".key>span:first-child{font-size:var(--font-small);line-height:1.05;letter-spacing:.04em}"
        ".key .sub{font-size:var(--font-small);opacity:1;letter-spacing:.04em;line-height:1.05}"
        ".key .sub.small{font-size:var(--font-small);letter-spacing:.04em}"
        ".key-alpha{align-items:flex-start;justify-content:flex-start;text-align:left;padding:5px "
        "7px 6px}"
        ".key-bottom-centre{align-items:center;justify-content:flex-end;text-align:center;padding:"
        "6px 6px 7px}"
        ".key-alpha>span:first-child,.key-emphasis>span:first-child{font-size:var(--font-large);"
        "line-height:1;letter-spacing:.02em}"
        ".key-alpha .sub{margin-top:auto;align-self:center}"
        ".key[data-button-id]{cursor:pointer;color:var(--live)}"
        ".key[data-button-id] .sub{color:var(--live)}"
        ".key[data-button-id]:hover{border-color:#8db29f}"
        ".key.down{transform:translateY(1px);box-shadow:inset 0 1px 0 #0008,0 1px 0 "
        "#020304;border-color:#97c7af}"
        ".key.ghost{color:#a8aeb4;border-style:dashed;opacity:.78}"
        ".hint-line{margin:9px 2px 0;font-size:12px;color:var(--muted)}"
        ".keystate{margin-top:10px;padding:8px 10px;border:1px solid "
        "var(--line);border-radius:8px;font-size:12px;color:var(--accent);background:#07110d;min-"
        "height:38px}"
        "body.popup-mode .wrap{max-width:790px;padding:10px 8px 12px}"
        "body.popup-mode .intro{display:none}"
        "body.popup-mode .card{padding:10px}"
        "@media (max-width:760px){"
        ".wrap{padding:12px 8px 18px}.ccu:before{inset:8px}.top-row,.nav-row,.keypad{gap:8px}}"
        "</style></head><body><main class=\"wrap\"><header class=\"intro\"><h1>Display Preview</h1>"
        "<p>Live framebuffer mirror plus a browser keypad so the CCU can be driven from your "
        "laptop.</p>",
        kHttpOkHeader);
    ok = ok && append_page_nav(cursor, remaining, ConfigWebPage::Preview);
    ok = ok && append(
        cursor, remaining,
        "</header>"
        "<section class=\"card\"><div class=\"meta\"><span class=\"badge\">%d x %d</span>"
        "<button id=\"toggle\" type=\"button\">Pause</button><button id=\"open_popup\" "
        "type=\"button\">Pop-out</button>"
        "<span id=\"state\" class=\"state\">Starting...</span></div>"
        "<div class=\"ccu-fixed\"><div class=\"ccu\" aria-label=\"Virtual CCU keypad\">"
        "<div class=\"row top-row\">"
        "<button class=\"key\" type=\"button\" data-button-id=\"Alert\" title=\"Open alert "
        "list\"><span>Alert</span></button>"
        "<div id=\"alert_led\" class=\"top-gap led-mask\" aria-hidden=\"true\"><span "
        "class=\"led-label\">ALRT</span></div><div id=\"test_led\" class=\"top-gap led-mask\" "
        "aria-hidden=\"true\"><span class=\"led-label\">TEST</span></div>"
        "<button class=\"key\" type=\"button\" data-button-id=\"Test\" title=\"Cycle TEST "
        "lamp\"><span>Test</span></button>"
        "<button class=\"key\" type=\"button\" data-button-id=\"Brt\"><span>Brt</span></button>"
        "<button class=\"key\" type=\"button\" data-button-id=\"Dim\"><span>Dim</span></button></div>"
        "<div class=\"display-bay\"><div class=\"display-shell\">"
        "<div class=\"soft-column left-soft\">"
        "<div class=\"soft-cell r0\"><button class=\"key\" type=\"button\" "
        "data-button-id=\"LeftTop\"><span>&#8594;</span></button><span class=\"tick\" "
        "aria-hidden=\"true\"></span></div>"
        "<div class=\"soft-cell r1\"><button class=\"key\" type=\"button\" "
        "data-button-id=\"LeftUpper\"><span>&#8594;</span></button><span class=\"tick\" "
        "aria-hidden=\"true\"></span></div>"
        "<div class=\"soft-cell r2\"><button class=\"key\" type=\"button\" "
        "data-button-id=\"LeftMiddle\"><span>&#8594;</span></button><span class=\"tick\" "
        "aria-hidden=\"true\"></span></div>"
        "<div class=\"soft-cell r3\"><button class=\"key\" type=\"button\" "
        "data-button-id=\"LeftLower\"><span>&#8594;</span></button><span class=\"tick\" "
        "aria-hidden=\"true\"></span></div>"
        "<div class=\"soft-cell r4\"><button class=\"key\" type=\"button\" "
        "data-button-id=\"LeftBottom\"><span>&#8594;</span></button><span class=\"tick\" "
        "aria-hidden=\"true\"></span></div>"
        "</div>"
        "<div class=\"screen-wrap\"><canvas id=\"fb\" width=\"%d\" height=\"%d\"></canvas></div>"
        "<div class=\"soft-column right-soft\">"
        "<div class=\"soft-cell r0\"><span class=\"tick\" aria-hidden=\"true\"></span><button "
        "class=\"key\" type=\"button\" "
        "data-button-id=\"RightTop\"><span>&#8592;</span></button></div>"
        "<div class=\"soft-cell r1\"><span class=\"tick\" aria-hidden=\"true\"></span><button "
        "class=\"key\" type=\"button\" "
        "data-button-id=\"RightUpper\"><span>&#8592;</span></button></div>"
        "<div class=\"soft-cell r2\"><span class=\"tick\" aria-hidden=\"true\"></span><button "
        "class=\"key\" type=\"button\" "
        "data-button-id=\"RightMiddle\"><span>&#8592;</span></button></div>"
        "<div class=\"soft-cell r3\"><span class=\"tick\" aria-hidden=\"true\"></span><button "
        "class=\"key\" type=\"button\" "
        "data-button-id=\"RightLower\"><span>&#8592;</span></button></div>"
        "<div class=\"soft-cell r4\"><span class=\"tick\" aria-hidden=\"true\"></span><button "
        "class=\"key\" type=\"button\" "
        "data-button-id=\"RightBottom\"><span>&#8592;</span></button></div>"
        "</div></div></div>"
        "<div class=\"keybed\"><div class=\"row nav-row\">"
        "<button class=\"key key-emphasis\" type=\"button\" "
        "data-button-id=\"Ltrs\"><span>Ltrs</span></button>"
        "<button class=\"key\" type=\"button\" data-button-id=\"BackStep\"><span>Back</span><span "
        "class=\"sub\">Step</span></button>"
        "<button class=\"key\" type=\"button\" "
        "data-button-id=\"CursorLeft\"><span>&#8592;</span></button>"
        "<button class=\"key\" type=\"button\" "
        "data-button-id=\"CursorRight\"><span>&#8594;</span></button>"
        "<button class=\"key\" type=\"button\" data-button-id=\"Slash\"><span>/</span></button>"
        "<button class=\"key key-emphasis\" type=\"button\" "
        "data-button-id=\"Clr\"><span>Clr</span></button></div>"
        "<div class=\"keypad\">"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaA\"><span>A</span><span class=\"sub\">Comm</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaB\"><span>B</span><span class=\"sub\">R Nav</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaC\"><span>C</span><span class=\"sub\">Perf</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaD\"><span>D</span><span class=\"sub\">Ams</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaE\"><span>E</span><span class=\"sub\">Maint</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaF\"><span>F</span><span class=\"sub\">Iff</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaG\"><span>G</span><span class=\"sub\">Totes</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaH\"><span>H</span><span class=\"sub\">Dsply</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaI\"><span>I</span><span class=\"sub\">D Link</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaJ\"><span>J</span><span class=\"sub\">1</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaK\"><span>K</span><span class=\"sub\">2</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaL\"><span>L</span><span class=\"sub\">3</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaM\"><span>M</span><span class=\"sub\">Sonics</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaN\"><span>N</span><span class=\"sub\">Radar</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaO\"><span>O</span><span class=\"sub\">Esm</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaP\"><span>P</span><span class=\"sub\">4</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaQ\"><span>Q</span><span class=\"sub\">5</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaR\"><span>R</span><span class=\"sub\">6</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaS\"><span>S</span><span class=\"sub\">Stores</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaT\"><span>T</span><span class=\"sub\">Ads</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaU\"><span>U</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaV\"><span>V</span><span class=\"sub\">7</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaW\"><span>W</span><span class=\"sub\">8</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaX\"><span>X</span><span class=\"sub\">9</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaY\"><span>Y</span><span class=\"sub\">T Nav</span></button>"
        "<button class=\"key key-alpha\" type=\"button\" "
        "data-button-id=\"AlphaZ\"><span>Z</span><span class=\"sub\">T Data</span></button>"
        "<button class=\"key key-bottom-centre\" type=\"button\" "
        "data-button-id=\"TFunc\"><span>T Func</span></button>"
        "<button class=\"key\" type=\"button\" data-button-id=\"Dot\"><span>.</span></button>"
        "<button class=\"key\" type=\"button\" "
        "data-button-id=\"Zero\"><span>&oslash;</span></button>"
        "<button class=\"key\" type=\"button\" data-button-id=\"Spc\"><span>Spc</span><span "
        "class=\"sub small\">:</span></button>"
        "</div><p class=\"hint-line\">All confirmed matrix keys are mapped. LTRS cycles ABC, "
        "123, and abc input modes.</p></div></div></div>"
        "<div class=\"keystate\" id=\"keys_state\">Ready. Press any keypad key to send an "
        "event.</div></section>"
        "<script>"
        "(function(){"
        "const w=%d,h=%d,s=%d;"
        "const canvas=document.getElementById('fb');"
        "const ctx=canvas.getContext('2d');"
        "const image=ctx.createImageData(w,h);"
        "const pixels=image.data;"
        "const state=document.getElementById('state');"
        "const toggle=document.getElementById('toggle');"
        "const popupButton=document.getElementById('open_popup');"
        "const alertLed=document.getElementById('alert_led');"
        "const testLed=document.getElementById('test_led');"
        "const keysState=document.getElementById('keys_state');"
        "const mappedKeys=document.querySelectorAll('.key[data-button-id]');"
        "const isPopupMode=window.location.search.indexOf('popup=1')>=0;"
        "if(isPopupMode){document.body.classList.add('popup-mode');}"
        "let running=true;"
        "let timer=null;"
        "let frameFetchInFlight=false;"
        "let frameAbortController=null;"
        "let panelFetchInFlight=false;"
        "let panelAbortController=null;"
        "let keyPostInFlight=false;"
        "const keyQueue=[];"
        "let consecutiveFrameFailures=0;"
        "function setKeyState(text){if(keysState){keysState.textContent=text;}}"
        "function openPopup(){"
        "if(isPopupMode){return;}"
        "const popup=window.open('/preview?popup=1','merlin_preview_window',"
        "'popup=yes,width=900,height=1550,resizable=yes,scrollbars=yes');"
        "if(popup){popup.focus();}"
        "}"
        "function "
        "flashKey(key){key.classList.add('down');setTimeout(function(){key.classList.remove('down')"
        ";},120);}"
        "function delay(ms){return new Promise(function(resolve){setTimeout(resolve,ms);});}"
        "function newAbortController(){return typeof AbortController==='function'?new "
        "AbortController():null;}"
        "function abortLowPriorityFetches(){"
        "if(frameAbortController){frameAbortController.abort();frameAbortController=null;}"
        "if(panelAbortController){panelAbortController.abort();panelAbortController=null;}"
        "}"
        "async function sendVirtualKeyNow(id,type){"
        "for(let attempt=1;attempt<=12;attempt++){"
        "const controller=newAbortController();"
        "const timeoutId=controller?setTimeout(function(){controller.abort();},1500):null;"
        "try{"
        "const response=await fetch('/api/button',{method:'POST',cache:'no-store',"
        "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'id='+encodeURIComponent(id)+'&type='+encodeURIComponent(type),"
        "signal:controller?controller.signal:undefined});"
        "if(timeoutId){clearTimeout(timeoutId);}"
        "const message=(await response.text()).trim();"
        "if(!response.ok){throw new Error(message||('HTTP '+response.status));}"
        "setKeyState(message);"
        "return true;"
        "}catch(error){"
        "if(timeoutId){clearTimeout(timeoutId);}"
        "const text=String(error&&error.message?error.message:error);"
        "const timedOut=error&&error.name==='AbortError';"
        "const networkError=error instanceof TypeError;"
        "const busy=timedOut||networkError||/busy|503|retry/i.test(text);"
        "if(attempt<12&&busy){await delay(Math.min(250,30*attempt));continue;}"
        "setKeyState('Key send failed: '+(timedOut?'timed out':text));"
        "return false;"
        "}"
        "}"
        "return false;"
        "}"
        "async function flushVirtualKeys(){"
        "if(keyPostInFlight){return;}"
        "keyPostInFlight=true;"
        "try{"
        "while(keyQueue.length>0){"
        "const next=keyQueue.shift();"
        "await sendVirtualKeyNow(next.id,next.type);"
        "}"
        "}finally{keyPostInFlight=false;if(running){void refreshPanelState();void refresh();}}"
        "}"
        "function queueVirtualKey(id,type){"
        "abortLowPriorityFetches();"
        "keyQueue.push({id:id,type:type});"
        "if(keyPostInFlight){return;}"
        "void flushVirtualKeys();"
        "}"
        "function triggerMappedKey(key){"
        "const id=key.getAttribute('data-button-id');"
        "if(!id){return;}"
        "flashKey(key);"
        "queueVirtualKey(id,'pressed');"
        "}"
        "function bindVirtualKeys(){"
        "for(let "
        "i=0;i<mappedKeys.length;i++){mappedKeys[i].addEventListener('click',function(){"
        "triggerMappedKey(this);});}"
        "const keyboardMap={"
        "'1':'AlphaJ','2':'AlphaK','3':'AlphaL','4':'AlphaP','5':'AlphaQ','6':'AlphaR',"
        "'7':'AlphaV','8':'AlphaW','9':'AlphaX','0':'Zero',"
        "'a':'AlphaA','b':'AlphaB','c':'AlphaC','d':'AlphaD','e':'AlphaE','f':'AlphaF',"
        "'g':'AlphaG','h':'AlphaH','i':'AlphaI','j':'AlphaJ','k':'AlphaK','l':'AlphaL',"
        "'m':'AlphaM','n':'AlphaN','o':'AlphaO','p':'AlphaP','q':'AlphaQ','r':'AlphaR',"
        "'s':'AlphaS','t':'AlphaT','u':'AlphaU','v':'AlphaV','w':'AlphaW','x':'AlphaX',"
        "'y':'AlphaY','z':'AlphaZ','.':'Dot','/':'Slash',' ':'Spc',"
        "'ArrowLeft':'CursorLeft','ArrowRight':'CursorRight','Backspace':'BackStep','Delete':'Clr',"
        "'Escape':'Clr','Tab':'Ltrs','F11':'Alert','F12':'Test',"
        "'F1':'LeftTop','F2':'LeftUpper','F3':'LeftMiddle','F4':'LeftLower','F5':'LeftBottom',"
        "'F6':'RightTop','F7':'RightUpper','F8':'RightMiddle','F9':'RightLower','F10':'RightBottom'"
        "};"
        "document.addEventListener('keydown',function(event){"
        "if(event.repeat){return;}"
        "const keyName=event.key.length===1?event.key.toLowerCase():event.key;"
        "const mapped=keyboardMap[keyName];"
        "if(!mapped){return;}"
        "event.preventDefault();"
        "const key=document.querySelector('[data-button-id=\"'+mapped+'\"]');"
        "if(key){triggerMappedKey(key);}"
        "});"
        "}"
        "function draw(bytes){"
        "let src=0;"
        "for(let y=0;y<h;y++){"
        "for(let xb=0;xb<s;xb++){"
        "const value=bytes[src++];"
        "for(let bit=0;bit<8;bit++){"
        "const x=(xb<<3)+bit;"
        "if(x>=w){continue;}"
        "const on=(value&(0x80>>bit))!==0;"
        "const p=((y*w)+x)*4;"
        "if(on){pixels[p]=255;pixels[p+1]=214;pixels[p+2]=64;pixels[p+3]=255;}"
        "else{pixels[p]=8;pixels[p+1]=16;pixels[p+2]=14;pixels[p+3]=255;}"
        "}"
        "}"
        "}"
        "ctx.putImageData(image,0,0);"
        "}"
        "function decodeRle(encoded){"
        "const decoded=new Uint8Array(h*s);"
        "let src=0;let dst=0;"
        "while(src+1<encoded.length&&dst<decoded.length){"
        "const count=encoded[src++];const value=encoded[src++];"
        "decoded.fill(value,dst,Math.min(decoded.length,dst+count));"
        "dst+=count;"
        "}"
        "if(dst!==decoded.length){throw new Error('Frame decode '+dst);}"
        "return decoded;"
        "}"
        "async function refresh(){"
        "if(!running||frameFetchInFlight||keyPostInFlight||keyQueue.length>0){return;}"
        "const controller=newAbortController();"
        "const options={cache:'no-store'};"
        "if(controller){options.signal=controller.signal;}"
        "frameAbortController=controller;"
        "frameFetchInFlight=true;"
        "try{"
        "const response=await fetch('/api/framebuffer.rle?t='+Date.now(),options);"
        "if(!response.ok){throw new Error('HTTP '+response.status);}"
        "const bytes=decodeRle(new Uint8Array(await response.arrayBuffer()));"
        "if(bytes.length!==(h*s)){throw new Error('Frame size '+bytes.length);}"
        "draw(bytes);"
        "consecutiveFrameFailures=0;"
        "state.textContent='Live';"
        "}catch(error){"
        "if(error&&error.name==='AbortError'){return;}"
        "consecutiveFrameFailures++;"
        "if(consecutiveFrameFailures>=3){state.textContent='Signal lost';}"
        "}finally{"
        "frameFetchInFlight=false;"
        "if(frameAbortController===controller){frameAbortController=null;}"
        "}"
        "}"
        "function schedule(){"
        "if(timer!==null){clearInterval(timer);timer=null;}"
        "if(running){timer=setInterval(refresh,350);}"
        "}"
        "toggle.addEventListener('click',function(){"
        "running=!running;"
        "toggle.textContent=running?'Pause':'Resume';"
        "state.textContent=running?'Live':'Paused';"
        "schedule();"
        "if(running){refresh();}"
        "});"
        "if(popupButton){popupButton.addEventListener('click',function(){openPopup();});}"
        "if(isPopupMode&&popupButton){popupButton.style.display='none';}"
        "function setLedMode(node,mode){"
        "if(!node){return;}"
        "node.classList.remove('on','flash-slow','flash-fast');"
        "if(mode==='on'){node.classList.add('on');return;}"
        "if(mode==='flash_slow'){node.classList.add('flash-slow');return;}"
        "if(mode==='flash_fast'){node.classList.add('flash-fast');return;}"
        "}"
        "async function refreshPanelState(){"
        "if(panelFetchInFlight||keyPostInFlight||keyQueue.length>0||frameFetchInFlight){return;}"
        "const controller=newAbortController();"
        "const options={cache:'no-store'};"
        "if(controller){options.signal=controller.signal;}"
        "panelAbortController=controller;"
        "panelFetchInFlight=true;"
        "try{"
        "const response=await fetch('/api/panel-state?t='+Date.now(),options);"
        "if(!response.ok){return;}"
        "const text=(await response.text()).trim();"
        "const lines=text.split(/\\r?\\n/);"
        "for(let i=0;i<lines.length;i++){"
        "const parts=lines[i].split('=');"
        "if(parts.length!==2){continue;}"
        "if(parts[0]==='alert'){setLedMode(alertLed,parts[1]);continue;}"
        "if(parts[0]==='test'){setLedMode(testLed,parts[1]);continue;}"
        "}"
        "}catch(error){"
        "if(!(error&&error.name==='AbortError')){return;}"
        "}finally{"
        "panelFetchInFlight=false;"
        "if(panelAbortController===controller){panelAbortController=null;}"
        "}"
        "}"
        "bindVirtualKeys();"
        "setInterval(refreshPanelState,600);"
        "refreshPanelState();"
        "schedule();"
        "refresh();"
        "})();"
        "</script></main></body></html>",
        kUiWidth, kUiHeight, kUiWidth, kUiHeight, kUiWidth, kUiHeight, kUiStride);
    if (!ok)
    {
        std::snprintf(g_response, sizeof(g_response),
                      "HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n"
                      "Display preview page exceeded response buffer.\n");
        return false;
    }

    return true;
}

/// @brief Builds one raw framebuffer response for browser-side preview rendering.
size_t build_framebuffer_response()
{
    const int header_len = std::snprintf(g_response, sizeof(g_response), "%s", kHttpBinaryHeader);
    if (header_len <= 0 || static_cast<size_t>(header_len) >= sizeof(g_response))
    {
        return 0;
    }

    if (static_cast<size_t>(header_len) + kUiFbSize > sizeof(g_response))
    {
        return 0;
    }

    const uint8_t* source = framebuffer::front();
    if (source == nullptr)
    {
        return 0;
    }

    std::memcpy(g_response + header_len, source, kUiFbSize);
    return static_cast<size_t>(header_len) + kUiFbSize;
}

/// @brief Builds a run-length encoded framebuffer response for browser preview.
/// @details Status and menu pages contain long spans of identical bytes. Sending
/// `(count, value)` pairs keeps the local preview responsive on a constrained
/// single-session HTTP server without changing the actual display framebuffer.
size_t build_framebuffer_rle_response()
{
    const int header_len = std::snprintf(g_response, sizeof(g_response), "%s", kHttpBinaryHeader);
    if (header_len <= 0 || static_cast<size_t>(header_len) >= sizeof(g_response))
    {
        return 0;
    }

    const uint8_t* source = framebuffer::front();
    if (source == nullptr)
    {
        return 0;
    }

    size_t write_index = static_cast<size_t>(header_len);
    size_t read_index = 0U;
    while (read_index < static_cast<size_t>(kUiFbSize))
    {
        const uint8_t value = source[read_index];
        uint8_t run_length = 1U;
        while (read_index + run_length < static_cast<size_t>(kUiFbSize) && run_length < 255U &&
               source[read_index + run_length] == value)
        {
            ++run_length;
        }

        if (write_index + 2U > sizeof(g_response))
        {
            return 0;
        }

        g_response[write_index++] = static_cast<char>(run_length);
        g_response[write_index++] = static_cast<char>(value);
        read_index += run_length;
    }

    return write_index;
}

/// @brief Builds a PBM (P4) snapshot response for regression capture and diffing.
/// @details The payload is a valid binary PBM image using the current UI
/// framebuffer bit packing, so host tooling can save and compare snapshots.
size_t build_framebuffer_pbm_response()
{
    const int http_header_len = std::snprintf(g_response, sizeof(g_response), "%s", kHttpPbmHeader);
    if (http_header_len <= 0 || static_cast<size_t>(http_header_len) >= sizeof(g_response))
    {
        return 0;
    }

    const int pbm_header_len = std::snprintf(
        g_response + http_header_len, sizeof(g_response) - static_cast<size_t>(http_header_len),
        "P4\n%d %d\n", kUiWidth, kUiHeight);
    if (pbm_header_len <= 0)
    {
        return 0;
    }

    const size_t header_total =
        static_cast<size_t>(http_header_len) + static_cast<size_t>(pbm_header_len);
    if (header_total >= sizeof(g_response) || header_total + kUiFbSize > sizeof(g_response))
    {
        return 0;
    }

    const uint8_t* source = framebuffer::front();
    if (source == nullptr)
    {
        return 0;
    }

    std::memcpy(g_response + header_total, source, kUiFbSize);
    return header_total + kUiFbSize;
}

/// @brief Returns true when the first request line matches the provided path.
/// @details Accepts plain path, optional trailing slash, and optional query.
bool matches_get_path(const char* request, const char* path)
{
    if (request == nullptr || path == nullptr)
    {
        return false;
    }

    const char* prefix = "GET ";
    const size_t prefix_len = std::strlen(prefix);
    const size_t path_len = std::strlen(path);
    if (std::strncmp(request, prefix, prefix_len) != 0)
    {
        return false;
    }

    const char* uri = request + prefix_len;
    if (std::strncmp(uri, path, path_len) != 0)
    {
        return false;
    }

    const char terminator = uri[path_len];
    return terminator == ' ' || terminator == '/' || terminator == '?';
}

/// @brief Returns true when the first request line matches the provided POST path.
/// @details Accepts plain path, optional trailing slash, and optional query.
bool matches_post_path(const char* request, const char* path)
{
    if (request == nullptr || path == nullptr)
    {
        return false;
    }

    const char* prefix = "POST ";
    const size_t prefix_len = std::strlen(prefix);
    const size_t path_len = std::strlen(path);
    if (std::strncmp(request, prefix, prefix_len) != 0)
    {
        return false;
    }

    const char* uri = request + prefix_len;
    if (std::strncmp(uri, path, path_len) != 0)
    {
        return false;
    }

    const char terminator = uri[path_len];
    return terminator == ' ' || terminator == '/' || terminator == '?';
}

/// @brief Resolves one posted web button id into the firmware button enum.
bool parse_web_button_id(const char* web_id, ButtonId* out_button_id)
{
    if (web_id == nullptr || out_button_id == nullptr)
    {
        return false;
    }

    const WebButtonBinding* const binding =
        std::find_if(kWebButtonBindings.begin(), kWebButtonBindings.end(),
                     [web_id](const WebButtonBinding& candidate)
                     { return std::strcmp(candidate.web_id, web_id) == 0; });
    if (binding == kWebButtonBindings.end())
    {
        return false;
    }

    *out_button_id = binding->button_id;
    return true;
}

/// @brief Parses one posted button edge string into the firmware event enum.
bool parse_button_event_type(const char* text, ButtonEventType* out_type)
{
    if (text == nullptr || out_type == nullptr)
    {
        return false;
    }

    if (std::strcmp(text, "pressed") == 0)
    {
        *out_type = ButtonEventType::Pressed;
        return true;
    }

    if (std::strcmp(text, "released") == 0)
    {
        *out_type = ButtonEventType::Released;
        return true;
    }

    return false;
}

/// @brief Returns one stable text token for a logical lamp mode.
const char* lamp_mode_text_token(LampMode mode)
{
    switch (mode)
    {
    case LampMode::Off:
        return "off";
    case LampMode::On:
        return "on";
    case LampMode::FlashSlow:
        return "flash_slow";
    case LampMode::FlashFast:
        return "flash_fast";
    }

    return "off";
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
    std::array<char, kRequestCapacity> copy = {};
    std::snprintf(copy.data(), copy.size(), "%s", body != nullptr ? body : "");
    return form_value(copy.data(), key, output, output_size);
}

bool form_has_key(const char* body, const char* key)
{
    std::array<char, 8> value = {};
    return get_form_value(body, key, value.data(), value.size());
}

/// @brief Parses a required TCP/HTTP port and rejects malformed form input.
/// @details Configuration should fail loudly when a browser submits invalid
/// numeric text; silently keeping the old port makes it look as though a save
/// succeeded when part of the form was ignored.
bool parse_port_field(const char* text, uint16_t* out_port)
{
    if (text == nullptr || text[0] == '\0' || out_port == nullptr)
    {
        return false;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0 || value > 65535UL)
    {
        return false;
    }

    *out_port = static_cast<uint16_t>(value);
    return true;
}

/// @brief Parses a bounded unsigned integer and rejects malformed text.
bool parse_u16_field(const char* text, uint16_t min_value, uint16_t max_value, uint16_t* out_value)
{
    if (text == nullptr || text[0] == '\0' || out_value == nullptr)
    {
        return false;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value < min_value || value > max_value)
    {
        return false;
    }

    *out_value = static_cast<uint16_t>(value);
    return true;
}

/// @brief Parses a submitted latitude/longitude magnitude + hemisphere pair
/// and combines them into a signed coordinate value (issue #87).
bool parse_coordinate_field(const char* magnitude_text, const char* hemi_text, bool is_latitude,
                            double* out_signed)
{
    if (magnitude_text == nullptr || magnitude_text[0] == '\0' || hemi_text == nullptr ||
        hemi_text[0] == '\0' || out_signed == nullptr)
    {
        return false;
    }

    char* end = nullptr;
    const double magnitude = std::strtod(magnitude_text, &end);
    if (end == magnitude_text || *end != '\0')
    {
        return false;
    }

    return geo_coordinates::combine_hemisphere(magnitude, hemi_text[0], is_latitude, out_signed);
}

/// @brief Returns whether text can be stored as a short human-facing label.
bool is_printable_config_text(const char* text)
{
    if (text == nullptr)
    {
        return false;
    }

    for (size_t i = 0; text[i] != '\0'; ++i)
    {
        const auto ch = static_cast<unsigned char>(text[i]);
        if (ch < 0x20U || ch > 0x7EU)
        {
            return false;
        }
    }

    return true;
}

/// @brief Returns whether a device name is safe for hostnames and entity IDs.
bool is_valid_device_name(const char* text)
{
    if (text == nullptr || text[0] == '\0')
    {
        return false;
    }

    for (size_t i = 0; text[i] != '\0'; ++i)
    {
        const auto ch = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(ch) && ch != '-' && ch != '_')
        {
            return false;
        }
    }

    return true;
}

/// @brief Validates one bounded text field before it is copied into flash config.
bool validate_text_field(const char* label, const char* value, size_t max_length, bool required,
                         bool (*validator)(const char*), char* error, size_t error_size)
{
    if (value == nullptr)
    {
        value = "";
    }

    if (required && value[0] == '\0')
    {
        std::snprintf(error, error_size, "Configuration not saved: %s is required.", label);
        return false;
    }

    if (std::strlen(value) > max_length)
    {
        std::snprintf(error, error_size,
                      "Configuration not saved: %s is too long; maximum is %u characters.", label,
                      static_cast<unsigned>(max_length));
        return false;
    }

    if (value[0] != '\0' && validator != nullptr && !validator(value))
    {
        std::snprintf(error, error_size,
                      "Configuration not saved: %s contains unsupported characters.", label);
        return false;
    }

    return true;
}

WeatherSource parse_weather_source(const char* text, WeatherSource fallback)
{
    if (std::strcmp(text, "open_meteo") == 0)
    {
        return WeatherSource::OpenMeteo;
    }
    if (std::strcmp(text, "met_norway") == 0)
    {
        return WeatherSource::OpenMeteo;
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
// The form fields are intentionally processed in page order so validation
// errors map directly back to the UI a user just submitted.
const char* handle_config_post(const char* body)
{
    std::printf("Web config POST received: %u bytes\n", body != nullptr ? std::strlen(body) : 0U);

    if (!form_has_key(body, "config_form_marker") ||
        !form_has_key(body, "remote_config_present") || !form_has_key(body, "ha_enabled_present") ||
        !form_has_key(body, "mqtt_enabled_present") ||
        !form_has_key(body, "air_traffic_enabled_present"))
    {
        std::printf("Web config save rejected: incomplete form payload; reload required\n");
        return "Configuration not saved: configuration page was incomplete. Reload and try again.";
    }

    RuntimeConfig cfg = config_manager::settings();
    std::array<char, 160> value = {};
    static char validation_error[160] = {};
    validation_error[0] = '\0';

    if (get_form_value(body, "device_name", value.data(), value.size()))
    {
        if (!validate_text_field("Device name", value.data(), cfg.device_name.size() - 1, true,
                                 is_valid_device_name, validation_error, sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.device_name, value.data());
    }
    if (get_form_value(body, "device_label", value.data(), value.size()))
    {
        if (!validate_text_field("Display label", value.data(), cfg.device_label.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.device_label, value.data());
    }
    if (get_form_value(body, "location", value.data(), value.size()))
    {
        if (!validate_text_field("Location", value.data(), cfg.location.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.location, value.data());
    }
    if (get_form_value(body, "room", value.data(), value.size()))
    {
        if (!validate_text_field("Room", value.data(), cfg.room.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.room, value.data());
    }
    cfg.remote_config_enabled = form_has_key(body, "remote_config");

    if (get_form_value(body, "wifi_ssid", value.data(), value.size()))
    {
        if (!validate_text_field("Wi-Fi SSID", value.data(), cfg.wifi_ssid.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.wifi_ssid, value.data());
    }
    if (get_form_value(body, "wifi_password", value.data(), value.size()) && value[0] != '\0')
    {
        if (!validate_text_field("Wi-Fi password", value.data(), cfg.wifi_password.size() - 1, true,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.wifi_password, value.data());
    }

    cfg.home_assistant_enabled = form_has_key(body, "ha_enabled");
    if (get_form_value(body, "ha_host", value.data(), value.size()))
    {
        if (!validate_text_field("Home Assistant host", value.data(), cfg.home_assistant_host.size() - 1,
                                 false, is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.home_assistant_host, value.data());
    }
    if (get_form_value(body, "ha_port", value.data(), value.size()))
    {
        uint16_t port = 0;
        if (!parse_port_field(value.data(), &port))
        {
            std::printf("Web config save rejected: invalid Home Assistant port '%s'\n", value.data());
            return "Configuration not saved: Home Assistant port must be 1-65535.";
        }
        cfg.home_assistant_port = port;
    }
    if (get_form_value(body, "ha_token", value.data(), value.size()) && value[0] != '\0')
    {
        if (!validate_text_field("Home Assistant token", value.data(), cfg.home_assistant_token.size() - 1,
                                 true, is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.home_assistant_token, value.data());
    }
    if (get_form_value(body, "ha_entity", value.data(), value.size()))
    {
        if (!validate_text_field("Tracked entity", value.data(), cfg.home_assistant_entity_id.size() - 1,
                                 false, is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.home_assistant_entity_id, value.data());
    }
    if (get_form_value(body, "ha_self", value.data(), value.size()))
    {
        if (!validate_text_field("Self entity", value.data(), cfg.home_assistant_self_entity_id.size() - 1,
                                 false, is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.home_assistant_self_entity_id, value.data());
    }
    if (get_form_value(body, "weather_entity", value.data(), value.size()))
    {
        if (!validate_text_field("Weather entity", value.data(), cfg.weather_entity_id.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.weather_entity_id, value.data());
    }
    if (get_form_value(body, "sun_entity", value.data(), value.size()))
    {
        if (!validate_text_field("Sun entity", value.data(), cfg.sun_entity_id.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.sun_entity_id, value.data());
    }
    {
        std::array<char, 16> lat_text = {};
        std::array<char, 4> lat_hemi_text = {};
        std::array<char, 16> lon_text = {};
        std::array<char, 4> lon_hemi_text = {};
        const bool has_lat = get_form_value(body, "weather_lat", lat_text.data(), lat_text.size());
        const bool has_lat_hemi =
            get_form_value(body, "weather_lat_hemi", lat_hemi_text.data(), lat_hemi_text.size());
        const bool has_lon = get_form_value(body, "weather_lon", lon_text.data(), lon_text.size());
        const bool has_lon_hemi =
            get_form_value(body, "weather_lon_hemi", lon_hemi_text.data(), lon_hemi_text.size());

        // The direct-weather fieldset is disabled (and its inputs excluded from the
        // POST body) whenever the Home Assistant weather source is selected; only
        // validate when the browser actually submitted these fields.
        if (has_lat || has_lat_hemi || has_lon || has_lon_hemi)
        {
            double lat = 0.0;
            double lon = 0.0;
            if (!parse_coordinate_field(lat_text.data(), lat_hemi_text.data(), true, &lat) ||
                !parse_coordinate_field(lon_text.data(), lon_hemi_text.data(), false, &lon))
            {
                std::printf("Web config save rejected: direct weather coordinates invalid\n");
                return "Configuration not saved: direct weather latitude/longitude must be valid.";
            }

            std::array<char, 32> coordinates_text = {};
            if (geo_coordinates::format_coordinates_text(lat, lon, coordinates_text.data(),
                                                          coordinates_text.size()))
            {
                copy_text(cfg.weather_coordinates, coordinates_text.data());
            }
        }
    }
    if (get_form_value(body, "weather_source", value.data(), value.size()))
    {
        cfg.weather_source = parse_weather_source(value.data(), cfg.weather_source);
    }

    cfg.mqtt_enabled = form_has_key(body, "mqtt_enabled");
    if (get_form_value(body, "mqtt_host", value.data(), value.size()))
    {
        if (!validate_text_field("MQTT broker host", value.data(), cfg.mqtt_host.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.mqtt_host, value.data());
    }
    if (get_form_value(body, "mqtt_port", value.data(), value.size()))
    {
        uint16_t port = 0;
        if (!parse_port_field(value.data(), &port))
        {
            std::printf("Web config save rejected: invalid MQTT port '%s'\n", value.data());
            return "Configuration not saved: MQTT port must be 1-65535.";
        }
        cfg.mqtt_port = port;
    }
    if (get_form_value(body, "mqtt_username", value.data(), value.size()))
    {
        if (!validate_text_field("MQTT username", value.data(), cfg.mqtt_username.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.mqtt_username, value.data());
    }
    if (get_form_value(body, "mqtt_password", value.data(), value.size()) && value[0] != '\0')
    {
        if (!validate_text_field("MQTT password", value.data(), cfg.mqtt_password.size() - 1, true,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.mqtt_password, value.data());
    }
    if (get_form_value(body, "mqtt_prefix", value.data(), value.size()))
    {
        if (!validate_text_field(
                "MQTT discovery prefix", value.data(), cfg.mqtt_discovery_prefix.size() - 1, false,
                is_printable_config_text, validation_error, sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.mqtt_discovery_prefix, value.data());
    }
    if (get_form_value(body, "mqtt_topic", value.data(), value.size()))
    {
        if (!validate_text_field("MQTT base topic", value.data(), cfg.mqtt_base_topic.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.mqtt_base_topic, value.data());
    }

    cfg.air_traffic_enabled = form_has_key(body, "air_traffic_enabled");
    if (get_form_value(body, "air_traffic_host", value.data(), value.size()))
    {
        if (!validate_text_field("Air traffic host", value.data(), cfg.air_traffic_host.size() - 1, false,
                                 is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.air_traffic_host, value.data());
    }
    if (get_form_value(body, "air_traffic_port", value.data(), value.size()))
    {
        uint16_t port = 0;
        if (!parse_port_field(value.data(), &port))
        {
            std::printf("Web config save rejected: invalid air traffic port '%s'\n", value.data());
            return "Configuration not saved: air traffic port must be 1-65535.";
        }
        cfg.air_traffic_port = port;
    }
    if (get_form_value(body, "air_traffic_radius", value.data(), value.size()))
    {
        uint16_t radius_nm = 0;
        if (!parse_u16_field(value.data(), 1, 250, &radius_nm))
        {
            std::printf("Web config save rejected: invalid air traffic radius '%s'\n", value.data());
            return "Configuration not saved: air traffic radius must be 1-250 nm.";
        }
        cfg.air_traffic_radius_nm = radius_nm;
    }
    {
        std::array<char, 16> lat_text = {};
        std::array<char, 4> lat_hemi_text = {};
        std::array<char, 16> lon_text = {};
        std::array<char, 4> lon_hemi_text = {};
        const bool has_lat = get_form_value(body, "air_traffic_lat", lat_text.data(), lat_text.size());
        const bool has_lat_hemi = get_form_value(body, "air_traffic_lat_hemi", lat_hemi_text.data(),
                                                  lat_hemi_text.size());
        const bool has_lon = get_form_value(body, "air_traffic_lon", lon_text.data(), lon_text.size());
        const bool has_lon_hemi = get_form_value(body, "air_traffic_lon_hemi", lon_hemi_text.data(),
                                                  lon_hemi_text.size());

        // The air-traffic fieldset is disabled (and its inputs excluded from the
        // POST body) whenever "Enable Local Traffic" is unchecked; only validate
        // when the browser actually submitted these fields.
        if (has_lat || has_lat_hemi || has_lon || has_lon_hemi)
        {
            double lat = 0.0;
            double lon = 0.0;
            if (!parse_coordinate_field(lat_text.data(), lat_hemi_text.data(), true, &lat) ||
                !parse_coordinate_field(lon_text.data(), lon_hemi_text.data(), false, &lon))
            {
                std::printf("Web config save rejected: air traffic coordinates invalid\n");
                return "Configuration not saved: air traffic latitude/longitude must be valid.";
            }

            std::array<char, 32> coordinates_text = {};
            if (geo_coordinates::format_coordinates_text(lat, lon, coordinates_text.data(),
                                                          coordinates_text.size()))
            {
                copy_text(cfg.air_traffic_coordinates, coordinates_text.data());
            }
        }
    }
    if (get_form_value(body, "air_traffic_api_key", value.data(), value.size()) && value[0] != '\0')
    {
        if (!validate_text_field("Air traffic API key", value.data(), cfg.air_traffic_api_key.size() - 1,
                                 true, is_printable_config_text, validation_error,
                                 sizeof(validation_error)))
        {
            std::printf("Web config save rejected: %s\n", validation_error);
            return validation_error;
        }
        copy_text(cfg.air_traffic_api_key, value.data());
    }

    if (get_form_value(body, "time_zone", value.data(), value.size()))
    {
        cfg.time_zone = parse_time_zone(value.data(), cfg.time_zone);
    }
    if (get_form_value(body, "screen_saver", value.data(), value.size()))
    {
        cfg.screen_saver = parse_screen_saver(value.data(), cfg.screen_saver);
    }
    if (get_form_value(body, "screen_timeout", value.data(), value.size()))
    {
        uint16_t timeout_minutes = 0;
        if (!parse_u16_field(value.data(), 0, 120, &timeout_minutes))
        {
            std::printf("Web config save rejected: invalid screen timeout '%s'\n", value.data());
            return "Configuration not saved: screen-saver timeout must be 0-120 minutes.";
        }
        cfg.screen_saver_timeout_minutes = timeout_minutes;
    }

    if (!config_manager::save(cfg))
    {
        std::printf("Web config save failed: flash write failed\n");
        return "Configuration not saved: flash write failed.";
    }

    console_controller::apply_runtime_config(config_manager::settings());
    console_controller::request_redraw();
    std::printf("Web config save complete: device='%s' label='%s'\n",
                config_manager::settings().device_name.data(),
                config_manager::settings().device_label.data());
    return "Configuration saved. Display settings and local web-access policy apply now; Wi-Fi and "
           "integration transport changes take effect after reboot.";
}

/// @brief Handles one posted virtual keypad event from the preview harness.
bool handle_button_post(const char* body, char* message, size_t message_size)
{
    if (message == nullptr || message_size == 0)
    {
        return false;
    }

    message[0] = '\0';
    std::array<char, 24> id_text = {};
    if (!get_form_value(body, "id", id_text.data(), id_text.size()))
    {
        std::snprintf(message, message_size, "Missing button id.");
        return false;
    }

    ButtonId id = ButtonId::LeftTop;
    if (!parse_web_button_id(id_text.data(), &id))
    {
        std::snprintf(message, message_size, "Unknown button id '%s'.", id_text.data());
        return false;
    }

    ButtonEventType type = ButtonEventType::Pressed;
    std::array<char, 24> type_text = {};
    if (get_form_value(body, "type", type_text.data(), type_text.size()) &&
        !parse_button_event_type(type_text.data(), &type))
    {
        std::snprintf(message, message_size, "Unknown button type '%s'.", type_text.data());
        return false;
    }

    const ButtonEvent event = {id, type};
    input::handle_button_event(event);
    if (type == ButtonEventType::Pressed)
    {
        console_controller::request_user_activity();
    }

    const bool changed = console_controller::handle_button_event(event);
    if (changed)
    {
        console_controller::request_redraw();
    }

    std::snprintf(message, message_size, "%s %s (%s).", changed ? "Accepted" : "Ignored",
                  input::button_name(id),
                  type == ButtonEventType::Pressed ? "pressed" : "released");
    return true;
}

/// @brief Handles one preview-only lamp control action posted from the web harness.
bool handle_panel_action_post(const char* body, char* message, size_t message_size)
{
    if (message == nullptr || message_size == 0)
    {
        return false;
    }

    message[0] = '\0';
    std::array<char, 32> action_text = {};
    if (!get_form_value(body, "action", action_text.data(), action_text.size()))
    {
        std::snprintf(message, message_size, "Missing panel action.");
        return false;
    }

    if (std::strcmp(action_text.data(), "open_alerts") == 0)
    {
        console_controller::request_user_activity();
        const bool changed = console_controller::open_alert_page();
        if (changed)
        {
            console_controller::request_redraw();
        }
        std::snprintf(message, message_size, "Opened alert page.");
        return true;
    }

    if (std::strcmp(action_text.data(), "cycle_test") == 0)
    {
        console_controller::request_user_activity();
        (void)console_controller::cycle_test_lamp_preview();
        console_controller::request_redraw();
        std::snprintf(message, message_size, "Preview TEST lamp cycled.");
        return true;
    }

    std::snprintf(message, message_size, "Unknown panel action '%s'.", action_text.data());
    return false;
}

/// @brief Returns a short, stable page-name token for automated test scripts.
/// @details Deliberately separate from screens.cpp's human-facing page
/// titles -- this only needs to distinguish the handful of pages a test
/// script might assert against, not read well on the physical display.
const char* debug_active_page_token(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Pinter:
        return "Pinter";
    case MenuPage::PinterSelectBrew:
        return "PinterSelectBrew";
    case MenuPage::PinterStartTiming:
        return "PinterStartTiming";
    default:
        return "Other";
    }
}

/// @brief Writes one plain-text panel state snapshot for web preview polling.
/// @details Also carries a few Pinter pending-timing fields purely so
/// automated click/latency test scripts can assert the resulting state
/// matches the number of clicks sent, rather than only measuring response
/// time -- see tools/button_stress_test.py.
void build_panel_state_text(char* message, size_t message_size)
{
    if (message == nullptr || message_size == 0)
    {
        return;
    }

    const ConsoleState& state = console_controller::state();
    const LampMode alert_mode = state.lamps[kAlertLampIndex];
    const LampMode test_mode = state.lamps[kTestLampIndex];
    std::snprintf(message, message_size,
                  "alert=%s\ntest=%s\npage=%s\nbrew_days=%u\ncond_days=%u\ncrash_days=%u\n",
                  lamp_mode_text_token(alert_mode), lamp_mode_text_token(test_mode),
                  debug_active_page_token(state.active_page),
                  static_cast<unsigned>(state.pinter_pending_brewing_days),
                  static_cast<unsigned>(state.pinter_pending_conditioning_days),
                  static_cast<unsigned>(state.pinter_pending_cold_crash_days));
}

/// @brief Starts sending prepared response bytes for this session.
void send_response_with_length(WebSession* session, tcp_pcb* pcb, const char* response,
                               size_t response_len, bool low_priority_response)
{
    if (session == nullptr || pcb == nullptr || response == nullptr)
    {
        return;
    }

    session->response_len = response_len;
    session->response_sent = 0;
    session->close_pending = false;
    session->low_priority_response = low_priority_response;
    tcp_sent(pcb, on_sent);
    (void)send_pending_response(session, pcb);
}

/// @brief Starts sending prepared high-priority response bytes for this session.
void send_response_with_length(WebSession* session, tcp_pcb* pcb, const char* response,
                               size_t response_len)
{
    send_response_with_length(session, pcb, response, response_len, false);
}

/// @brief Starts sending a null-terminated text response.
void send_response(WebSession* session, tcp_pcb* pcb, const char* response)
{
    send_response_with_length(session, pcb, response, std::strlen(response));
}

/// @brief Handles a complete HTTP request from the local config page.
void handle_request(WebSession* session, tcp_pcb* pcb, const char* request)
{
    const char* message = "";
    if (matches_post_path(request, "/api/button"))
    {
        const char* body = std::strstr(request, "\r\n\r\n");
        std::array<char, 96> button_message = {};
        const bool ok = handle_button_post(body != nullptr ? body + 4 : "", button_message.data(),
                                           button_message.size());
        std::snprintf(g_response, sizeof(g_response), "%s%s\n",
                      ok ? kHttpTextHeader : kHttpBadRequestHeader, button_message.data());
        send_response(session, pcb, g_response);
        return;
    }
    if (matches_post_path(request, "/api/panel-state"))
    {
        const char* body = std::strstr(request, "\r\n\r\n");
        std::array<char, 96> panel_message = {};
        const bool ok = handle_panel_action_post(body != nullptr ? body + 4 : "",
                                                 panel_message.data(), panel_message.size());
        std::snprintf(g_response, sizeof(g_response), "%s%s\n",
                      ok ? kHttpTextHeader : kHttpBadRequestHeader, panel_message.data());
        send_response(session, pcb, g_response);
        return;
    }
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
    else if (matches_get_path(request, "/api/panel-state"))
    {
        std::array<char, 128> panel_state = {};
        build_panel_state_text(panel_state.data(), panel_state.size());
        std::snprintf(g_response, sizeof(g_response), "%s%s", kHttpTextHeader, panel_state.data());
        send_response(session, pcb, g_response);
        return;
    }
    else if (matches_get_path(request, "/preview"))
    {
        (void)build_preview_page();
        send_response(session, pcb, g_response);
        return;
    }
    else if (matches_get_path(request, "/pinter"))
    {
        (void)build_pinter_activity_page();
        send_response(session, pcb, g_response);
        return;
    }
    else if (matches_get_path(request, "/api/framebuffer.pbm"))
    {
        const size_t response_len = build_framebuffer_pbm_response();
        if (response_len == 0)
        {
            std::snprintf(g_response, sizeof(g_response),
                          "%sFramebuffer PBM response buffer exhausted\n", kHttpBadRequestHeader);
            send_response(session, pcb, g_response);
            return;
        }

        send_response_with_length(session, pcb, g_response, response_len, true);
        return;
    }
    else if (matches_get_path(request, "/api/framebuffer.rle"))
    {
        const size_t response_len = build_framebuffer_rle_response();
        if (response_len == 0)
        {
            std::snprintf(g_response, sizeof(g_response),
                          "%sFramebuffer RLE preview response buffer exhausted\n",
                          kHttpBadRequestHeader);
            send_response(session, pcb, g_response);
            return;
        }

        send_response_with_length(session, pcb, g_response, response_len, true);
        return;
    }
    else if (matches_get_path(request, "/api/framebuffer"))
    {
        const size_t response_len = build_framebuffer_response();
        if (response_len == 0)
        {
            std::snprintf(g_response, sizeof(g_response),
                          "%sFramebuffer preview response buffer exhausted\n",
                          kHttpBadRequestHeader);
            send_response(session, pcb, g_response);
            return;
        }

        send_response_with_length(session, pcb, g_response, response_len, true);
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
    const auto header_bytes = static_cast<size_t>((body + 4) - request);
    return request_len >= header_bytes + content_length;
}

err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t err)
{
    auto* session = static_cast<WebSession*>(arg);
    if (err != ERR_OK || p == nullptr || session == nullptr)
    {
        bool aborted = false;
        if (p != nullptr)
        {
            pbuf_free(p);
        }
        if (pcb != nullptr)
        {
            tcp_arg(pcb, nullptr);
            tcp_recv(pcb, nullptr);
            tcp_sent(pcb, nullptr);
            tcp_err(pcb, nullptr);
            const err_t close_rc = tcp_close(pcb);
            if (close_rc != ERR_OK)
            {
                tcp_abort(pcb);
                aborted = true;
            }
        }
        if (g_session.pcb == pcb)
        {
            // A browser refresh or dropped Wi-Fi packet can close the request
            // before headers are complete. Release the singleton session so
            // the next page load is not rejected as permanently busy.
            g_session = {};
        }
        return aborted ? ERR_ABRT : ERR_OK;
    }

    session->last_activity = get_absolute_time();

    const size_t copy_len = (session->request_len + p->tot_len < sizeof(session->request) - 1)
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

/// @brief Aborts a session that has made no recv/send progress for too long.
/// @details lwIP's own TCP retransmission/error detection can take tens of
/// seconds to notice a silently-vanished peer (a dropped Wi-Fi packet, or a
/// browser abandoning a fetch without a clean FIN/RST reaching us). Without
/// this, a stalled session pins the singleton `g_session` for however long
/// that takes, rejecting every other request in the meantime.
err_t on_poll(void* arg, tcp_pcb* pcb)
{
    auto* session = static_cast<WebSession*>(arg);
    if (session == nullptr || pcb == nullptr)
    {
        return ERR_OK;
    }

    const int64_t idle_us = absolute_time_diff_us(session->last_activity, get_absolute_time());
    if (idle_us < static_cast<int64_t>(kSessionIdleTimeoutMs) * 1000)
    {
        return ERR_OK;
    }

    PERIODIC_LOG("Web config session idle %lldms, aborting to free the singleton slot\n",
                 static_cast<long long>(idle_us / 1000));
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    tcp_abort(pcb);
    if (g_session.pcb == pcb)
    {
        g_session = {};
    }
    return ERR_ABRT;
}

/// @brief Replies with a temporary busy response then closes the connection.
void reject_busy_connection(tcp_pcb* pcb)
{
    if (pcb == nullptr)
    {
        return;
    }

    const size_t response_len = std::strlen(kHttpBusyResponse);
    err_t rc =
        tcp_write(pcb, kHttpBusyResponse, static_cast<u16_t>(response_len), TCP_WRITE_FLAG_COPY);
    if (rc == ERR_OK)
    {
        (void)tcp_output(pcb);
    }

    rc = tcp_close(pcb);
    if (rc != ERR_OK)
    {
        tcp_abort(pcb);
    }
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
        if (!g_session.low_priority_response)
        {
            reject_busy_connection(new_pcb);
            return ERR_OK;
        }

        abort_active_session();
    }

    g_session = {};
    g_session.pcb = new_pcb;
    g_session.last_activity = get_absolute_time();
    tcp_arg(new_pcb, &g_session);
    tcp_recv(new_pcb, on_recv);
    tcp_sent(new_pcb, on_sent);
    tcp_err(new_pcb, on_error);
    tcp_poll(new_pcb, on_poll, kSessionPollInterval);
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
    const bool should_run = config_manager::settings().remote_config_enabled &&
                            wifi_status.state == WifiConnectionState::Connected &&
                            wifi_status.ip_address[0] != '\0';

    // g_session is shared with lwIP callbacks running in the cyw43 background
    // context, so the pending-response check itself must be inside the lock,
    // not just the call that follows it — otherwise the foreground loop can
    // race a background callback (e.g. close_session resetting g_session) on
    // this very check.
    cyw43_arch_lwip_begin();
    if (g_session.pcb != nullptr && g_session.response_len > 0 &&
        g_session.response_sent < g_session.response_len)
    {
        (void)send_pending_response(&g_session, g_session.pcb);
    }
    cyw43_arch_lwip_end();

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

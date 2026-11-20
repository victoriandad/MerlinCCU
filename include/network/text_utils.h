#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

/// @brief Small bounded text/endpoint helpers shared across config persistence
/// and the network managers (see issue #64). Kept header-only and dependency
/// free so it can be included from host tests as well as firmware.
namespace text_utils
{

/// @brief Copies a C string into a fixed-size buffer, truncating safely and
/// zero-filling the rest. A null `src` clears the destination.
/// @details Copies byte-by-byte rather than via `snprintf("%s", src)` so an
/// arbitrarily long `src` cannot trigger `-Wformat-truncation`: the compiler
/// can see the loop bound is `N` regardless of `src`'s length.
template <size_t N> void copy_text(std::array<char, N>& dest, const char* src)
{
    dest.fill('\0');
    if (src == nullptr)
    {
        return;
    }

    size_t i = 0;
    for (; i + 1 < N && src[i] != '\0'; ++i)
    {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/// @brief Parses a decimal TCP port string in the range 1-65535.
inline bool parse_port(const char* text, uint16_t* out_port)
{
    if (text == nullptr || *text == '\0' || out_port == nullptr)
    {
        return false;
    }

    constexpr unsigned long kMaxTcpPortValue = std::numeric_limits<uint16_t>::max();

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

/// @brief Splits a (scheme-stripped) "host[:port]" endpoint at the first `:`
/// or `/`, copying the host into a bounded buffer and parsing an optional
/// port suffix via `parse_port()`. `port_out` is left unchanged when no `:` is
/// present, so callers should pre-seed it with their default port.
/// @return false if the host is empty/too long, or a `:port` suffix is present
/// but fails to parse; `end_out` (if non-null) receives a pointer to the first
/// unconsumed character (start of a path, or the terminating '\0').
inline bool parse_host_and_optional_port(const char* text, char* host_out, size_t host_out_size,
                                          uint16_t* port_out, const char** end_out = nullptr)
{
    const char* host_start = text;
    const char* host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/')
    {
        ++host_end;
    }

    const size_t host_len = static_cast<size_t>(host_end - host_start);
    if (host_len == 0 || host_len >= host_out_size)
    {
        return false;
    }

    std::memcpy(host_out, host_start, host_len);
    host_out[host_len] = '\0';

    const char* end = host_end;
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
            return false;
        }

        std::memcpy(port_text, port_start, port_len);
        port_text[port_len] = '\0';
        if (!parse_port(port_text, port_out))
        {
            return false;
        }

        end = port_end;
    }

    if (end_out != nullptr)
    {
        *end_out = end;
    }

    return true;
}

/// @brief One endpoint scheme a client accepts, and the port it implies.
struct EndpointScheme
{
    std::string_view prefix;
    uint16_t default_port;
    bool uses_tls;
};

inline constexpr uint16_t kHttpDefaultPort = 80U;
inline constexpr uint16_t kHttpsDefaultPort = 443U;
inline constexpr uint16_t kMqttDefaultPort = 1883U;

/// @brief Schemes accepted for Home Assistant REST and the shares feed.
inline constexpr std::array<EndpointScheme, 2> kHttpSchemes = {{
    {"http://", kHttpDefaultPort, false},
    {"https://", kHttpsDefaultPort, true},
}};

/// @brief Schemes accepted for the MQTT broker. Both mean plain TCP.
inline constexpr std::array<EndpointScheme, 2> kMqttSchemes = {{
    {"mqtt://", kMqttDefaultPort, false},
    {"tcp://", kMqttDefaultPort, false},
}};

/// @brief Parses `[scheme://]host[:port]` the way a URL is read.
/// @details A URL carries its own port: `http://ha.lan/` means port 80 and
/// `https://` means 443, just as a browser reads them. Only a bare host falls
/// back to `configured_port`, the separate Port setting. An explicit `:port`
/// always wins. Without this, `http://ha.lan/` silently kept the 8123 default
/// and never reached a Home Assistant served on port 80.
/// @param text Endpoint text from config.
/// @param schemes Schemes this client accepts; anything else is read as a host.
/// @param configured_port Port used when `text` has neither a scheme nor `:port`.
/// @param tls_out Optional; set when the matched scheme uses TLS.
/// @param end_out Optional; receives the first unparsed character (a path or '\0').
/// @return false if the host is empty or too long, or the `:port` is invalid.
template <size_t N>
inline bool parse_endpoint(const char* text, const std::array<EndpointScheme, N>& schemes,
                           uint16_t configured_port, char* host_out, size_t host_out_size,
                           uint16_t* port_out, bool* tls_out = nullptr,
                           const char** end_out = nullptr)
{
    if (text == nullptr || host_out == nullptr || port_out == nullptr)
    {
        return false;
    }

    const char* host_start = text;
    uint16_t port = configured_port;
    bool uses_tls = false;
    for (const EndpointScheme& scheme : schemes)
    {
        if (std::strncmp(text, scheme.prefix.data(), scheme.prefix.size()) == 0)
        {
            host_start = text + scheme.prefix.size();
            port = scheme.default_port;
            uses_tls = scheme.uses_tls;
            break;
        }
    }

    if (!parse_host_and_optional_port(host_start, host_out, host_out_size, &port, end_out))
    {
        return false;
    }

    *port_out = port;
    if (tls_out != nullptr)
    {
        *tls_out = uses_tls;
    }
    return true;
}

} // namespace text_utils

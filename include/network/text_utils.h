#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

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

} // namespace text_utils

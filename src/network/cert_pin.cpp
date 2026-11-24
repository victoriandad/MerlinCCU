#include "cert_pin.h"

#include <cstdio>

namespace cert_pin
{

namespace
{

/// @brief The value of one hex digit, or -1.
int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::optional<Fingerprint> parse(std::string_view text)
{
    // openssl prints "sha256 Fingerprint=AB:CD:..."; keep what follows '='.
    if (const size_t equals = text.rfind('='); equals != std::string_view::npos)
    {
        text.remove_prefix(equals + 1U);
    }
    Fingerprint fingerprint = {};
    size_t digits = 0U;
    for (const char c : text)
    {
        if (c == ':' || c == ' ' || c == '\t' || c == '-')
        {
            continue;
        }
        const int value = hex_value(c);
        if (value < 0 || digits >= fingerprint.size() * 2U)
        {
            return std::nullopt;
        }
        const size_t byte = digits / 2U;
        fingerprint[byte] = static_cast<uint8_t>((digits % 2U == 0U)
                                                     ? (value << 4)
                                                     : (fingerprint[byte] | value));
        ++digits;
    }
    if (digits != fingerprint.size() * 2U)
    {
        return std::nullopt;
    }
    return fingerprint;
}

void format(const Fingerprint& fingerprint, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    out[0] = '\0';
    if (out_size <= kFormattedLength)
    {
        return;
    }
    size_t at = 0U;
    for (size_t i = 0U; i < fingerprint.size(); ++i)
    {
        std::snprintf(out + at, out_size - at, i == 0U ? "%02X" : ":%02X",
                      static_cast<unsigned>(fingerprint[i]));
        at += i == 0U ? 2U : 3U;
    }
}

} // namespace cert_pin

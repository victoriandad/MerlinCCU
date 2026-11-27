#include "web_auth.h"

namespace web_auth
{

namespace
{

/// @brief The request path, from after the method to the first space or '?'.
std::string_view request_path(std::string_view request, std::string_view& method)
{
    const size_t space = request.find(' ');
    if (space == std::string_view::npos)
    {
        method = {};
        return {};
    }
    method = request.substr(0U, space);
    std::string_view rest = request.substr(space + 1U);
    const size_t end = rest.find_first_of(" ?\r\n");
    return end == std::string_view::npos ? rest : rest.substr(0U, end);
}

/// @brief Drops one trailing slash, so "/ping/" matches "/ping".
std::string_view without_trailing_slash(std::string_view path)
{
    if (path.size() > 1U && path.back() == '/')
    {
        path.remove_suffix(1U);
    }
    return path;
}

} // namespace

bool is_public_request(std::string_view request)
{
    std::string_view method;
    const std::string_view path = without_trailing_slash(request_path(request, method));
    if (path == "/login")
    {
        return method == "GET" || method == "POST";
    }
    // Read-only status for monitors that cannot log in: Uptime Kuma and the
    // house dashboard (docs/network-deployment.md).
    return method == "GET" &&
           (path == "/ping" || path == "/api/firmware" || path == "/api/status.json");
}

bool pin_is_acceptable(std::string_view pin)
{
    if (pin.size() < kMinPinLength || pin.size() > kMaxPinLength)
    {
        return false;
    }
    if (pin.front() == ' ' || pin.back() == ' ')
    {
        return false;
    }
    for (const char c : pin)
    {
        if (c < ' ' || c > '~')
        {
            return false;
        }
    }
    return true;
}

std::string_view find_cookie(std::string_view headers, std::string_view name)
{
    constexpr std::string_view kHeader = "\r\ncookie:";
    // Header names are case-insensitive; scan for "Cookie:" in any case.
    for (size_t at = 0U; at + kHeader.size() <= headers.size(); ++at)
    {
        bool match = true;
        for (size_t i = 0U; i < kHeader.size(); ++i)
        {
            char c = headers[at + i];
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
            if (c != kHeader[i])
            {
                match = false;
                break;
            }
        }
        if (!match)
        {
            continue;
        }

        std::string_view line = headers.substr(at + kHeader.size());
        const size_t line_end = line.find("\r\n");
        if (line_end != std::string_view::npos)
        {
            line = line.substr(0U, line_end);
        }
        // "a=1; merlin_session=abc; b=2"
        while (!line.empty())
        {
            while (!line.empty() && (line.front() == ' ' || line.front() == ';'))
            {
                line.remove_prefix(1U);
            }
            const size_t pair_end = line.find(';');
            const std::string_view pair = line.substr(0U, pair_end);
            const size_t equals = pair.find('=');
            if (equals != std::string_view::npos && pair.substr(0U, equals) == name)
            {
                return pair.substr(equals + 1U);
            }
            if (pair_end == std::string_view::npos)
            {
                break;
            }
            line.remove_prefix(pair_end + 1U);
        }
    }
    return {};
}

Token token_from_bytes(const std::array<uint8_t, kTokenBytes>& bytes)
{
    constexpr char kHex[] = "0123456789abcdef";
    Token token = {};
    for (size_t i = 0U; i < bytes.size(); ++i)
    {
        token[i * 2U] = kHex[bytes[i] >> 4U];
        token[(i * 2U) + 1U] = kHex[bytes[i] & 0x0FU];
    }
    return token;
}

bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t length)
{
    unsigned difference = 0U;
    for (size_t i = 0U; i < length; ++i)
    {
        difference |= static_cast<unsigned>(a[i] ^ b[i]);
    }
    return difference == 0U;
}

void SessionTable::add(const Token& token, uint64_t now_ms)
{
    Entry* target = &entries_[0];
    for (Entry& entry : entries_)
    {
        if (!entry.used)
        {
            target = &entry;
            break;
        }
        if (entry.created_ms < target->created_ms)
        {
            target = &entry;
        }
    }
    target->used = true;
    target->token = token;
    target->created_ms = now_ms;
}

bool SessionTable::valid(std::string_view token, uint64_t now_ms) const
{
    if (token.size() != kTokenHexLength)
    {
        return false;
    }
    for (const Entry& entry : entries_)
    {
        if (entry.used && now_ms - entry.created_ms < kSessionLifetimeMs &&
            constant_time_equal(reinterpret_cast<const uint8_t*>(entry.token.data()),
                                reinterpret_cast<const uint8_t*>(token.data()), kTokenHexLength))
        {
            return true;
        }
    }
    return false;
}

void SessionTable::remove(std::string_view token)
{
    for (Entry& entry : entries_)
    {
        if (entry.used && token.size() == kTokenHexLength &&
            std::string_view(entry.token.data(), entry.token.size()) == token)
        {
            entry = {};
        }
    }
}

void SessionTable::clear()
{
    entries_ = {};
}

bool Lockout::locked(uint64_t now_ms) const
{
    return now_ms < locked_until_ms_;
}

uint32_t Lockout::seconds_left(uint64_t now_ms) const
{
    if (!locked(now_ms))
    {
        return 0U;
    }
    return static_cast<uint32_t>((locked_until_ms_ - now_ms + 999U) / 1000U);
}

void Lockout::record_failure(uint64_t now_ms)
{
    ++failures_;
    if (failures_ >= kMaxFailedAttempts)
    {
        failures_ = 0U;
        locked_until_ms_ = now_ms + kLockoutMs;
    }
}

void Lockout::record_success()
{
    failures_ = 0U;
}

} // namespace web_auth

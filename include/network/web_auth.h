#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

/// @brief Web config PIN login (#158), kept free of Pico SDK calls so host
/// tests can cover it.
/// @details With a PIN set, every page except the login page, `/ping` and
/// `/api/firmware` needs a session cookie. A correct PIN creates a session;
/// wrong PINs lock logins out for a while. The PIN is stored only as a salted
/// SHA-256 hash (hashing and randomness live in the firmware), so this module
/// handles the parts that are pure logic: which paths are public, PIN rules,
/// cookie parsing, the session table and the lockout.
namespace web_auth
{

/// @brief Shortest and longest PIN or password accepted.
inline constexpr size_t kMinPinLength = 4U;
inline constexpr size_t kMaxPinLength = 32U;

/// @brief Session cookie name and lifetime.
inline constexpr std::string_view kSessionCookie = "merlin_session";
inline constexpr uint64_t kSessionLifetimeMs = 12ULL * 60ULL * 60ULL * 1000ULL;

/// @brief Random bytes per session token; the cookie carries them as hex.
inline constexpr size_t kTokenBytes = 16U;
inline constexpr size_t kTokenHexLength = kTokenBytes * 2U;

/// @brief Browsers that can be logged in at once; a new login past this
/// replaces the oldest session.
inline constexpr size_t kMaxSessions = 4U;

/// @brief Wrong PINs allowed before logins lock, and how long they lock for.
inline constexpr uint8_t kMaxFailedAttempts = 5U;
inline constexpr uint64_t kLockoutMs = 60ULL * 1000ULL;

/// @brief A session token as lower-case hex, without a terminator.
using Token = std::array<char, kTokenHexLength>;

/// @brief True for requests that never need a login: the login page itself,
/// the `/ping` health check and the read-only `/api/firmware`.
/// @param request the request line onwards, e.g. "GET /ping HTTP/1.1...".
bool is_public_request(std::string_view request);

/// @brief True when the text is acceptable as a new PIN: 4 to 32 printable
/// ASCII characters, no spaces at either end.
bool pin_is_acceptable(std::string_view pin);

/// @brief Finds a cookie's value in a request's headers.
/// @return the value, or an empty view when the cookie is absent.
std::string_view find_cookie(std::string_view headers, std::string_view name);

/// @brief Formats random bytes as a session token.
Token token_from_bytes(const std::array<uint8_t, kTokenBytes>& bytes);

/// @brief Compares two byte runs without stopping at the first difference.
bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t length);

/// @brief The logged-in browsers.
class SessionTable
{
  public:
    /// @brief Adds a session, replacing the oldest when the table is full.
    void add(const Token& token, uint64_t now_ms);

    /// @brief True when the token names a live session.
    bool valid(std::string_view token, uint64_t now_ms) const;

    /// @brief Ends one session (logout).
    void remove(std::string_view token);

    /// @brief Ends every session, e.g. when the PIN changes.
    void clear();

  private:
    struct Entry
    {
        bool used = false;
        Token token = {};
        uint64_t created_ms = 0U;
    };
    std::array<Entry, kMaxSessions> entries_ = {};
};

/// @brief Counts wrong PINs and locks logins after too many.
class Lockout
{
  public:
    /// @brief True while logins are refused.
    bool locked(uint64_t now_ms) const;

    /// @brief Seconds left on the lock, rounded up; 0 when not locked.
    uint32_t seconds_left(uint64_t now_ms) const;

    /// @brief Records a wrong PIN; the fifth in a row starts the lock.
    void record_failure(uint64_t now_ms);

    /// @brief Clears the count after a correct PIN.
    void record_success();

  private:
    uint8_t failures_ = 0U;
    uint64_t locked_until_ms_ = 0U;
};

} // namespace web_auth

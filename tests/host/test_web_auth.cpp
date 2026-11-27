#include "web_auth.h"

#include <string>

#include "test_framework.h"

namespace
{

web_auth::Token token_of(uint8_t fill)
{
    std::array<uint8_t, web_auth::kTokenBytes> bytes = {};
    bytes.fill(fill);
    return web_auth::token_from_bytes(bytes);
}

std::string_view view(const web_auth::Token& token)
{
    return {token.data(), token.size()};
}

} // namespace

HOST_TEST(web_auth_only_login_ping_and_firmware_status_are_public)
{
    EXPECT_TRUE(web_auth::is_public_request("GET /login HTTP/1.1\r\n"));
    EXPECT_TRUE(web_auth::is_public_request("POST /login HTTP/1.1\r\n"));
    EXPECT_TRUE(web_auth::is_public_request("GET /ping HTTP/1.1\r\n"));
    EXPECT_TRUE(web_auth::is_public_request("GET /api/firmware HTTP/1.1\r\n"));
    EXPECT_TRUE(web_auth::is_public_request("GET /api/firmware?x=1 HTTP/1.1\r\n"));
    EXPECT_TRUE(web_auth::is_public_request("GET /api/status.json HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("POST /api/status.json HTTP/1.1\r\n"));

    EXPECT_FALSE(web_auth::is_public_request("GET / HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("GET /config HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("POST /config HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("GET /preview HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("POST /api/button HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("GET /api/framebuffer HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("POST /ping HTTP/1.1\r\n"));
    // A longer path that starts like a public one is not public.
    EXPECT_FALSE(web_auth::is_public_request("GET /pinger HTTP/1.1\r\n"));
    EXPECT_FALSE(web_auth::is_public_request("GET /loginx HTTP/1.1\r\n"));
}

HOST_TEST(web_auth_pin_rules)
{
    EXPECT_TRUE(web_auth::pin_is_acceptable("1234"));
    EXPECT_TRUE(web_auth::pin_is_acceptable("correct horse"));
    EXPECT_FALSE(web_auth::pin_is_acceptable("123"));
    EXPECT_FALSE(web_auth::pin_is_acceptable(" 1234"));
    EXPECT_FALSE(web_auth::pin_is_acceptable("1234 "));
    EXPECT_FALSE(web_auth::pin_is_acceptable("12\n34"));
    EXPECT_FALSE(web_auth::pin_is_acceptable(std::string(33U, 'a')));
    EXPECT_TRUE(web_auth::pin_is_acceptable(std::string(32U, 'a')));
}

HOST_TEST(web_auth_finds_the_session_cookie_among_others)
{
    const std::string headers = "GET / HTTP/1.1\r\nHost: merlinccu\r\n"
                                "Cookie: theme=dark; merlin_session=abc123; other=1";
    EXPECT_TRUE(web_auth::find_cookie(headers, "merlin_session") == "abc123");
    EXPECT_TRUE(web_auth::find_cookie(headers, "theme") == "dark");
    EXPECT_TRUE(web_auth::find_cookie(headers, "missing").empty());

    const std::string lower = "GET / HTTP/1.1\r\ncookie: merlin_session=xyz\r\nHost: a";
    EXPECT_TRUE(web_auth::find_cookie(lower, "merlin_session") == "xyz");

    // A cookie whose name merely ends with ours does not count.
    const std::string lookalike = "GET / HTTP/1.1\r\nCookie: not_merlin_session=bad";
    EXPECT_TRUE(web_auth::find_cookie(lookalike, "merlin_session").empty());
    EXPECT_TRUE(web_auth::find_cookie("GET / HTTP/1.1", "merlin_session").empty());
}

HOST_TEST(web_auth_sessions_expire_and_end_on_logout)
{
    web_auth::SessionTable sessions;
    const web_auth::Token token = token_of(0xA5U);
    EXPECT_FALSE(sessions.valid(view(token), 0U));

    sessions.add(token, 1000U);
    EXPECT_TRUE(sessions.valid(view(token), 1000U));
    EXPECT_TRUE(sessions.valid(view(token), 1000U + web_auth::kSessionLifetimeMs - 1U));
    EXPECT_FALSE(sessions.valid(view(token), 1000U + web_auth::kSessionLifetimeMs));
    EXPECT_FALSE(sessions.valid("short", 1000U));
    EXPECT_FALSE(sessions.valid(view(token_of(0x5AU)), 1000U));

    sessions.remove(view(token));
    EXPECT_FALSE(sessions.valid(view(token), 1000U));
}

HOST_TEST(web_auth_a_full_session_table_replaces_the_oldest)
{
    web_auth::SessionTable sessions;
    for (uint8_t i = 0U; i < web_auth::kMaxSessions; ++i)
    {
        sessions.add(token_of(i), 100U + i);
    }
    sessions.add(token_of(0xEEU), 500U);
    EXPECT_FALSE(sessions.valid(view(token_of(0U)), 600U));
    EXPECT_TRUE(sessions.valid(view(token_of(1U)), 600U));
    EXPECT_TRUE(sessions.valid(view(token_of(0xEEU)), 600U));

    sessions.clear();
    EXPECT_FALSE(sessions.valid(view(token_of(0xEEU)), 600U));
}

HOST_TEST(web_auth_five_wrong_pins_lock_logins_for_a_minute)
{
    web_auth::Lockout lockout;
    for (uint8_t i = 0U; i + 1U < web_auth::kMaxFailedAttempts; ++i)
    {
        lockout.record_failure(1000U);
    }
    EXPECT_FALSE(lockout.locked(1000U));

    lockout.record_failure(1000U);
    EXPECT_TRUE(lockout.locked(1000U));
    EXPECT_EQ(lockout.seconds_left(1000U), 60U);
    EXPECT_TRUE(lockout.locked(1000U + web_auth::kLockoutMs - 1U));
    EXPECT_FALSE(lockout.locked(1000U + web_auth::kLockoutMs));

    // A correct PIN clears the count.
    lockout.record_failure(70000U);
    lockout.record_success();
    for (uint8_t i = 0U; i + 1U < web_auth::kMaxFailedAttempts; ++i)
    {
        lockout.record_failure(70000U);
    }
    EXPECT_FALSE(lockout.locked(70000U));
}

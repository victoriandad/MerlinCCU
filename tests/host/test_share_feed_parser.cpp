#include "share_feed_parser.h"

#include <cstring>

#include "test_framework.h"

namespace
{

std::array<ShareWatchEntry, kMaxWatchedShares> make_watchlist(const char* symbol)
{
    std::array<ShareWatchEntry, kMaxWatchedShares> watched_shares = {};
    watched_shares[0].symbol.fill('\0');
    std::snprintf(watched_shares[0].symbol.data(), watched_shares[0].symbol.size(), "%s", symbol);
    return watched_shares;
}

} // namespace

HOST_TEST(parse_shares_feed_response_parses_a_realistic_full_response)
{
    // Matches docs/share-feed-design.md's proposed contract shape.
    const char* json =
        "{\"shares\":["
        "{\"symbol\":\"BA.L\",\"name\":\"BAE SYSTEMS\",\"exchange\":\"LSE\",\"currency\":\"GBX\","
        "\"price\":\"1372.0\",\"change\":\"+0.02%\",\"data_state\":\"live\","
        "\"history\":[1362,1364,1370,1372]}"
        "]}";
    const char* end = json + std::strlen(json);

    auto watched_shares = make_watchlist("BA.L");
    const bool updated = share_feed::parse_shares_feed_response(json, end, watched_shares, 1);

    EXPECT_TRUE(updated);
    EXPECT_TRUE(std::strcmp(watched_shares[0].display_name.data(), "BAE SYSTEMS") == 0);
    EXPECT_TRUE(std::strcmp(watched_shares[0].exchange.data(), "LSE") == 0);
    EXPECT_TRUE(std::strcmp(watched_shares[0].currency.data(), "GBX") == 0);
    EXPECT_TRUE(std::strcmp(watched_shares[0].price_text.data(), "1,372.0") == 0);
    EXPECT_TRUE(std::strcmp(watched_shares[0].change_text.data(), "+0.02%") == 0);
    EXPECT_TRUE(watched_shares[0].history_points[0] == 1362U);
    EXPECT_TRUE(watched_shares[0].history_points[watched_shares[0].history_points.size() - 1] ==
               1372U);
}

HOST_TEST(parse_shares_feed_response_accepts_a_raw_numeric_price)
{
    const char* json = "{\"shares\":[{\"symbol\":\"BA.L\",\"price\":42.5}]}";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_TRUE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
    EXPECT_TRUE(std::strcmp(watched_shares[0].price_text.data(), "42.5") == 0);
}

HOST_TEST(parse_shares_feed_response_ignores_a_symbol_not_on_the_watchlist)
{
    const char* json = "{\"shares\":[{\"symbol\":\"UNKNOWN\",\"price\":10}]}";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_FALSE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
    EXPECT_TRUE(watched_shares[0].price_text[0] == '\0');
}

HOST_TEST(parse_shares_feed_response_skips_a_non_live_data_state)
{
    const char* json = "{\"shares\":[{\"symbol\":\"BA.L\",\"price\":99,\"data_state\":\"stale\"}]}";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_FALSE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
    EXPECT_TRUE(watched_shares[0].price_text[0] == '\0');
}

HOST_TEST(parse_shares_feed_response_returns_false_for_an_empty_array)
{
    const char* json = "{\"shares\":[]}";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_FALSE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
}

HOST_TEST(parse_shares_feed_response_returns_false_without_a_shares_key)
{
    const char* json = "{\"other\":1}";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_FALSE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
}

HOST_TEST(parse_shares_feed_response_stops_cleanly_at_a_truncated_final_object)
{
    // Second object is cut off mid-response (as if the recv buffer filled
    // up) -- the first, complete share must still be parsed.
    const char* json =
        "{\"shares\":[{\"symbol\":\"BA.L\",\"price\":10},{\"symbol\":\"VOD.L\",\"price\":";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_TRUE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
    EXPECT_TRUE(std::strcmp(watched_shares[0].price_text.data(), "10.0") == 0);
}

HOST_TEST(parse_shares_feed_response_falls_back_to_price_as_the_history_graph_when_history_absent)
{
    const char* json = "{\"shares\":[{\"symbol\":\"BA.L\",\"price\":50}]}";
    const char* end = json + std::strlen(json);
    auto watched_shares = make_watchlist("BA.L");

    EXPECT_TRUE(share_feed::parse_shares_feed_response(json, end, watched_shares, 1));
    for (uint16_t point : watched_shares[0].history_points)
    {
        EXPECT_TRUE(point == 50U);
    }
}

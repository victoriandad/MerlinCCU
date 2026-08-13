#include "air_traffic_response_parser.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "test_framework.h"

HOST_TEST(parse_aircraft_candidates_parses_a_realistic_full_response)
{
    // Matches the field shape described in docs/adsb-air-traffic-feature-design.md.
    const char* json =
        "{\"ac\":["
        "{\"hex\":\"4ca87c\",\"flight\":\"BAW123  \",\"dst\":12.3,\"dir\":180.0,\"alt_baro\":5000},"
        "{\"hex\":\"406f01\",\"flight\":\"EZY456\",\"dst\":4.1,\"dir\":90.0,\"alt_baro\":\"ground\"}"
        "],\"total\":2,\"now\":1735689600}";
    const char* array_body = std::strstr(json, "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json + std::strlen(json);

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, 2);
    // Closest-first ordering: EZY456 (4.1nm) before BAW123 (12.3nm).
    EXPECT_TRUE(std::strcmp(best[0].callsign, "EZY456") == 0);
    EXPECT_TRUE(best[0].on_ground);
    EXPECT_FALSE(best[0].has_altitude);
    EXPECT_TRUE(std::strcmp(best[1].callsign, "BAW123") == 0);
    EXPECT_FALSE(best[1].on_ground);
    EXPECT_TRUE(best[1].has_altitude);
    EXPECT_TRUE(best[1].altitude_ft > 4999.0 && best[1].altitude_ft < 5001.0);
}

HOST_TEST(parse_aircraft_candidates_skips_an_object_missing_distance)
{
    const char* json = "{\"ac\":[{\"hex\":\"abc123\",\"flight\":\"NOD\"}]}";
    const char* array_body = std::strstr(json, "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json + std::strlen(json);

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, 0);
}

HOST_TEST(parse_aircraft_candidates_falls_back_to_hex_when_flight_is_blank)
{
    const char* json = "{\"ac\":[{\"hex\":\"abc123\",\"flight\":\"   \",\"dst\":1.0,\"dir\":0}]}";
    const char* array_body = std::strstr(json, "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json + std::strlen(json);

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, 1);
    EXPECT_TRUE(std::strcmp(best[0].callsign, "abc123") == 0);
}

HOST_TEST(parse_aircraft_candidates_falls_back_to_question_mark_with_no_hex_or_flight)
{
    const char* json = "{\"ac\":[{\"dst\":1.0,\"dir\":0}]}";
    const char* array_body = std::strstr(json, "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json + std::strlen(json);

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, 1);
    EXPECT_TRUE(std::strcmp(best[0].callsign, "?") == 0);
}

HOST_TEST(parse_aircraft_candidates_stops_cleanly_at_a_truncated_final_object)
{
    const char* json = "{\"ac\":[{\"hex\":\"aaa\",\"dst\":1.0},{\"hex\":\"bbb\",\"dst\":";
    const char* array_body = std::strstr(json, "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json + std::strlen(json);

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, 1);
    EXPECT_TRUE(std::strcmp(best[0].hex, "aaa") == 0);
}

HOST_TEST(parse_aircraft_candidates_returns_zero_for_an_empty_array)
{
    const char* json = "{\"ac\":[]}";
    const char* array_body = std::strstr(json, "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json + std::strlen(json);

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, 0);
}

HOST_TEST(parse_aircraft_candidates_keeps_only_the_closest_n_when_over_capacity)
{
    std::string json = "{\"ac\":[";
    const size_t total = kAirTrafficEntryCapacity + 5;
    for (size_t i = 0; i < total; ++i)
    {
        if (i > 0)
        {
            json += ",";
        }
        // Distances descend from a large value down to 1.0, so the LAST
        // entries generated are the closest and must survive eviction.
        char entry[96] = {};
        std::snprintf(entry, sizeof(entry), "{\"hex\":\"h%02zu\",\"dst\":%zu.0}", i, total - i);
        json += entry;
    }
    json += "]}";

    const char* array_body = std::strstr(json.c_str(), "\"ac\":[") + std::strlen("\"ac\":[");
    const char* buffer_end = json.c_str() + json.size();

    std::array<air_traffic_response::Candidate, kAirTrafficEntryCapacity> best = {};
    const uint8_t count =
        air_traffic_response::parse_aircraft_candidates(array_body, buffer_end, best);

    EXPECT_EQ(count, static_cast<int>(kAirTrafficEntryCapacity));
    // Closest of all (dst=1.0, the very last generated entry) must be first.
    EXPECT_TRUE(best[0].distance_nm > 0.99 && best[0].distance_nm < 1.01);
}

#include "air_traffic_response_parser.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include "bounded_json.h"

namespace air_traffic_response
{

namespace
{

/// @brief Inserts `candidate` into the bounded closest-N-by-distance array.
void insert_candidate(std::array<Candidate, kAirTrafficEntryCapacity>& best, uint8_t& count,
                      const Candidate& candidate)
{
    if (count < best.size())
    {
        best[count] = candidate;
        ++count;
    }
    else if (candidate.distance_nm < best[count - 1U].distance_nm)
    {
        best[count - 1U] = candidate;
    }
    else
    {
        return;
    }

    for (uint8_t i = count; i > 1U && best[i - 1U].distance_nm < best[i - 2U].distance_nm; --i)
    {
        std::swap(best[i - 1U], best[i - 2U]);
    }
}

} // namespace

uint8_t parse_aircraft_candidates(const char* array_body, const char* buffer_end,
                                  std::array<Candidate, kAirTrafficEntryCapacity>& best)
{
    best = {};
    uint8_t best_count = 0U;

    if (array_body == nullptr || buffer_end == nullptr)
    {
        return 0U;
    }

    const char* cursor = array_body;

    while (cursor < buffer_end)
    {
        cursor = bounded_json::skip_space(cursor);
        if (cursor >= buffer_end || *cursor == ']')
        {
            break;
        }
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }
        if (*cursor != '{')
        {
            break;
        }

        const char* obj_end = bounded_json::find_object_end(cursor, buffer_end);
        if (obj_end == nullptr)
        {
            // Truncated final object (response didn't fit) -- stop here and
            // keep whatever closer aircraft were already found.
            break;
        }

        double dst = 0.0;
        if (bounded_json::extract_bounded_number(cursor, obj_end, "\"dst\"", &dst))
        {
            Candidate candidate = {};
            candidate.distance_nm = dst;

            double dir = 0.0;
            bounded_json::extract_bounded_number(cursor, obj_end, "\"dir\"", &dir);
            candidate.bearing_deg = dir;

            bounded_json::extract_bounded_string(cursor, obj_end, "\"hex\"", candidate.hex,
                                                 sizeof(candidate.hex));

            char callsign[16] = {};
            if (bounded_json::extract_bounded_string(cursor, obj_end, "\"flight\"", callsign,
                                                      sizeof(callsign)))
            {
                size_t len = std::strlen(callsign);
                while (len > 0U && callsign[len - 1U] == ' ')
                {
                    callsign[--len] = '\0';
                }
            }
            if (callsign[0] == '\0')
            {
                std::snprintf(callsign, sizeof(callsign), "%s", candidate.hex);
            }
            std::snprintf(candidate.callsign, sizeof(candidate.callsign), "%s",
                          callsign[0] != '\0' ? callsign : "?");

            if (bounded_json::bounded_value_is_string(cursor, obj_end, "\"alt_baro\""))
            {
                candidate.on_ground = true;
                candidate.has_altitude = false;
            }
            else
            {
                double altitude = 0.0;
                candidate.has_altitude =
                    bounded_json::extract_bounded_number(cursor, obj_end, "\"alt_baro\"",
                                                          &altitude);
                candidate.altitude_ft = altitude;
            }

            insert_candidate(best, best_count, candidate);
        }

        cursor = obj_end + 1;
    }

    return best_count;
}

} // namespace air_traffic_response

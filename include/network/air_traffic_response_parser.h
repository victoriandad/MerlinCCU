#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Pure parsing of the adsb.lol/dump1090-family `{"ac":[...]}`
/// aircraft-list response, extracted from `air_traffic_manager.cpp` (issue
/// #72). No lwIP/Pico SDK dependency, so this is host-testable (see
/// tests/host/test_air_traffic_response_parser.cpp).
///
/// Deliberately NOT here: per-aircraft snail-trail history merging
/// (`merge_trail_history()` in `air_traffic_manager.cpp`) and display-text
/// formatting -- both operate on the manager's own persistent tracked-trail
/// state and stay there, since they're not response parsing.
namespace air_traffic_response
{

/// @brief One nearby aircraft parsed from the provider response.
struct Candidate
{
    char hex[8];
    char callsign[16];
    double distance_nm;
    double bearing_deg;
    double altitude_ft;
    bool on_ground;
    bool has_altitude;
};

/// @brief Parses a `"ac":[...]` aircraft array spanning `[array_body,
/// buffer_end)` (the text just past the array's opening `[`) into the
/// closest-N entries by distance, ascending, written into `best`. Returns
/// the number of candidates written. An aircraft object missing `"dst"` is
/// skipped entirely -- distance is the one field every candidate needs to be
/// ranked at all.
uint8_t parse_aircraft_candidates(const char* array_body, const char* buffer_end,
                                  std::array<Candidate, kAirTrafficEntryCapacity>& best);

} // namespace air_traffic_response

# Local Air Traffic Feature Design (ADS-B)

Written to be scoped and buildable with the existing network-manager and
rendering patterns already in the codebase, not a new architecture.

**Status (2026-07-09):** milestone (B), the compact list page, is implemented
(`air_traffic_manager.cpp`/`air_traffic_screens.cpp`, issue #74). The
radar-style visualization described below remains a possible follow-up but is
not built.

## Motivation

The CCU already integrates weather, calendar, shares, and a home-brew
scheduler via the same non-blocking network-manager pattern. A live local
air-traffic feed fits the same shape technically, and fits the *device*
thematically in a way none of the others quite do -- this is a recreation of
a Merlin helicopter's cockpit control unit, and "what's flying nearby" is the
one data feed that's actually in-character for the airframe rather than a
generic smart-home widget bolted on.

There's also a ready-made rendering foundation already in the tree:
`src/display/screensavers/screensaver_radar.cpp` draws a PPI-style radar
(concentric range rings, rotating sweep line, decaying blips) with entirely
synthetic/random data. The geometry primitives there -- polar-to-cartesian
projection via unit vectors, midpoint-circle drawing, blip rendering -- are
directly reusable for plotting *real* aircraft instead of random ones.

## Verified: the data source

Checked directly against the live API rather than assumed from training data:

- `https://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}` is a real, working,
  unauthenticated endpoint. A live query against London (51.5074, -0.1278,
  50nm radius) returned a valid response.
- Top-level shape: `{"ac": [...], "total": <count>, "now": <timestamp>, ...}`.
- Each aircraft in `ac` includes (at least): `hex` (ICAO24 address), `flight`
  (callsign), `r` (registration), `t` (aircraft type code), `alt_baro`
  (barometric altitude), `gs` (ground speed), `track` (heading),
  `lat`/`lon`, and -- importantly -- **`dst` (distance) and `dir` (bearing)
  are pre-computed by the server relative to the query point.** The Pico does
  not need to implement its own great-circle distance/bearing math; it can
  use `dst`/`dir` directly.
- adsb.lol's own repo (github.com/adsblol/api) states rate limits are
  "dynamic based on environment load" (no fixed documented number), and
  **states plainly that future access may require an API key obtained by
  contributing data to the project** (e.g. running a feeder). That's a real
  risk to a "free forever" assumption -- see Open Questions.

This confirms the endpoint shape is buildable against today, but the docs
site itself (`api.adsb.lol/docs`) is a JS-rendered Swagger UI that couldn't be
read directly -- the field list above comes from one live sample response,
not the formal schema. Worth a fuller field-by-field pass immediately before
implementation, the same way the greyscale investigation caught its own
unverified assumption before it caused a wrong decision.

## Recommended source priority

Matches the project's existing preference (see the Decision Log entry on
replacing direct Yahoo scraping with a local Home Assistant feed): prefer a
source you control over a third-party cloud API.

1. **A local dump1090/readsb receiver**, if one exists on the LAN (an RTL-SDR
   dongle is ~£20 and a common Home Assistant-adjacent hobby purchase). Same
   JSON shape family, no rate limits, no future API-key risk, fully local --
   consistent with the project's stated direction for share/calendar data.
2. **adsb.lol** as the practical default for anyone without their own
   receiver. Free, no key today, but see the API-key risk above.
3. Paid aggregators (FlightAware AeroAPI, etc.) -- not recommended; against
   the project's established "no ongoing subscription cost" pattern.

Both (1) and (2) can share one client implementation if the local-receiver
option is exposed as "just a different host/port," the same way Home
Assistant's host is configurable today.

## Proposed architecture

Follows the exact shape of `home_assistant_manager.cpp`: a non-blocking
altcp/DNS state machine with an explicit deadline checked from `update()`,
because that pattern is what the project's own history says works (and the
alternative -- another blocking fetch in the main loop -- is exactly the
class of bug two prior incidents already happened over).

- New manager: `air_traffic_manager` (`src/network/air_traffic_manager.cpp`),
  modeled on `home_assistant_manager.cpp`'s connect/request/parse/deadline
  skeleton. Plain HTTP (adsb.lol and dump1090/readsb are typically HTTP, not
  HTTPS), which is actually a simplification versus the TLS path HA/MQTT use.
- Poll interval: aircraft positions update roughly every second on the source
  side, but there's no need to refresh that fast for a small onboard display.
  A 10-15s refresh (matching `share_price_manager`'s `kRefreshIntervalMs`
  order of magnitude, though updated far more often given aircraft move much
  faster than share prices) is a reasonable starting point -- tune once real
  usage is observed, the same way other refresh intervals in this codebase
  were tuned.
- Bounded response handling: near a busy airport, `total` could be large.
  Follow `share_price_manager.cpp`'s existing pattern (`kMaxParsedHistoryValues`,
  truncation-is-failure) -- cap at some small N (8-12) closest aircraft by
  `dst`, parsed with the same bounds-checked JSON scalar extraction already
  used elsewhere (`extract_json_number`/`extract_json_string`-style helpers),
  not a new ad hoc parser.
- Config additions to `RuntimeConfig` (`config_manager.h`): enable flag, host
  (default `api.adsb.lol`), optional API key (for the future-gated case
  above), home latitude/longitude, and search radius (nm). Same shape as the
  existing HA/MQTT config fields; same example-file/gitignore secrets
  handling if a key is ever required.

## Proposed UI

Two options, not mutually exclusive:

**A. A live "Local Traffic" page, radar-style.** Reuse
`screensaver_radar.cpp`'s rendering primitives (`draw_circle`, the
sweep-vector polar projection, blip placement) but feed them real `dst`/`dir`
pairs instead of `std::rand()`. This is the more visually satisfying option
and the one that best matches the console's character, but it needs the
radar geometry pulled out of the screensaver's anonymous namespace into a
small shared header first (the same "extract the pure/reusable part" pattern
already used for `alert_ordering`/`keypad_matrix_decode`/`date_time_math`),
since a screensaver and a live data page are different `MenuPage`s with
different lifecycles.

**B. A compact list page**, following the existing `draw_compact_detail_rows`
pattern already used by Status/Resources: callsign, distance, altitude,
bearing as text rows. Cheaper to build, easier to read at a glance, less
visually interesting. Good as a first cut or a fallback if (A)'s shared-geometry
extraction turns out to be more work than it's worth.

Recommendation: build (B) first to validate the network manager and data end
to end, then upgrade to (A) once the geometry extraction is worth doing on
its own merits.

## Menu integration

A new `MenuPage::AirTraffic` (or `MenuPage::LocalTraffic`) alongside the
existing `Weather`/`Shares`/`Calendar` top-level pages, reachable from Home
the same way those are. No new architectural concept needed here.

## Open questions / risks

- **API-key risk is real, not hypothetical** -- adsb.lol's own repo says
  free access may later require contributing data. Worth designing the
  config for "host + optional key" from day one so a future gating change is
  a config edit, not a code change.
- **Coverage gaps**: not every aircraft transmits ADS-B (older GA aircraft,
  some military traffic with it deliberately disabled). This is a Merlin
  console, so there's a small irony in the real aircraft it's modeled on
  potentially being invisible to the very feed this feature would show --
  worth a line in the UI ("ADS-B traffic only") rather than implying
  completeness.
- **Distance/bearing math is NOT needed on-device** given `dst`/`dir` come
  pre-computed from the API -- this removes what would otherwise be the
  trickiest part of the feature (a `date_time_math`-style pure geo-math
  module). If a local dump1090/readsb source is used instead and doesn't
  provide these fields, that math would need adding then, not now.
- **Field list above is one live sample, not the formal schema** -- confirm
  the full field set and any documented rate limit against the interactive
  docs (which need a JS-capable fetch, not `WebFetch`) before implementation
  starts.
- **Refresh interval and response-size cap are both unvalidated guesses**
  above -- tune against real traffic density near the user's location before
  committing to numbers.

## Suggested next step

Not yet filed as a GitHub issue -- this is a design proposal, not a committed
plan. If it's worth pursuing, file it as a normal feature issue (matching the
existing `feature`/`integration`/`network` label pattern) referencing this
doc, scoped to option (B) above as the first milestone.

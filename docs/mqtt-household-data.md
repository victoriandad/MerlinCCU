# Household Data over MQTT

Home Assistant (HA) fetches household data once and publishes it to every
CCU over MQTT (#153). The CCUs need no internet access and no third-party
credentials for it. This page is the payload schema; setup is in
[home-assistant-setup.md](home-assistant-setup.md), step 6.

Today this covers weather, and each unit's Controls list (below). Share
prices stay on HA's on-demand share feed
([share-feed-design.md](share-feed-design.md)).

## Topics

All are published **retained** by HA's MerlinCCU integration
(`ha/custom_components/merlinccu/house_weather.py`), so a CCU gets the latest
data as soon as it subscribes. `<base>` is the CCUs' MQTT base topic
(default `merlinccu`); every CCU on that base shares them.

| Topic | Content | Max size |
| --- | --- | --- |
| `<base>/house/weather/now` | Current conditions, provider, sun times | 512 bytes |
| `<base>/house/weather/hourly` | Up to 24 hourly rows | 4096 bytes |
| `<base>/house/weather/daily` | Up to 7 daily rows | 1536 bytes |
| `<base>/house/weather/warnings` | Met Office and flood warnings in force (#202) | 512 bytes |

The sizes are the CCU's receive buffers (`kMaxPayloadBytes` in
`include/network/house_weather.h`). HA trims its payloads to fit; the CCU
drops a bigger one and logs it.

HA publishes the three weather topics every 5 minutes and whenever the
weather or sun entity changes, `now` first; `warnings` every 15 minutes
(`warnings_publisher.py`). It publishes nothing while the weather entity is
`unavailable` or `unknown`, so the CCUs keep the last data and its timestamp
shows them it is getting old.

## Common fields

Every payload is compact JSON (no spaces after `:`), UTF-8, with:

| Field | Type | Meaning |
| --- | --- | --- |
| `v` | integer | Schema version, `1`. The CCU refuses other versions. |
| `ts` | integer | Unix time (UTC seconds) HA built the payload. |

Other fields use the names of HA's own weather entity and forecasts, so the
CCU's existing HA weather parsers read them. Missing optional fields are
shown as `-` or `?`.

## `now`

```json
{"v":1,"ts":1763900000,"attribution":"Weather forecast from met.no, ...","state":"rainy",
 "temperature":7.2,"wind_speed":14.4,"wind_bearing":225,"temperature_unit":"°C",
 "wind_speed_unit":"km/h","next_rising":"2026-11-24T07:41:00+00:00",
 "next_setting":"2026-11-24T16:01:00+00:00"}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `state` | yes | HA weather condition, e.g. `rainy`, `partlycloudy` |
| `attribution` | no | Provider credit; the CCU shows the provider's name in the Weather footer. Dropped first if the payload is too big. |
| `temperature`, `temperature_unit` | no | Current temperature, 1 decimal place, and its unit (`°C` or `°F`) |
| `wind_speed`, `wind_speed_unit` | no | Current wind speed and its unit (`km/h`, `mph`, `m/s`, `kn`, ...) |
| `wind_bearing` | no | Degrees |
| `next_rising`, `next_setting` | no | Next sunrise and sunset from `sun.sun`, ISO 8601 |

## `hourly` and `daily`

```json
{"v":1,"ts":1763900000,"temperature_unit":"°C","wind_speed_unit":"km/h",
 "forecast":[{"datetime":"2026-11-23T18:00:00+00:00","condition":"rainy",
 "temperature":7.1,"wind_speed":14.4,"wind_bearing":225}]}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `temperature_unit`, `wind_speed_unit`, `precipitation_unit` | no | Units of the rows. Each forecast carries its own, so the topics can arrive in any order. Precipitation is `mm` or `in`; mm when missing. |
| `forecast` | yes | Rows, oldest first: 24 at most for `hourly`, 7 for `daily` |
| `forecast[].datetime` | yes | ISO 8601 start of the hour or day; a row without it is skipped |
| `forecast[].condition` | no | HA weather condition |
| `forecast[].temperature` | no | Temperature (the high, for `daily`) |
| `forecast[].templow` | no | `daily` only: the low |
| `forecast[].precipitation` | no | Rain or melted snow in the hour or day; left out when dry (#202) |
| `forecast[].wind_speed`, `forecast[].wind_bearing` | no | Wind |
| `forecast[].wind_gust_speed` | no | Gusts, when the provider gives them (#202) |

The CCU uses `precipitation`, `wind_gust_speed` and `pouring` to pick out
exceptional weather (thresholds in [alerts.md](alerts.md)). HA has no
snowfall field, so a `snowy` row's precipitation is read as snow.

## `warnings`

```json
{"v":1,"ts":1763900000,"level":"amber","summary":"AMBER WIND",
 "lines":["Amber wind until 21:00 Tue","Yellow flood alert: South Devon Coast"]}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `level` | yes | Highest level in force: `none`, `yellow`, `amber` or `red`. `none` clears the warnings. |
| `summary` | with a warning | The highest warning, upper case, at most 16 characters, e.g. `AMBER WIND`, `YELLOW RAIN/WIND`, `RED FLOOD` |
| `lines` | with a warning | Every warning in force, highest first, ASCII, at most 6 lines and 239 characters in all; the last may be `+N more` |

Met Office warnings come from the National Severe Weather Warnings RSS feed
for one region; a warning that has started reads `until` its end, one still
to come reads `from` its start, and an ended one is left out. Environment
Agency flood messages map onto the Met Office levels: flood alert = Yellow,
flood warning = Amber, severe flood warning = Red. If either source cannot be
read, HA publishes nothing that round, so the CCU keeps the last warnings
and marks them as possibly out of date after 2 hours.

## How the CCU uses it

- With **Weather source** set to Home Assistant, the CCU subscribes to
  `<base>/house/weather/+` and its weather state follows the MQTT link.
- The data's age is the CCU's clock minus `now`'s `ts` (or zero before the
  clock is set). The Weather footer shows the age, and the data counts as
  stale after 20 minutes, the same as for Open-Meteo.
- A refused payload (other version, no `ts`, no `state` or `forecast`)
  leaves the data already shown in place and is logged on the serial console.
- `warnings` are kept whichever weather source is chosen, as long as MQTT is
  set up. They raise the `OfficialWeatherWarning` alert and lead the Weather
  footer (docs/alerts.md).

## Controls (#152)

Each CCU's Controls page lists Home Assistant entities it can switch or run.
Unlike the weather, every unit has its own list, under its own prefix
`<base>/<mac>` (`<mac>` is the unit's Wi-Fi MAC, 12 lower-case hex digits;
`GET /api/firmware` shows it as `mqtt_unit=`). HA's integration
(`ha/custom_components/merlinccu/controls.py`) builds the payloads.

| Topic | Direction | Retained | Content |
| --- | --- | --- | --- |
| `<base>/<mac>/controls/list` | HA to CCU | yes | The unit's list, up to 2048 bytes |
| `<base>/<mac>/controls/press` | CCU to HA | no | `<list id>:<index>`, e.g. `5f3a9c01:3` |
| `<base>/<mac>/controls/result` | HA to CCU | no | Whether that press worked, up to 160 bytes |

### `list`

```json
{"v":1,"list":"5f3a9c01","names":["Hall light","Evening"],"kinds":["toggle","run"],
 "states":["on",""]}
```

| Field | Meaning |
| --- | --- |
| `list` | 8 hex digits identifying the entities and names. A press carries it, so HA refuses a press made against a list that has since changed. |
| `names` | What the keys say: ASCII, at most 20 characters. The CCU uppercases them and shortens one that does not fit its key. |
| `kinds` | `toggle` (lights, switches, fans, input booleans) or `run` (scenes, scripts) |
| `states` | `on`, `off` or `unavailable` for a toggle; `""` or `unavailable` for a run entity |

The three arrays have the same length, at most 24. HA republishes the list
whenever one of its entities changes state, and when HA starts.

### `result`

```json
{"v":1,"index":3,"ok":false,"error":"Service light.toggle not found"}
```

`error` is for the CCU's serial log; the panel says only that HA refused.

### On the CCU

- The CCU subscribes to `<base>/<mac>/controls/+` once connected. With no
  MQTT link the page has no keys and says why; a list cannot go stale, since
  it arrives over the same link.
- A key press shows `[Switching]` (or `[Running]`) until HA answers. A
  refusal, or no answer within 5 seconds, shows a notice on the page for 10
  seconds and raises `CONTROL FAILED` until a press works. One press at a
  time.
- A toggle that worked shows its new state at once; HA's next list confirms
  it.

## Changing the schema

Additive optional fields keep `v` at 1. Anything else (a renamed or removed
field, a changed meaning) needs a new `v`, published alongside version 1 on
new topics until every CCU runs firmware that reads it.

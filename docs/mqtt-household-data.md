# Household Data over MQTT

Home Assistant (HA) fetches household data once and publishes it to every
CCU over MQTT (#153). The CCUs need no internet access and no third-party
credentials for it. This page is the payload schema; setup is in
[home-assistant-setup.md](home-assistant-setup.md), step 6.

Today this covers weather. Share prices stay on HA's on-demand share feed
([share-feed-design.md](share-feed-design.md)).

## Topics

All are published **retained** by HA's MerlinCCU integration
(`ha/custom_components/merlinccu/house_weather.py`), so a CCU gets the latest
data as soon as it subscribes. `<base>` is the CCUs' MQTT base topic
(default `merlinccu`); every CCU on that base shares them.

| Topic | Content | Max size |
| --- | --- | --- |
| `<base>/house/weather/now` | Current conditions, provider, sun times | 512 bytes |
| `<base>/house/weather/hourly` | Up to 24 hourly rows | 3584 bytes |
| `<base>/house/weather/daily` | Up to 7 daily rows | 1536 bytes |

The sizes are the CCU's receive buffers (`kMaxPayloadBytes` in
`include/network/house_weather.h`). HA trims its payloads to fit; the CCU
drops a bigger one and logs it.

HA publishes all three every 5 minutes and whenever the weather or sun
entity changes, `now` first. It publishes nothing while the weather entity is
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
| `temperature_unit`, `wind_speed_unit` | no | Units of the rows. Each forecast carries its own, so the topics can arrive in any order. |
| `forecast` | yes | Rows, oldest first: 24 at most for `hourly`, 7 for `daily` |
| `forecast[].datetime` | yes | ISO 8601 start of the hour or day; a row without it is skipped |
| `forecast[].condition` | no | HA weather condition |
| `forecast[].temperature` | no | Temperature (the high, for `daily`) |
| `forecast[].templow` | no | `daily` only: the low |
| `forecast[].wind_speed`, `forecast[].wind_bearing` | no | Wind |

## How the CCU uses it

- With **Weather source** set to Home Assistant, the CCU subscribes to
  `<base>/house/weather/+` and its weather state follows the MQTT link.
- The data's age is the CCU's clock minus `now`'s `ts` (or zero before the
  clock is set). The Weather footer shows the age, and the data counts as
  stale after 20 minutes, the same as for Open-Meteo.
- A refused payload (other version, no `ts`, no `state` or `forecast`)
  leaves the data already shown in place and is logged on the serial console.

## Changing the schema

Additive optional fields keep `v` at 1. Anything else (a renamed or removed
field, a changed meaning) needs a new `v`, published alongside version 1 on
new topics until every CCU runs firmware that reads it.

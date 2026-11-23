"""Weather payloads Home Assistant publishes to every MerlinCCU over MQTT (#153).

Three retained topics under `<base>/house/weather/`: `now`, `hourly` and
`daily`. They keep the field names of HA's own weather entity, trimmed to what
the CCU shows, so one HA fetch serves every CCU and no CCU needs internet
access or third-party credentials. Schema: docs/mqtt-household-data.md.

Pure functions, so ha/tests covers them without Home Assistant installed.
"""

from __future__ import annotations

import json
from datetime import datetime
from typing import Any

SCHEMA_VERSION = 1

NOW = "now"
HOURLY = "hourly"
DAILY = "daily"

# The CCU's receive buffer per topic (kMaxPayloadBytes in
# include/network/house_weather.h); a bigger payload is dropped there.
MAX_PAYLOAD_BYTES = {NOW: 512, HOURLY: 3584, DAILY: 1536}

# Rows the CCU keeps (kWeatherForecastEntryCount and
# kWeatherDailyForecastEntryCount in console_model.h).
MAX_ENTRIES = {HOURLY: 24, DAILY: 7}

# Entity states that mean there is no current data to send.
_NO_DATA_STATES = {"", "unavailable", "unknown"}


def topic(base_topic: str, name: str) -> str:
    """Return the full topic of one weather payload."""
    return f"{base_topic}/house/weather/{name}"


def _encode(payload: dict[str, Any]) -> str:
    # Compact separators: the CCU's scanner looks for `"key":` with no space.
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)


def _fits(name: str, text: str) -> bool:
    return len(text.encode("utf-8")) <= MAX_PAYLOAD_BYTES[name]


def _number(value: Any, digits: int = 1) -> float | int | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return round(value) if digits == 0 else round(float(value), digits)


def _text(value: Any) -> str | None:
    if isinstance(value, datetime):
        return value.isoformat()
    if isinstance(value, str) and value:
        return value
    return None


def _units(attributes: dict[str, Any]) -> dict[str, str]:
    units = {}
    for key in ("temperature_unit", "wind_speed_unit"):
        if _text(attributes.get(key)):
            units[key] = attributes[key]
    return units


def now_payload(
    state: str | None,
    attributes: dict[str, Any],
    sun_attributes: dict[str, Any] | None,
    timestamp: int,
) -> str | None:
    """Build the `now` payload, or None when the weather entity has no data.

    Sunrise and sunset ride along from the sun entity, so the CCU needs no
    sun entity of its own.
    """
    if state is None or state in _NO_DATA_STATES:
        return None
    payload: dict[str, Any] = {"v": SCHEMA_VERSION, "ts": int(timestamp)}
    attribution = _text(attributes.get("attribution"))
    if attribution:
        payload["attribution"] = attribution
    payload["state"] = state
    for key, digits in (("temperature", 1), ("wind_speed", 1), ("wind_bearing", 0)):
        value = _number(attributes.get(key), digits)
        if value is not None:
            payload[key] = value
    payload.update(_units(attributes))
    for key in ("next_rising", "next_setting"):
        value = _text((sun_attributes or {}).get(key))
        if value:
            payload[key] = value

    text = _encode(payload)
    if not _fits(NOW, text) and "attribution" in payload:
        # The provider credit is the only long free text; drop it first.
        del payload["attribution"]
        text = _encode(payload)
    return text if _fits(NOW, text) else None


def _forecast_entry(name: str, entry: dict[str, Any]) -> dict[str, Any] | None:
    when = _text(entry.get("datetime"))
    if not when:
        return None
    out: dict[str, Any] = {"datetime": when}
    condition = _text(entry.get("condition"))
    if condition:
        out["condition"] = condition
    keys = [("temperature", 1), ("wind_speed", 1), ("wind_bearing", 0)]
    if name == DAILY:
        keys.insert(1, ("templow", 1))
    for key, digits in keys:
        value = _number(entry.get(key), digits)
        if value is not None:
            out[key] = value
    return out


def forecast_payload(
    name: str, forecast: list[dict[str, Any]] | None, attributes: dict[str, Any], timestamp: int
) -> str | None:
    """Build the `hourly` or `daily` payload, or None when there are no rows.

    Rows past what the CCU keeps are left out, and the last rows are dropped
    until the payload fits the CCU's buffer.
    """
    entries = []
    for entry in forecast or []:
        built = _forecast_entry(name, entry)
        if built is not None:
            entries.append(built)
        if len(entries) == MAX_ENTRIES[name]:
            break

    payload: dict[str, Any] = {"v": SCHEMA_VERSION, "ts": int(timestamp)}
    payload.update(_units(attributes))
    while entries:
        payload["forecast"] = entries
        text = _encode(payload)
        if _fits(name, text):
            return text
        entries = entries[:-1]
    return None

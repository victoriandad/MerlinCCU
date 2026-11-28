"""Weather alerts accepted once for every MerlinCCU (#210): the payloads.

A panel that accepts a weather alert sends its id on `<base>/house/acks/set`:
`{"alert":"wx_temp","id":"1a2b3c4d","unit":"merlinccu2"}`. The id is a
fingerprint every panel works out alike from the weather HA sends it, so the
same alert has the same id on every panel. HA records the newest id of each
kind and publishes them all, retained, on `<base>/house/weather/acks`:
`{"v":1,"official":"1a2b3c4d","wx_temp":"..."}`. A panel whose alert has the
listed id clears it. Schema: docs/mqtt-household-data.md.

No Home Assistant imports, so the tests run without it.
"""

from __future__ import annotations

import json
import re
from typing import Any

SCHEMA_VERSION = 1

# The alerts a panel can accept for every unit, as the firmware names them
# (weather_acks::name()).
KINDS = ("official", "wx_provider", "wx_temp", "wx_wind")

_ID = re.compile(r"^[0-9a-f]{8}$")
# A panel's device name: letters, digits, - and _, up to 15.
_UNIT = re.compile(r"^[A-Za-z0-9_-]{1,15}$")


def set_topic(base_topic: str) -> str:
    """Where panels send an acceptance."""
    return f"{base_topic}/house/acks/set"


def acks_topic(base_topic: str) -> str:
    """Where HA publishes the accepted ids, retained, beside the weather."""
    return f"{base_topic}/house/weather/acks"


def parse_ack(payload: str | bytes) -> tuple[str, str, str] | None:
    """An acceptance as (kind, id, unit), or None when it is not one."""
    if isinstance(payload, bytes):
        payload = payload.decode("utf-8", "replace")
    try:
        data = json.loads(payload)
    except ValueError:
        return None
    if not isinstance(data, dict):
        return None
    kind = data.get("alert")
    ack_id = data.get("id")
    unit = data.get("unit") or "unknown"
    if kind not in KINDS or not isinstance(ack_id, str) or not _ID.match(ack_id):
        return None
    if not isinstance(unit, str) or not _UNIT.match(unit):
        unit = "unknown"
    return kind, ack_id, unit


def clean(stored: Any) -> dict[str, str]:
    """The stored ids, keeping only well-formed ones of known kinds."""
    if not isinstance(stored, dict):
        return {}
    return {
        kind: value
        for kind, value in stored.items()
        if kind in KINDS and isinstance(value, str) and _ID.match(value)
    }


def acks_payload(acked: dict[str, str]) -> str:
    """The retained payload listing every accepted id."""
    body: dict[str, Any] = {"v": SCHEMA_VERSION}
    for kind in KINDS:
        if kind in acked:
            body[kind] = acked[kind]
    return json.dumps(body, separators=(",", ":"))

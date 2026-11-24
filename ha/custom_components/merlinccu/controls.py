"""Payloads for each MerlinCCU's Controls page (#152).

Pure functions, so they are tested without Home Assistant
(ha/tests/test_controls.py). The schema is in docs/mqtt-household-data.md:

- `<base>/<mac>/controls/list` (retained, HA to CCU): the unit's entities with
  their names, kinds and states, and a list id.
- `<base>/<mac>/controls/press` (CCU to HA): `<list id>:<index>`.
- `<base>/<mac>/controls/result` (HA to CCU): whether the press worked.
"""

from __future__ import annotations

import hashlib
import json
import unicodedata
from dataclasses import dataclass

SCHEMA_VERSION = 1

# The CCU's limits (include/network/home_controls.h).
MAX_CONTROLS = 24
NAME_LENGTH = 20
MAX_LIST_BYTES = 2048
LIST_ID_LENGTH = 8
MAX_ERROR_LENGTH = 80

# Domains a key switches on and off, and domains a key runs.
TOGGLE_DOMAINS = frozenset({"light", "switch", "fan", "input_boolean"})
RUN_DOMAINS = frozenset({"scene", "script"})
SUPPORTED_DOMAINS = TOGGLE_DOMAINS | RUN_DOMAINS

UNAVAILABLE_STATES = frozenset({"unavailable", "unknown"})

LIST = "list"
PRESS = "press"
RESULT = "result"


@dataclass(frozen=True)
class Control:
    """One entity on a unit's list, with the name the CCU shows."""

    entity_id: str
    name: str

    @property
    def domain(self) -> str:
        return self.entity_id.split(".", 1)[0]

    @property
    def kind(self) -> str:
        return "run" if self.domain in RUN_DOMAINS else "toggle"


def topic(base_topic: str, mac: str, name: str) -> str:
    """The topic `name` (list, press or result) for the unit with this MAC."""
    return f"{base_topic}/{mac}/controls/{name}"


def mac_from_topic(base_topic: str, topic_name: str) -> str | None:
    """The unit MAC in a `<base>/<mac>/controls/press` topic."""
    parts = topic_name.split("/")
    base_parts = base_topic.split("/")
    if (
        len(parts) != len(base_parts) + 3
        or parts[: len(base_parts)] != base_parts
        or parts[-2:] != ["controls", PRESS]
    ):
        return None
    return parts[len(base_parts)]


def clean_name(name: str, length: int = NAME_LENGTH) -> str:
    """Text the CCU can draw: ASCII, no quotes or backslashes, `length` characters.

    Accents are dropped rather than the letters (Café becomes Cafe); other
    characters the panel's fonts lack are removed.
    """
    ascii_text = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode("ascii")
    kept = "".join(c for c in ascii_text if " " <= c <= "~" and c not in '"\\')
    return " ".join(kept.split())[:length].rstrip()


def list_id(controls: list[Control]) -> str:
    """Identifies this list, so a press made against an older one is refused."""
    text = "\n".join(f"{c.entity_id}={c.name}" for c in controls)
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:LIST_ID_LENGTH]


def state_text(control: Control, state: str | None) -> str:
    """The state the CCU shows: on, off or unavailable for a toggle; a run
    entity has none of its own, so only unavailable is passed on."""
    if state is None or state in UNAVAILABLE_STATES:
        return "unavailable"
    if control.kind == "run":
        return ""
    return "on" if state == "on" else "off"


def list_payload(controls: list[Control], states: dict[str, str | None]) -> str:
    """The retained list for one unit, as compact JSON."""
    payload = {
        "v": SCHEMA_VERSION,
        "list": list_id(controls),
        "names": [c.name for c in controls],
        "kinds": [c.kind for c in controls],
        "states": [state_text(c, states.get(c.entity_id)) for c in controls],
    }
    text = json.dumps(payload, separators=(",", ":"))
    if len(text.encode("utf-8")) > MAX_LIST_BYTES:
        raise ValueError(f"controls list is {len(text)} bytes, over {MAX_LIST_BYTES}")
    return text


def parse_press(payload: str) -> tuple[str, int] | None:
    """`<list id>:<index>` as sent by the CCU, or None."""
    list_part, sep, index_part = payload.strip().partition(":")
    if not sep or len(list_part) != LIST_ID_LENGTH or not index_part.isdigit():
        return None
    return list_part, int(index_part)


def service_for(control: Control) -> tuple[str, str]:
    """The HA service a key press calls: toggle, or turn_on to run."""
    return (control.domain, "turn_on" if control.kind == "run" else "toggle")


def result_payload(index: int, ok: bool, error: str | None = None) -> str:
    """HA's answer to a press. The error is for the CCU's serial log, cut to
    fit its 160-byte result buffer."""
    payload: dict[str, object] = {"v": SCHEMA_VERSION, "index": index, "ok": ok}
    if error:
        payload["error"] = clean_name(error, MAX_ERROR_LENGTH)
    return json.dumps(payload, separators=(",", ":"))

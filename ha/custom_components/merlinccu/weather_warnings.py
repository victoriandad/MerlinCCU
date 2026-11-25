"""Official weather and flood warnings for every MerlinCCU (#202).

Reads the Met Office's National Severe Weather Warnings RSS feed for one
region and the Environment Agency's flood warnings near one point, and builds
the retained `<base>/house/weather/warnings` payload. The CCU shows the
highest warning as an alert and in the Weather page footer.

Both sources are graded Yellow, Amber or Red on the CCU. The Met Office uses
those levels itself; the Environment Agency's are mapped onto them:
flood alert = Yellow, flood warning = Amber, severe flood warning = Red.

Pure functions, so ha/tests covers them without Home Assistant installed.
Schema: docs/mqtt-household-data.md.
"""

from __future__ import annotations

import json
import re
import unicodedata
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from datetime import datetime
from typing import Any

SCHEMA_VERSION = 1

TOPIC_NAME = "warnings"

# The CCU's receive buffer for this topic (kMaxPayloadBytes in
# include/network/house_weather.h).
MAX_PAYLOAD_BYTES = 512

# The CCU's detail text holds 239 characters (WeatherWarnings::detail).
MAX_DETAIL_LENGTH = 239

# The CCU reads at most this many lines (kMaxWarningLines in house_weather.cpp).
MAX_LINES = 6

# Longest summary that fits an alert key with its time (LBL-6), e.g.
# "YELLOW RAIN/WIND"; host test official_warning_summaries_fit_an_alert_key.
MAX_SUMMARY_LENGTH = 16

# Longest flood area name kept on a line.
MAX_AREA_LENGTH = 40

NONE, YELLOW, AMBER, RED = 0, 1, 2, 3
LEVEL_NAMES = {NONE: "none", YELLOW: "yellow", AMBER: "amber", RED: "red"}

# Met Office warning regions (the RSS feed's region codes).
MET_OFFICE_REGIONS = {
    "os": "Orkney & Shetland",
    "he": "Highlands & Eilean Siar",
    "gr": "Grampian",
    "st": "Strathclyde",
    "ta": "Central, Tayside & Fife",
    "dg": "Dumfries, Galloway, Lothian & Borders",
    "ni": "Northern Ireland",
    "wl": "Wales",
    "nw": "North West England",
    "ne": "North East England",
    "yh": "Yorkshire & Humber",
    "wm": "West Midlands",
    "em": "East Midlands",
    "ee": "East of England",
    "sw": "South West England",
    "se": "London & South East England",
}

# Short names for the summary, in the Met Office's hazard words.
_HAZARD_SHORT_NAMES = {
    "wind": "WIND",
    "rain": "RAIN",
    "snow": "SNOW",
    "ice": "ICE",
    "thunderstorm": "STORM",
    "thunderstorms": "STORM",
    "lightning": "STORM",
    "fog": "FOG",
    "extreme heat": "HEAT",
    "heat": "HEAT",
}

# Environment Agency severityLevel: 1 severe flood warning, 2 flood warning,
# 3 flood alert, 4 no longer in force.
_FLOOD_LEVELS = {1: RED, 2: AMBER, 3: YELLOW}

_TITLE = re.compile(r"^\s*(yellow|amber|red)\s+warning\s+of\s+(.+?)(?:\s+affecting\b.*)?$", re.I)
# e.g. "valid from 0600 Tue 25 Nov to 2100 Tue 25 Nov"
_VALIDITY = re.compile(
    r"valid from (\d{4}) \w{3} (\d{1,2}) (\w{3}) to (\d{4}) \w{3} (\d{1,2}) (\w{3})", re.I
)
_MONTHS = {m: i for i, m in enumerate(
    ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"], 1
)}


@dataclass
class OfficialWarning:
    """One warning in force, ready to show."""

    level: int
    summary_hazard: str  # e.g. "RAIN/WIND" or "FLOOD"
    line: str  # e.g. "Amber wind until 21:00 Tue"


def met_office_url(region: str) -> str:
    """The RSS feed of one Met Office warning region."""
    return f"https://www.metoffice.gov.uk/public/data/PWSCache/WarningsRSS/Region/{region}"


def flood_url(latitude: float, longitude: float, distance_km: float) -> str:
    """Environment Agency flood warnings within `distance_km` of a point."""
    return (
        "https://environment.data.gov.uk/flood-monitoring/id/floods"
        f"?lat={latitude:.4f}&long={longitude:.4f}&dist={distance_km:g}"
    )


def _ascii(text: str) -> str:
    """The CCU's fonts are ASCII: fold accents, drop anything else."""
    folded = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii")
    return " ".join(folded.split())


def _hazards(text: str) -> list[str]:
    """Split "rain, wind" or "snow and ice" into hazard words."""
    parts = re.split(r",|\band\b", text.lower())
    return [p.strip() for p in parts if p.strip()]


def _when(day: str, month: str, hhmm: str, now: datetime) -> datetime | None:
    """A warning time in the feed's local time, in the year closest to now."""
    month_number = _MONTHS.get(month[:3].lower())
    if month_number is None:
        return None
    candidates = []
    for year in (now.year - 1, now.year, now.year + 1):
        try:
            candidates.append(
                now.replace(
                    year=year, month=month_number, day=int(day),
                    hour=int(hhmm[:2]), minute=int(hhmm[2:]), second=0, microsecond=0,
                )
            )
        except ValueError:
            continue
    if not candidates:
        return None
    return min(candidates, key=lambda c: abs(c - now))


def parse_met_office(rss_text: str, now: datetime) -> list[OfficialWarning]:
    """Warnings from one region's RSS feed that have not yet ended.

    `now` is UK local time, which the feed's times are in. A warning that has
    started reads "until <end>"; one still to come reads "from <start>".
    """
    root = ElementTree.fromstring(rss_text)
    warnings = []
    for item in root.iter("item"):
        title = _ascii(item.findtext("title") or "")
        match = _TITLE.match(title)
        if not match:
            continue
        level = {"yellow": YELLOW, "amber": AMBER, "red": RED}[match.group(1).lower()]
        hazards = _hazards(match.group(2))
        if not hazards:
            continue

        when = ""
        validity = _VALIDITY.search(_ascii(item.findtext("description") or ""))
        if validity:
            start = _when(validity.group(2), validity.group(3), validity.group(1), now)
            end = _when(validity.group(5), validity.group(6), validity.group(4), now)
            if end is not None and end < now:
                continue
            if start is not None and start > now:
                when = f" from {start:%H:%M %a}"
            elif end is not None:
                when = f" until {end:%H:%M %a}"

        short = []
        for hazard in hazards:
            name = _HAZARD_SHORT_NAMES.get(hazard, hazard.upper())
            if name not in short:
                short.append(name)
        line = f"{LEVEL_NAMES[level].capitalize()} {', '.join(hazards)}{when}"
        warnings.append(OfficialWarning(level, "/".join(short), line))
    return warnings


def parse_floods(data: dict[str, Any]) -> list[OfficialWarning]:
    """Flood alerts and warnings in force from the Environment Agency."""
    warnings = []
    for item in data.get("items") or []:
        level = _FLOOD_LEVELS.get(item.get("severityLevel"))
        if level is None:
            continue
        area = (item.get("floodArea") or {}).get("riverOrSea") or item.get("description") or ""
        area = _ascii(str(area))[:MAX_AREA_LENGTH].rstrip()
        severity = _ascii(str(item.get("severity") or "flood warning")).lower()
        line = f"{LEVEL_NAMES[level].capitalize()} {severity}"
        if area:
            line += f": {area}"
        warnings.append(OfficialWarning(level, "FLOOD", line))
    return warnings


def _summary(warning: OfficialWarning) -> str:
    """e.g. "AMBER WIND"; extra hazards that do not fit become "+"."""
    level = LEVEL_NAMES[warning.level].upper()
    hazards = warning.summary_hazard.split("/")
    text = f"{level} {'/'.join(hazards)}"
    while len(text) > MAX_SUMMARY_LENGTH and len(hazards) > 1:
        hazards = hazards[:-1]
        text = f"{level} {'/'.join(hazards)}+"
    return text[:MAX_SUMMARY_LENGTH]


def payload(warnings: list[OfficialWarning], timestamp: int) -> str:
    """The retained warnings payload: the highest level, its summary, and a
    line per warning, highest first.

    Lines that would overflow the CCU's detail text are left out, the last
    kept line making way for "+N more" when needed.
    """
    ordered = sorted(warnings, key=lambda w: -w.level)
    body: dict[str, Any] = {"v": SCHEMA_VERSION, "ts": int(timestamp)}
    if not ordered:
        body.update(level=LEVEL_NAMES[NONE], lines=[])
        return _encode(body)

    lines = [w.line for w in ordered]
    kept: list[str] = []
    for line in lines:
        if len(kept) == MAX_LINES or len("\n".join(kept + [line])) > MAX_DETAIL_LENGTH:
            break
        kept.append(line)
    if len(kept) < len(lines):
        def more() -> str:
            return f"+{len(lines) - len(kept)} more"

        while kept and (
            len(kept) >= MAX_LINES or len("\n".join(kept + [more()])) > MAX_DETAIL_LENGTH
        ):
            kept.pop()
        kept.append(more())

    body.update(level=LEVEL_NAMES[ordered[0].level], summary=_summary(ordered[0]), lines=kept)
    text = _encode(body)
    while len(text.encode("utf-8")) > MAX_PAYLOAD_BYTES and len(body["lines"]) > 1:
        body["lines"] = body["lines"][:-1]
        text = _encode(body)
    return text


def _encode(body: dict[str, Any]) -> str:
    # Compact separators: the CCU's scanner looks for `"key":` with no space.
    return json.dumps(body, separators=(",", ":"), ensure_ascii=True)


def topic(base_topic: str) -> str:
    """The retained warnings topic."""
    return f"{base_topic}/house/weather/{TOPIC_NAME}"

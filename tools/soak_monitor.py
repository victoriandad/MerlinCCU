#!/usr/bin/env python3
"""
Watch MerlinCCU units through a long soak test (#155, docs/soak-test.md).

Polls each unit's /api/firmware once a minute (by default), appends every
reading to a CSV file, and prints a line whenever something changes: a unit
stops answering or comes back, a link (Wi-Fi, MQTT, HA REST) drops or
recovers and how long it was down, a restart with its reason, or the heap
arena growing. Ctrl+C prints a summary per unit.

A unit without Wi-Fi cannot answer, so a Wi-Fi drop shows as the unit
being unreachable, with the time it took to come back.

Usage:
  python tools/soak_monitor.py merlinccu1 merlinccu2 merlinccu3
  python tools/soak_monitor.py --interval 30 --csv soak.csv 192.168.1.172
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

REQUEST_TIMEOUT_S = 5.0
# The CCU serves one request at a time; a busy answer is retried this often.
BUSY_RETRIES = 3
BUSY_DELAY_S = 1.0
LINKS = ("mqtt_connected", "ha_connected")
CSV_FIELDS = ("time", "host", "reachable", "version", "slot", "reset", "uptime_min",
              "wifi_connected", "mqtt_connected", "ha_connected", "stack_free",
              "heap_arena", "alerts")
# Reasons a restart is expected; anything else counts as a failure.
EXPECTED_RESETS = {"power_on", "reset_pin", "restart", "update"}


def parse_status(text: str) -> dict[str, str]:
    """Reads the key=value lines /api/firmware returns."""
    pairs = (line.split("=", 1) for line in text.splitlines() if "=" in line)
    return {key.strip(): value.strip() for key, value in pairs}


def fetch(host: str) -> dict[str, str] | None:
    """One reading from a unit, or None if it does not answer."""
    for _ in range(BUSY_RETRIES):
        try:
            with urllib.request.urlopen(f"http://{host}/api/firmware",
                                        timeout=REQUEST_TIMEOUT_S) as response:
                text = response.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as error:
            if error.code == 503:
                time.sleep(BUSY_DELAY_S)
                continue
            return None
        except (urllib.error.URLError, OSError):
            return None
        if "busy" in text:
            time.sleep(BUSY_DELAY_S)
            continue
        return parse_status(text)
    return None


@dataclass
class UnitLog:
    """What has been seen of one unit so far."""
    last: dict[str, str] | None = None
    reachable: bool | None = None
    down_since: dict[str, float] = field(default_factory=dict)
    restarts: list[str] = field(default_factory=list)
    outages: dict[str, list[float]] = field(default_factory=dict)
    first_heap: int | None = None
    peak_heap: int = 0


def as_int(reading: dict[str, str], key: str) -> int | None:
    """A numeric field, or None when missing (older firmware) or not a number."""
    try:
        return int(reading[key])
    except (KeyError, ValueError):
        return None


def detect_events(log: UnitLog, reading: dict[str, str] | None, now: float) -> list[str]:
    """Compares a reading with the last one and returns what changed.

    Updates `log` as it goes. Pure apart from that, so it is unit-tested.
    """
    events: list[str] = []
    key = "unit"
    if reading is None:
        if log.reachable is not False:
            events.append("unreachable")
            log.down_since[key] = now
        log.reachable = False
        return events

    if log.reachable is False:
        down = now - log.down_since.pop(key, now)
        log.outages.setdefault(key, []).append(down)
        events.append(f"reachable again after {down:.0f} s")
    log.reachable = True

    previous = log.last
    uptime = as_int(reading, "uptime_min")
    if previous is not None and uptime is not None:
        previous_uptime = as_int(previous, "uptime_min")
        if previous_uptime is not None and uptime < previous_uptime:
            reason = reading.get("reset", "unknown")
            log.restarts.append(reason)
            flag = "" if reason in EXPECTED_RESETS else " (UNEXPECTED)"
            events.append(f"restarted: {reason}{flag}, up {uptime} min")

    for link in LINKS:
        now_up = reading.get(link) == "1"
        was_up = previous is not None and previous.get(link) == "1"
        if previous is None:
            if not now_up:
                log.down_since[link] = now
            continue
        if was_up and not now_up:
            log.down_since[link] = now
            events.append(f"{link.removesuffix('_connected')} down")
        elif not was_up and now_up:
            down = now - log.down_since.pop(link, now)
            log.outages.setdefault(link, []).append(down)
            events.append(f"{link.removesuffix('_connected')} back after {down:.0f} s")

    heap = as_int(reading, "heap_arena")
    if heap is not None:
        if log.first_heap is None:
            log.first_heap = heap
        if heap > log.peak_heap and log.peak_heap > 0:
            events.append(f"heap arena grew to {heap} B (+{heap - log.first_heap} B)")
        log.peak_heap = max(log.peak_heap, heap)

    log.last = reading
    return events


def summary(host: str, log: UnitLog) -> str:
    """One paragraph per unit for the end of the run."""
    lines = [f"{host}:"]
    unexpected = [r for r in log.restarts if r not in EXPECTED_RESETS]
    lines.append(f"  restarts: {len(log.restarts)} ({len(unexpected)} unexpected"
                 f"{': ' + ', '.join(unexpected) if unexpected else ''})")
    for key, durations in sorted(log.outages.items()):
        name = key.removesuffix("_connected")
        lines.append(f"  {name} outages: {len(durations)}, longest {max(durations):.0f} s")
    for key in log.down_since:
        lines.append(f"  {key.removesuffix('_connected')} still down")
    if log.first_heap is not None:
        lines.append(f"  heap arena: {log.first_heap} B at start, peak {log.peak_heap} B")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("hosts", nargs="+", help="unit host names or IP addresses")
    parser.add_argument("--interval", type=float, default=60.0,
                        help="seconds between polls (default: 60)")
    parser.add_argument("--csv", type=Path, default=Path("soak.csv"),
                        help="file the readings are appended to (default: soak.csv)")
    args = parser.parse_args()

    logs = {host: UnitLog() for host in args.hosts}
    new_file = not args.csv.exists()
    with args.csv.open("a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS, extrasaction="ignore")
        if new_file:
            writer.writeheader()
        try:
            while True:
                started = time.monotonic()
                stamp = datetime.now().isoformat(timespec="seconds")
                for host in args.hosts:
                    reading = fetch(host)
                    row = {"time": stamp, "host": host, "reachable": int(reading is not None)}
                    row.update(reading or {})
                    writer.writerow(row)
                    for event in detect_events(logs[host], reading, time.monotonic()):
                        print(f"{stamp} {host}: {event}", flush=True)
                handle.flush()
                time.sleep(max(0.0, args.interval - (time.monotonic() - started)))
        except KeyboardInterrupt:
            print()
    for host in args.hosts:
        print(summary(host, logs[host]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

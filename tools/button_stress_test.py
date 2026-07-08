#!/usr/bin/env python3
"""
Button click latency/reliability stress test for MerlinCCU's web preview.

Repeatedly posts virtual keypad events to /api/button (the same endpoint the
browser preview uses), timing each request/response round trip. When the
device firmware includes the Pinter debug fields in /api/panel-state
(page=/brew_days=/cond_days=/crash_days=), the script also drives the Pinter
BREW TIMING screen and verifies the resulting cold-crash day count matches
the number of clicks sent -- catching dropped or duplicated clicks, not just
slow ones.

This exists to reproduce, with hard numbers, an intermittent multi-second UI
stall reported while using the web preview (see issue #18 follow-up
discussion): press a softkey, nothing visibly happens for ~10-30 seconds.

Usage:
  python tools/button_stress_test.py --host merlinccu --iterations 2000
  python tools/button_stress_test.py --host merlinccu --iterations 200 --raw-only
  python tools/button_stress_test.py --host merlinccu --iterations 2000 --csv out.csv
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass


@dataclass
class ClickResult:
    index: int
    button_id: str
    latency_s: float
    http_status: int | None
    response_text: str


def post_button(host: str, button_id: str, timeout: float) -> ClickResult:
    """POSTs one virtual key press and times the full request/response round trip."""
    url = f"http://{host}/api/button"
    body = f"id={button_id}&type=pressed".encode("ascii")
    request = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    start = time.monotonic()
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:  # nosec B310 - local device endpoint
            text = response.read().decode("utf-8", errors="replace").strip()
            status = response.status
    except urllib.error.HTTPError as exc:
        elapsed = time.monotonic() - start
        try:
            text = exc.read().decode("utf-8", errors="replace").strip()
        except OSError:
            # The connection can be reset/aborted while reading the error
            # body of a 503 -- that abort is itself useful signal (server
            # busy under back-to-back load), not a reason to crash the run.
            text = f"HTTP {exc.code} (body unreadable: connection reset)"
        return ClickResult(-1, button_id, elapsed, exc.code, text)
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        elapsed = time.monotonic() - start
        return ClickResult(-1, button_id, elapsed, None, f"ERROR: {exc}")
    elapsed = time.monotonic() - start
    return ClickResult(-1, button_id, elapsed, status, text)


def get_panel_state(host: str, timeout: float) -> tuple[float, dict[str, str]]:
    """GETs /api/panel-state; returns (latency_seconds, parsed key=value fields)."""
    url = f"http://{host}/api/panel-state?t={time.time_ns()}"
    start = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:  # nosec B310 - local device endpoint
            text = response.read().decode("utf-8", errors="replace")
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError):
        return time.monotonic() - start, {}
    elapsed = time.monotonic() - start
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            fields[key] = value
    return elapsed, fields


def navigate_to_brew_timing(host: str, timeout: float) -> dict[str, str]:
    """Drives the front panel from wherever it is to the BREW TIMING screen.

    Unwinds to Home with repeated BackStep presses (safe from any depth),
    then: Home L4 -> Pinter, select slot 1 (P1), RESET (harmless no-op if P1
    is already Idle), R1 START -> recipe catalogue, pick the first recipe ->
    BREW TIMING, with brew/cond preset to recommended and crash at 0.
    """
    for _ in range(6):
        post_button(host, "BackStep", timeout)
        time.sleep(0.05)
    post_button(host, "LeftLower", timeout)  # Home L4: GoPinter
    time.sleep(0.1)
    post_button(host, "LeftTop", timeout)  # Pinter L1: select P1
    time.sleep(0.1)
    post_button(host, "RightMiddle", timeout)  # Pinter R3: RESET (no-op if already Idle)
    time.sleep(0.1)
    post_button(host, "RightTop", timeout)  # Pinter R1: START -> recipe catalogue
    time.sleep(0.1)
    post_button(host, "LeftTop", timeout)  # catalogue L1: pick first recipe -> BREW TIMING
    time.sleep(0.1)
    _, fields = get_panel_state(host, timeout)
    return fields


def summarize(results: list[ClickResult], slow_threshold_s: float) -> None:
    latencies = [r.latency_s for r in results]
    errors = [r for r in results if r.http_status is None or r.http_status >= 400]
    slow = [r for r in results if r.latency_s >= slow_threshold_s]

    print(f"\n{len(results)} clicks sent.")
    print(f"  min={min(latencies) * 1000:.1f}ms  "
          f"median={statistics.median(latencies) * 1000:.1f}ms  "
          f"mean={statistics.mean(latencies) * 1000:.1f}ms  "
          f"max={max(latencies) * 1000:.1f}ms")
    if len(latencies) >= 20:
        quantiles = statistics.quantiles(latencies, n=100)
        print(f"  p95={quantiles[94] * 1000:.1f}ms  p99={quantiles[98] * 1000:.1f}ms")
    print(f"  errors (no response / HTTP >=400): {len(errors)}")
    print(f"  slow (>= {slow_threshold_s * 1000:.0f}ms): {len(slow)}")
    for r in slow[:20]:
        print(f"    #{r.index:5d} {r.button_id:12s} {r.latency_s * 1000:8.1f}ms  {r.response_text!r}")
    if len(slow) > 20:
        print(f"    ... and {len(slow) - 20} more")


def run_crash_toggle_stress(
    host: str, iterations: int, timeout: float, delay: float, slow_threshold_s: float
) -> list[ClickResult]:
    """Alternates CRASH+ (RightBottom) and CRASH- (LeftBottom) `iterations` times.

    Each pair should return the pending cold-crash day count to where it
    started (0 -> 1 -> 0 -> ...). When the debug fields are available, every
    single click is verified against the expected value immediately after,
    catching a dropped or duplicated click even if its own HTTP response
    looked fine.
    """
    fields = navigate_to_brew_timing(host, timeout)
    have_debug_fields = "crash_days" in fields
    if have_debug_fields:
        print(f"Reached page={fields.get('page')} "
              f"brew_days={fields.get('brew_days')} cond_days={fields.get('cond_days')} "
              f"crash_days={fields.get('crash_days')}")
        if fields.get("page") != "PinterStartTiming":
            print("WARNING: not on the BREW TIMING screen -- navigation sequence may be stale "
                  "against this firmware build. Continuing with raw latency measurement only.")
            have_debug_fields = False
    else:
        print("Device firmware does not expose the Pinter debug fields yet "
              "(needs the latest build reflashed) -- running raw latency measurement only, "
              "with no correctness verification of the resulting state.")

    results: list[ClickResult] = []
    expected_crash_days = 0
    mismatches = 0

    for i in range(iterations):
        increasing = (i % 2) == 0
        button_id = "RightBottom" if increasing else "LeftBottom"
        result = post_button(host, button_id, timeout)
        result.index = i
        results.append(result)

        if have_debug_fields:
            if increasing and expected_crash_days < 3:
                expected_crash_days += 1
            elif not increasing and expected_crash_days > 0:
                expected_crash_days -= 1

            _, verify_fields = get_panel_state(host, timeout)
            actual_text = verify_fields.get("crash_days")
            if actual_text is not None and int(actual_text) != expected_crash_days:
                mismatches += 1
                print(f"  MISMATCH after click #{i} ({button_id}): "
                      f"expected crash_days={expected_crash_days}, got {actual_text} "
                      f"(click latency {result.latency_s * 1000:.1f}ms, "
                      f"response={result.response_text!r})")

        if (i + 1) % 200 == 0:
            print(f"  ...{i + 1}/{iterations} clicks sent")

        if delay > 0:
            time.sleep(delay)

    if have_debug_fields:
        print(f"\nState-correctness check: {mismatches} mismatch(es) out of {iterations} clicks.")

    return results


def run_raw_button_stress(
    host: str, iterations: int, timeout: float, delay: float, button_id: str
) -> list[ClickResult]:
    """Repeatedly presses one harmless hard key (default Brt) with no state assertions."""
    results: list[ClickResult] = []
    for i in range(iterations):
        result = post_button(host, button_id, timeout)
        result.index = i
        results.append(result)
        if (i + 1) % 200 == 0:
            print(f"  ...{i + 1}/{iterations} clicks sent")
        if delay > 0:
            time.sleep(delay)
    return results


def write_csv(path: str, results: list[ClickResult]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["index", "button_id", "latency_ms", "http_status", "response_text"])
        for r in results:
            writer.writerow([r.index, r.button_id, f"{r.latency_s * 1000:.2f}", r.http_status, r.response_text])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="merlinccu", help="Device hostname or IP (default: merlinccu)")
    parser.add_argument("--iterations", type=int, default=2000, help="Number of clicks to send (default: 2000)")
    parser.add_argument("--timeout", type=float, default=5.0, help="Per-request timeout in seconds (default: 5)")
    parser.add_argument("--delay", type=float, default=0.0,
                        help="Extra delay between clicks in seconds (default: 0, back-to-back)")
    parser.add_argument("--slow-threshold-ms", type=float, default=250.0,
                        help="Latency (ms) at/above which a click is flagged as slow (default: 250)")
    parser.add_argument("--raw-only", action="store_true",
                        help="Skip Pinter navigation/verification; just hammer a harmless hard key (Brt)")
    parser.add_argument("--csv", default=None, help="Optional path to write a raw per-click CSV log")
    args = parser.parse_args()

    print(f"Target: http://{args.host}  iterations={args.iterations}  "
          f"timeout={args.timeout}s  delay={args.delay}s")

    if args.raw_only:
        results = run_raw_button_stress(args.host, args.iterations, args.timeout, args.delay, "Brt")
    else:
        results = run_crash_toggle_stress(
            args.host, args.iterations, args.timeout, args.delay, args.slow_threshold_ms / 1000.0
        )

    summarize(results, args.slow_threshold_ms / 1000.0)

    if args.csv:
        write_csv(args.csv, results)
        print(f"\nRaw per-click log written to {args.csv}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

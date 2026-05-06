#!/usr/bin/env python3
"""
Run multiple framebuffer regression checks defined in a JSON suite file.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from framebuffer_regression import command_capture, command_compare


def load_suite(path: Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(f"Suite file not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if "checks" not in data or not isinstance(data["checks"], list):
        raise ValueError("Suite file must contain a checks array")
    return data


def run_capture_all(suite: dict, override_host: str | None) -> int:
    host = override_host or suite.get("host", "merlinccu")
    checks = suite["checks"]
    for check in checks:
        name = check.get("name", check["baseline"])
        prompt = check.get("capture_prompt", f"Set display to '{name}'")
        print(f"\n== {name} ==")
        print(prompt)
        try:
            input("Press Enter to capture this baseline...")
        except EOFError:
            print("Capture aborted: interactive input not available.")
            return 2
        settle_seconds = float(check.get("settle_seconds", 1.0))
        if settle_seconds > 0.0:
            print(f"Waiting {settle_seconds:.1f}s for display to settle...")
            time.sleep(settle_seconds)
        args = argparse.Namespace(host=host, out=check["baseline"])
        rc = command_capture(args)
        if rc != 0:
            print(f"Capture failed for check: {name}")
            return rc
    print(f"Captured {len(checks)} baseline(s).")
    return 0


def run_compare_all(suite: dict, override_host: str | None) -> int:
    host = override_host or suite.get("host", "merlinccu")
    checks = suite["checks"]
    failures = 0
    errors = 0
    for check in checks:
        name = check.get("name", check["baseline"])
        masks = [tuple(mask) for mask in check.get("masks", [])]
        args = argparse.Namespace(host=host, baseline=check["baseline"], mask=masks)
        print(f"\n== {name} ==")
        rc = command_compare(args)
        if rc == 1:
            failures += 1
        elif rc == 2:
            errors += 1
    if errors > 0:
        print(f"\nFAILED: {errors} setup error(s), {failures} visual regression(s)")
        return 2
    if failures > 0:
        print(f"\nFAILED: {failures} visual regression(s)")
        return 1
    print(f"\nPASS: {len(checks)} check(s)")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MerlinCCU framebuffer regression suite runner")
    parser.add_argument(
        "--suite",
        default="tools/framebuffer_suite.json",
        help="Path to suite JSON file",
    )
    parser.add_argument(
        "--host",
        default=None,
        help="Optional host override (defaults to suite host)",
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("capture-all", help="Capture all baselines declared in the suite")
    sub.add_parser("compare-all", help="Compare all checks declared in the suite")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    suite = load_suite(Path(args.suite))
    if args.command == "capture-all":
        return run_capture_all(suite, args.host)
    if args.command == "compare-all":
        return run_compare_all(suite, args.host)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

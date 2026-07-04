#!/usr/bin/env python3
"""No-target repository checks for MerlinCCU.

These checks are deliberately lightweight. They validate repository data and
test metadata that can drift during safe architecture/documentation work when
the Pico hardware is not available.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


EXPECTED_FRAMEBUFFER_SIZE = (252, 320)


@dataclass(frozen=True)
class CheckResult:
    name: str
    passed: bool
    detail: str


class CheckFailure(Exception):
    """Raised when a host-side check fails."""


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def parse_pbm(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    first_newline = data.find(b"\n")
    if first_newline < 0:
        raise CheckFailure(f"{path}: missing PBM magic line")
    second_newline = data.find(b"\n", first_newline + 1)
    if second_newline < 0:
        raise CheckFailure(f"{path}: missing PBM dimension line")

    magic = data[:first_newline].decode("ascii", errors="strict").strip()
    if magic != "P4":
        raise CheckFailure(f"{path}: expected binary PBM P4, got {magic!r}")

    dimension_text = data[first_newline + 1 : second_newline].decode(
        "ascii", errors="strict"
    )
    dimensions = dimension_text.split()
    if len(dimensions) != 2:
        raise CheckFailure(f"{path}: malformed PBM dimensions")

    try:
        width = int(dimensions[0])
        height = int(dimensions[1])
    except ValueError as exc:
        raise CheckFailure(f"{path}: non-integer PBM dimensions") from exc

    payload_size = len(data) - (second_newline + 1)
    expected_payload_size = ((width + 7) // 8) * height
    if payload_size != expected_payload_size:
        raise CheckFailure(
            f"{path}: payload is {payload_size} bytes, expected {expected_payload_size}"
        )

    return width, height, payload_size


def load_framebuffer_suite(repo_root: Path) -> dict:
    suite_path = repo_root / "tools" / "framebuffer_suite.json"
    if not suite_path.exists():
        raise CheckFailure("tools/framebuffer_suite.json is missing")

    try:
        suite = json.loads(suite_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CheckFailure(f"{suite_path}: invalid JSON: {exc}") from exc

    checks = suite.get("checks")
    if not isinstance(checks, list) or not checks:
        raise CheckFailure(f"{suite_path}: checks must be a non-empty array")

    return suite


def validate_mask(mask: object, width: int, height: int, context: str) -> None:
    if not isinstance(mask, list) or len(mask) != 4:
        raise CheckFailure(f"{context}: mask must be [x, y, width, height]")
    if not all(isinstance(value, int) for value in mask):
        raise CheckFailure(f"{context}: mask values must be integers")

    x, y, mask_width, mask_height = mask
    if mask_width <= 0 or mask_height <= 0:
        raise CheckFailure(f"{context}: mask width and height must be positive")
    if x < 0 or y < 0 or x >= width or y >= height:
        raise CheckFailure(f"{context}: mask origin is outside framebuffer")
    if x + mask_width > width or y + mask_height > height:
        raise CheckFailure(f"{context}: mask extends outside framebuffer")


def check_framebuffer_suite(repo_root: Path) -> CheckResult:
    suite = load_framebuffer_suite(repo_root)
    seen_names: set[str] = set()
    seen_baselines: set[Path] = set()

    for index, check in enumerate(suite["checks"]):
        if not isinstance(check, dict):
            raise CheckFailure(f"framebuffer check {index}: entry must be an object")

        name = check.get("name")
        if not isinstance(name, str) or not name:
            raise CheckFailure(f"framebuffer check {index}: name is required")
        if name in seen_names:
            raise CheckFailure(f"framebuffer check {index}: duplicate name {name!r}")
        seen_names.add(name)

        baseline_text = check.get("baseline")
        if not isinstance(baseline_text, str) or not baseline_text:
            raise CheckFailure(f"framebuffer check {name}: baseline is required")

        baseline = repo_root / baseline_text
        if not baseline.exists():
            raise CheckFailure(f"framebuffer check {name}: baseline missing: {baseline_text}")
        seen_baselines.add(baseline.resolve())

        width, height, _payload_size = parse_pbm(baseline)
        if (width, height) != EXPECTED_FRAMEBUFFER_SIZE:
            raise CheckFailure(
                f"framebuffer check {name}: baseline is {width}x{height}, "
                f"expected {EXPECTED_FRAMEBUFFER_SIZE[0]}x{EXPECTED_FRAMEBUFFER_SIZE[1]}"
            )

        masks = check.get("masks", [])
        if not isinstance(masks, list):
            raise CheckFailure(f"framebuffer check {name}: masks must be an array")
        for mask_index, mask in enumerate(masks):
            validate_mask(mask, width, height, f"framebuffer check {name} mask {mask_index}")

    return CheckResult(
        "framebuffer-suite",
        True,
        f"{len(suite['checks'])} suite checks and {len(seen_baselines)} baselines validated",
    )


def tracked_files(repo_root: Path, paths: Iterable[str]) -> list[str]:
    command = ["git", "ls-files", *paths]
    completed = subprocess.run(
        command,
        cwd=repo_root,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise CheckFailure(f"git ls-files failed: {completed.stderr.strip()}")
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def check_tracked_config_files(repo_root: Path) -> CheckResult:
    tracked = tracked_files(repo_root, ["config"])
    offenders = [
        path
        for path in tracked
        if path.endswith(".h") and not path.endswith(".example.h")
    ]
    if offenders:
        offender_list = ", ".join(offenders)
        raise CheckFailure(f"local config headers must not be tracked: {offender_list}")

    example_headers = [path for path in tracked if path.endswith(".example.h")]
    if not example_headers:
        raise CheckFailure("no tracked config example headers found")

    return CheckResult(
        "tracked-config",
        True,
        f"{len(example_headers)} tracked config example headers, no local headers tracked",
    )


def check_baseline_inventory(repo_root: Path) -> CheckResult:
    baselines_dir = repo_root / "baselines"
    if not baselines_dir.exists():
        raise CheckFailure("baselines directory is missing")

    pbm_files = sorted(baselines_dir.glob("*.pbm"))
    if not pbm_files:
        raise CheckFailure("no PBM baselines found")

    sizes: set[tuple[int, int]] = set()
    for path in pbm_files:
        width, height, _payload_size = parse_pbm(path)
        sizes.add((width, height))

    if sizes != {EXPECTED_FRAMEBUFFER_SIZE}:
        raise CheckFailure(f"baseline dimensions differ from expected size: {sorted(sizes)}")

    return CheckResult(
        "baseline-inventory",
        True,
        f"{len(pbm_files)} PBM baselines at {EXPECTED_FRAMEBUFFER_SIZE[0]}x{EXPECTED_FRAMEBUFFER_SIZE[1]}",
    )


def run_checks(repo_root: Path) -> list[CheckResult]:
    checks = [
        check_tracked_config_files,
        check_baseline_inventory,
        check_framebuffer_suite,
    ]
    results: list[CheckResult] = []
    for check in checks:
        results.append(check(repo_root))
    return results


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run no-target MerlinCCU repository checks",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root, defaults to the parent of tools/",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    repo_root = args.repo_root.resolve()

    try:
        results = run_checks(repo_root)
    except CheckFailure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    for result in results:
        print(f"PASS {result.name}: {result.detail}")
    print(f"PASS: {len(results)} no-target check(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

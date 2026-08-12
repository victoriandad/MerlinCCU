#!/usr/bin/env python3
"""
Static RAM / program flash budget check for MerlinCCU (issue #15).

Parses a linked firmware's `.map` file for the same symbols
`status_screens.cpp`'s Resources page reads on-device (`__flash_binary_start`/
`__flash_binary_end`, `__end__`, and the linker's `FLASH`/`RAM` region
table), so this gives the same numbers before flashing that the device would
show after -- a repeatable, scriptable replacement for the ad hoc
`arm-none-eabi-size` runs previously pasted into docs/architecture.md's
decision log by hand.

Usage:
  python tools/check_size_budget.py
  python tools/check_size_budget.py --map build/MerlinCCU.elf.map --warn-percent 90
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


# Bytes carved out of program flash by on-device persistence stores.
# Must track config_manager::kReservedFlashBytes + pinter_store::kReservedFlashBytes;
# both are statically asserted against their real flash reservation in C++,
# there is no way to read them from a .map file alone.
RESERVED_FLASH_BYTES = (4096 * 2) + (4096 * 2)

WARN_PERCENT_DEFAULT = 90.0


class BudgetCheckError(Exception):
    """Raised when the map file can't be parsed as expected."""


@dataclass(frozen=True)
class MemoryRegion:
    name: str
    origin: int
    length: int


@dataclass(frozen=True)
class BudgetLine:
    label: str
    used_bytes: int
    budget_bytes: int

    @property
    def percent(self) -> float:
        if self.budget_bytes <= 0:
            return 100.0
        return (self.used_bytes / self.budget_bytes) * 100.0

    @property
    def free_bytes(self) -> int:
        return self.budget_bytes - self.used_bytes


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def read_map_text(map_path: Path) -> str:
    if not map_path.exists():
        raise BudgetCheckError(
            f"{map_path} does not exist -- build the firmware first "
            "(cmake --build build) so it can be linked and measured"
        )
    return map_path.read_text(encoding="utf-8", errors="replace")


def find_region(map_text: str, name: str) -> MemoryRegion:
    # Memory Configuration table row, e.g.:
    #   RAM              0x20000000         0x00080000         xrw
    pattern = rf"^{name}\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+\S*$"
    match = re.search(pattern, map_text, re.MULTILINE)
    if not match:
        raise BudgetCheckError(f"could not find '{name}' memory region in map file")
    return MemoryRegion(name, int(match.group(1), 16), int(match.group(2), 16))


def find_symbol_address(map_text: str, symbol: str) -> int:
    # Matches both direct assignment (`__end__ = .`) and PROVIDE() forms,
    # each preceded by the resolved address on the same or previous token.
    pattern = rf"(0x[0-9a-fA-F]+)\s+(?:PROVIDE\s*\()?\s*{re.escape(symbol)}\s*="
    match = re.search(pattern, map_text)
    if not match:
        raise BudgetCheckError(f"could not find symbol '{symbol}' in map file")
    return int(match.group(1), 16)


def compute_budget_lines(map_text: str) -> list[BudgetLine]:
    flash_region = find_region(map_text, "FLASH")
    ram_region = find_region(map_text, "RAM")

    flash_start = find_symbol_address(map_text, "__flash_binary_start")
    flash_end = find_symbol_address(map_text, "__flash_binary_end")
    static_ram_end = find_symbol_address(map_text, "__end__")

    program_flash_used = max(flash_end - flash_start, 0)
    program_flash_budget = flash_region.length - RESERVED_FLASH_BYTES

    static_ram_used = max(static_ram_end - ram_region.origin, 0)
    static_ram_budget = ram_region.length

    return [
        BudgetLine("Program flash", program_flash_used, program_flash_budget),
        BudgetLine("Static RAM (.data+.bss)", static_ram_used, static_ram_budget),
    ]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--map",
        type=Path,
        default=None,
        help="Path to the linked .map file (default: <repo>/build/MerlinCCU.elf.map)",
    )
    parser.add_argument(
        "--warn-percent",
        type=float,
        default=WARN_PERCENT_DEFAULT,
        help=f"Warn once usage crosses this percent of budget (default: {WARN_PERCENT_DEFAULT})",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    map_path = args.map or (repo_root_from_script() / "build" / "MerlinCCU.elf.map")

    try:
        map_text = read_map_text(map_path)
        lines = compute_budget_lines(map_text)
    except BudgetCheckError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    worst_status = "PASS"
    for line in lines:
        status = "PASS"
        if line.used_bytes > line.budget_bytes:
            status = "FAIL"
        elif line.percent >= args.warn_percent:
            status = "WARN"

        if status == "FAIL":
            worst_status = "FAIL"
        elif status == "WARN" and worst_status != "FAIL":
            worst_status = "WARN"

        print(
            f"{status} {line.label}: {line.used_bytes:,} / {line.budget_bytes:,} bytes "
            f"({line.percent:.1f}%, {line.free_bytes:,} bytes free)"
        )

    if worst_status == "FAIL":
        print("FAIL: at least one budget was exceeded -- see docs/architecture.md's "
              "\"Memory and Timing Rules\" before adding more static storage", file=sys.stderr)
        return 1

    print(f"{worst_status}: {map_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

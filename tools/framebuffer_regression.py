#!/usr/bin/env python3
"""
Framebuffer regression checker for MerlinCCU preview snapshots.

Usage examples:
  python tools/framebuffer_regression.py capture --host merlinccu --out baselines/status.pbm
  python tools/framebuffer_regression.py compare --host merlinccu --baseline baselines/status.pbm --mask 660,0,96,20
"""

from __future__ import annotations

import argparse
import sys
import urllib.request
import urllib.error
import time
from pathlib import Path


def fetch_pbm(host: str) -> bytes:
    url = f"http://{host}/api/framebuffer.pbm"
    last_error: Exception | None = None
    for attempt in range(1, 7):
        try:
            with urllib.request.urlopen(url, timeout=5) as response:  # nosec B310 - local device endpoint
                data = response.read()
            return data
        except urllib.error.HTTPError as exc:
            last_error = exc
            # 503 is expected occasionally while the embedded web server is busy.
            if exc.code != 503:
                break
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last_error = exc
        time.sleep(0.35 * attempt)

    if last_error is None:
        raise RuntimeError("Failed to fetch PBM for unknown reason")
    raise last_error


def split_pbm(data: bytes) -> tuple[bytes, bytes]:
    # PBM P4 format: magic line, size line, then binary bitmap payload.
    nl1 = data.find(b"\n")
    if nl1 < 0:
        raise ValueError("Invalid PBM: missing magic line")
    nl2 = data.find(b"\n", nl1 + 1)
    if nl2 < 0:
        raise ValueError("Invalid PBM: missing size line")
    header = data[: nl2 + 1]
    payload = data[nl2 + 1 :]
    return header, payload


def parse_size(header: bytes) -> tuple[int, int]:
    parts = header.decode("ascii", errors="strict").splitlines()
    if len(parts) < 2 or parts[0].strip() != "P4":
        raise ValueError("Invalid PBM: expected P4")
    dims = parts[1].strip().split()
    if len(dims) != 2:
        raise ValueError("Invalid PBM: malformed dimensions")
    return int(dims[0]), int(dims[1])


def clear_mask(payload: bytearray, width: int, height: int, mask: tuple[int, int, int, int]) -> None:
    x, y, w, h = mask
    if w <= 0 or h <= 0:
        return
    stride = (width + 7) // 8
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(width, x + w)
    y1 = min(height, y + h)
    for row in range(y0, y1):
        row_start = row * stride
        for col in range(x0, x1):
            byte_index = row_start + (col // 8)
            bit = 7 - (col % 8)
            payload[byte_index] &= ~(1 << bit)


def parse_mask(arg: str) -> tuple[int, int, int, int]:
    parts = arg.split(",")
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("Mask must be x,y,width,height")
    try:
        return tuple(int(p.strip()) for p in parts)  # type: ignore[return-value]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("Mask values must be integers") from exc


def command_capture(args: argparse.Namespace) -> int:
    try:
        pbm = fetch_pbm(args.host)
    except Exception as exc:
        print(f"Capture failed while fetching framebuffer: {exc}", file=sys.stderr)
        return 2
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(pbm)
    print(f"Captured baseline: {out_path}")
    return 0


def command_compare(args: argparse.Namespace) -> int:
    try:
        current = fetch_pbm(args.host)
    except Exception as exc:
        print(f"Compare failed while fetching framebuffer: {exc}", file=sys.stderr)
        return 2
    baseline_path = Path(args.baseline)
    if not baseline_path.exists():
        print(f"Baseline file not found: {baseline_path}", file=sys.stderr)
        return 2
    baseline = baseline_path.read_bytes()

    cur_header, cur_payload = split_pbm(current)
    base_header, base_payload = split_pbm(baseline)

    if cur_header != base_header:
        print("FAIL: PBM headers differ (resolution/format mismatch)")
        return 1

    width, height = parse_size(cur_header)
    cur_mut = bytearray(cur_payload)
    base_mut = bytearray(base_payload)
    for mask in args.mask:
        clear_mask(cur_mut, width, height, mask)
        clear_mask(base_mut, width, height, mask)

    if cur_mut == base_mut:
        print("PASS: framebuffer matches baseline (after masks)")
        return 0

    stride = (width + 7) // 8
    differing_indices = [
        i for i in range(min(len(cur_mut), len(base_mut))) if cur_mut[i] != base_mut[i]
    ]
    diff = len(differing_indices)
    min_x = width
    min_y = height
    max_x = 0
    max_y = 0
    for idx in differing_indices:
        y = idx // stride
        xb = idx % stride
        x0 = xb * 8
        x1 = min(width - 1, x0 + 7)
        if x0 < min_x:
            min_x = x0
        if y < min_y:
            min_y = y
        if x1 > max_x:
            max_x = x1
        if y > max_y:
            max_y = y

    print(f"FAIL: framebuffer differs from baseline, bytes changed: {diff}")
    if diff > 0:
        box_w = (max_x - min_x) + 1
        box_h = (max_y - min_y) + 1
        print(f"Diff bounding box: x={min_x}, y={min_y}, w={box_w}, h={box_h}")
        print(f"Suggested mask: --mask {min_x},{min_y},{box_w},{box_h}")
    return 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MerlinCCU framebuffer regression checker")
    sub = parser.add_subparsers(dest="command", required=True)

    cap = sub.add_parser("capture", help="Capture current framebuffer PBM as baseline")
    cap.add_argument("--host", default="merlinccu", help="Device host name or IP")
    cap.add_argument("--out", required=True, help="Output baseline PBM path")
    cap.set_defaults(func=command_capture)

    cmp_parser = sub.add_parser("compare", help="Compare current framebuffer against baseline")
    cmp_parser.add_argument("--host", default="merlinccu", help="Device host name or IP")
    cmp_parser.add_argument("--baseline", required=True, help="Baseline PBM path")
    cmp_parser.add_argument(
        "--mask",
        action="append",
        type=parse_mask,
        default=[],
        help="Mask rectangle x,y,width,height (repeatable)",
    )
    cmp_parser.set_defaults(func=command_compare)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Update one or more MerlinCCU units over Wi-Fi (#154, docs/ota.md).

For each unit in turn: upload the firmware .bin to /api/ota, wait for the
unit to restart into the new slot, and wait until it reports the new build
as confirmed. Stops at the first unit that fails, so one bad build is never
rolled out to every unit.

The token comes from --token, the MERLIN_OTA_TOKEN environment variable, or
config/ota_credentials.h, in that order. The expected version is read from
the build folder's generated firmware_version.h next to the .bin, so the
script can tell the unit really runs the new build.

Usage:
  python tools/ota_update.py merlinccu merlin-ground merlin-loft
  python tools/ota_update.py --bin build-plus2/MerlinCCU.bin 192.168.1.172
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BIN = REPO_ROOT / "build-plus2" / "MerlinCCU.bin"
CREDENTIALS = REPO_ROOT / "config" / "ota_credentials.h"

# The CCU serves one request at a time and answers 503 while busy.
BUSY_RETRIES = 15
BUSY_DELAY_S = 1.0
# Writing ~800 KB takes about 17 s; leave plenty of room.
UPLOAD_TIMEOUT_S = 120.0
# A trial image confirms after 15 s with Wi-Fi up, or rolls back by 90 s.
CONFIRM_TIMEOUT_S = 150.0
POLL_INTERVAL_S = 3.0
REQUEST_TIMEOUT_S = 5.0


def read_token(cli_token: str | None) -> str:
    """Finds the upload token: argument, environment, then credentials file."""
    if cli_token:
        return cli_token
    if os.environ.get("MERLIN_OTA_TOKEN"):
        return os.environ["MERLIN_OTA_TOKEN"]
    if CREDENTIALS.exists():
        match = re.search(r'kOtaUploadToken\[\]\s*=\s*"([^"]*)"', CREDENTIALS.read_text())
        if match and match.group(1):
            return match.group(1)
    sys.exit("No upload token: pass --token, set MERLIN_OTA_TOKEN, or fill "
             "config/ota_credentials.h")


def expected_version(bin_path: Path) -> str | None:
    """Reads the version the build stamped next to the .bin, if it is there."""
    header = bin_path.parent / "merlin_generated" / "firmware_version.h"
    if not header.exists():
        return None
    match = re.search(r'kFirmwareVersion\[\]\s*=\s*"([^"]*)"', header.read_text())
    return match.group(1) if match else None


def firmware_state(host: str) -> dict[str, str] | None:
    """Reads /api/firmware as a dict, or None while the unit is unreachable."""
    try:
        with urllib.request.urlopen(f"http://{host}/api/firmware",
                                    timeout=REQUEST_TIMEOUT_S) as response:
            text = response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        if error.code == 503:
            return None
        # Firmware from before /api/firmware existed still takes uploads.
        return {"version": "unknown", "slot": "unknown", "state": "unknown"}
    except (urllib.error.URLError, OSError):
        return None
    if "busy" in text:
        return None
    pairs = (line.split("=", 1) for line in text.splitlines() if "=" in line)
    return {key.strip(): value.strip() for key, value in pairs}


def upload(host: str, image: bytes, token: str) -> tuple[bool, str]:
    """Posts the image, retrying while the unit's web server is busy."""
    request = urllib.request.Request(
        f"http://{host}/api/ota", data=image, method="POST",
        headers={"X-OTA-Token": token, "Content-Type": "application/octet-stream"})
    for _ in range(BUSY_RETRIES):
        try:
            with urllib.request.urlopen(request, timeout=UPLOAD_TIMEOUT_S) as response:
                return True, response.read().decode("utf-8", "replace").strip()
        except urllib.error.HTTPError as error:
            text = error.read().decode("utf-8", "replace").strip()
            if error.code == 503:
                time.sleep(BUSY_DELAY_S)
                continue
            return False, text
        except (urllib.error.URLError, OSError) as error:
            return False, f"upload failed: {error}"
    return False, "unit stayed busy"


def update_unit(host: str, image: bytes, token: str, version: str | None) -> bool:
    """Updates one unit and waits for it to confirm. Returns True on success."""
    before = None
    for _ in range(BUSY_RETRIES):
        before = firmware_state(host)
        if before is not None:
            break
        time.sleep(BUSY_DELAY_S)
    if before is None:
        print(f"{host}: not reachable")
        return False
    print(f"{host}: running {before.get('version')} in slot {before.get('slot')} "
          f"({before.get('state')})")
    if before.get("state") == "not_partitioned":
        print(f"{host}: no A/B partition table; move it to the A/B layout over USB first")
        return False

    started = time.monotonic()
    ok, reply = upload(host, image, token)
    print(f"{host}: {reply}")
    if not ok:
        return False

    deadline = time.monotonic() + CONFIRM_TIMEOUT_S
    last_state = None
    while time.monotonic() < deadline:
        time.sleep(POLL_INTERVAL_S)
        state = firmware_state(host)
        if state is None:
            continue
        summary = (state.get("version"), state.get("slot"), state.get("state"))
        if summary != last_state:
            print(f"{host}: {state.get('version')} in slot {state.get('slot')} "
                  f"({state.get('state')})")
            last_state = summary
        if state.get("state") == "rolled_back":
            print(f"{host}: FAILED - the new firmware did not confirm; back on the old one")
            return False
        moved = state.get("slot") != before.get("slot")
        if state.get("state") == "confirmed" and moved:
            if version is not None and state.get("version") != version:
                print(f"{host}: FAILED - running {state.get('version')}, expected {version}")
                return False
            print(f"{host}: done in {time.monotonic() - started:.0f} s")
            return True
    print(f"{host}: FAILED - no confirmation within {CONFIRM_TIMEOUT_S:.0f} s")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("hosts", nargs="+", help="unit host names or IP addresses")
    parser.add_argument("--bin", type=Path, default=DEFAULT_BIN,
                        help=f"firmware image (default: {DEFAULT_BIN.relative_to(REPO_ROOT)})")
    parser.add_argument("--token", help="upload token (default: config/ota_credentials.h)")
    args = parser.parse_args()

    if args.bin.suffix != ".bin":
        sys.exit(f"{args.bin} is not a .bin image; the CCU refuses .uf2 and .elf files")
    image = args.bin.read_bytes()
    token = read_token(args.token)
    version = expected_version(args.bin)
    print(f"Image {args.bin} ({len(image) // 1024} KB), version {version or 'unknown'}")

    for host in args.hosts:
        if not update_unit(host, image, token, version):
            remaining = args.hosts[args.hosts.index(host) + 1:]
            if remaining:
                print(f"Stopped; not updated: {', '.join(remaining)}")
            return 1
    print("All units updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

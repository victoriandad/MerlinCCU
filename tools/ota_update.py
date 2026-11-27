#!/usr/bin/env python3
"""
Update one or more MerlinCCU units over Wi-Fi (#154, docs/ota.md).

For each unit in turn: pick the image built for the unit's board, upload it
to /api/ota, wait for the unit to restart into the new slot, and wait until
it reports the new build as confirmed. Stops at the first unit whose update
fails, so one bad build is never rolled out to every unit. A unit that is
offline, not yet on the A/B layout, or on a board no image is built for is
skipped and listed at the end (#210).

Images: every --bin given, or by default each of build-plus2/ and build/
that has been built. The version and board of each image are read from the
build folder's generated firmware_version.h next to the .bin. A unit reports
its board in /api/firmware; firmware from before that line existed can only
be updated with a single --bin, which is then trusted.

The token comes from --token, the MERLIN_OTA_TOKEN environment variable, or
config/ota_credentials.h, in that order.

Usage:
  python tools/ota_update.py merlinccu1 merlinccu2 merlinccu3
  python tools/ota_update.py --bin build-plus2/MerlinCCU.bin 192.168.1.172
"""

from __future__ import annotations

import argparse
import enum
import os
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BINS = (REPO_ROOT / "build-plus2" / "MerlinCCU.bin",
                REPO_ROOT / "build" / "MerlinCCU.bin")
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


@dataclass
class Image:
    """One firmware image and what its build stamped."""
    path: Path
    data: bytes
    version: str | None
    board: str | None


class Outcome(enum.Enum):
    UPDATED = "updated"
    SKIPPED = "skipped"
    FAILED = "failed"


def stamped(bin_path: Path, name: str) -> str | None:
    """Reads a constant the build stamped next to the .bin, if it is there."""
    header = bin_path.parent / "merlin_generated" / "firmware_version.h"
    if not header.exists():
        return None
    match = re.search(name + r'\[\]\s*=\s*"([^"]*)"', header.read_text())
    return match.group(1) if match else None


def load_image(bin_path: Path) -> Image:
    """Reads an image and its stamped version and board."""
    if bin_path.suffix != ".bin":
        sys.exit(f"{bin_path} is not a .bin image; the CCU refuses .uf2 and .elf files")
    return Image(bin_path, bin_path.read_bytes(), stamped(bin_path, "kFirmwareVersion"),
                 stamped(bin_path, "kFirmwareBoard"))


def choose_image(host: str, board: str | None, images: list[Image]) -> Image | None:
    """Picks the image for a unit's board, or None (with the reason printed)."""
    if board is None:
        if len(images) == 1:
            print(f"{host}: does not report its board; using {images[0].path} as given")
            return images[0]
        print(f"{host}: does not report its board; update it once with a single --bin")
        return None
    for image in images:
        if image.board == board:
            return image
    built = ", ".join(f"{image.board or 'unknown'} ({image.path})" for image in images)
    print(f"{host}: board {board} but the images are for {built}; not updated")
    return None


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
    """Posts the image, retrying while the unit's web server is busy.

    A busy unit (a browser page polling it, say) answers 503 and closes the
    connection while the image is still being sent, so the send can fail
    with the connection reset instead. Both are retried: a cut-off upload is
    abandoned by the unit with its running image untouched.
    """
    request = urllib.request.Request(
        f"http://{host}/api/ota", data=image, method="POST",
        headers={"X-OTA-Token": token, "Content-Type": "application/octet-stream"})
    last_error = "unit stayed busy"
    for _ in range(BUSY_RETRIES):
        try:
            with urllib.request.urlopen(request, timeout=UPLOAD_TIMEOUT_S) as response:
                return True, response.read().decode("utf-8", "replace").strip()
        except urllib.error.HTTPError as error:
            if error.code != 503:
                try:
                    return False, error.read().decode("utf-8", "replace").strip()
                except OSError:
                    return False, f"upload refused: HTTP {error.code}"
            last_error = "unit stayed busy"
        except (urllib.error.URLError, OSError) as error:
            last_error = f"upload failed: {error}"
        time.sleep(BUSY_DELAY_S)
    return False, last_error


def update_unit(host: str, images: list[Image], token: str) -> Outcome:
    """Updates one unit with its board's image and waits for it to confirm."""
    before = None
    for _ in range(BUSY_RETRIES):
        before = firmware_state(host)
        if before is not None:
            break
        time.sleep(BUSY_DELAY_S)
    if before is None:
        print(f"{host}: not reachable")
        return Outcome.SKIPPED
    print(f"{host}: running {before.get('version')} in slot {before.get('slot')} "
          f"({before.get('state')})")
    if before.get("state") == "not_partitioned":
        print(f"{host}: no A/B partition table; move it to the A/B layout over USB first")
        return Outcome.SKIPPED
    image = choose_image(host, before.get("board"), images)
    if image is None:
        return Outcome.SKIPPED
    version = image.version

    started = time.monotonic()
    ok, reply = upload(host, image.data, token)
    print(f"{host}: {reply}")
    if not ok:
        return Outcome.FAILED

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
            return Outcome.FAILED
        moved = state.get("slot") != before.get("slot")
        if state.get("state") == "confirmed" and moved:
            if version is not None and state.get("version") != version:
                print(f"{host}: FAILED - running {state.get('version')}, expected {version}")
                return Outcome.FAILED
            print(f"{host}: done in {time.monotonic() - started:.0f} s")
            return Outcome.UPDATED
    print(f"{host}: FAILED - no confirmation within {CONFIRM_TIMEOUT_S:.0f} s")
    return Outcome.FAILED


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("hosts", nargs="+", help="unit host names or IP addresses")
    parser.add_argument("--bin", type=Path, action="append",
                        help="firmware image; repeat for several boards "
                             "(default: build-plus2/ and build/, whichever are built)")
    parser.add_argument("--token", help="upload token (default: config/ota_credentials.h)")
    args = parser.parse_args()

    bins = args.bin or [path for path in DEFAULT_BINS if path.exists()]
    if not bins:
        sys.exit("No images built: build build-plus2/ or build/, or pass --bin")
    images = [load_image(path) for path in bins]
    token = read_token(args.token)
    for image in images:
        print(f"Image {image.path} ({len(image.data) // 1024} KB), "
              f"board {image.board or 'unknown'}, version {image.version or 'unknown'}")

    skipped = []
    for index, host in enumerate(args.hosts):
        outcome = update_unit(host, images, token)
        if outcome is Outcome.FAILED:
            remaining = args.hosts[index + 1:]
            if remaining:
                print(f"Stopped; not updated: {', '.join(remaining)}")
            return 1
        if outcome is Outcome.SKIPPED:
            skipped.append(host)
    if skipped:
        print(f"Not updated: {', '.join(skipped)}")
        return 1
    print("All units updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

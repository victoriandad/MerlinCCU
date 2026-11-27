# Over-the-Air Updates

How the CCU updates its firmware over Wi-Fi without anyone opening the case
(#154): the flash layout, the update flow, and how to use it.

## Status

| Step | What | State |
| --- | --- | --- |
| 1 | Flash layout and partition table (`tools/ota_partition_table.json`) | Done |
| 2 | Upload into the spare slot over Wi-Fi, restart into it | Done |
| 3 | Try-before-you-buy confirm, rollback, `UPDATE FAILED` alert | Done |
| 4 | Firmware version on the Status page, Firmware page and in HA (MQTT) | Done |
| 5 | `tools/ota_update.py` updates every unit in turn | Done |

## Why A/B Slots

The RP2350 boot ROM understands partition tables. It can boot from one of two
linked partitions (A and B) and roll back if a new image fails. Using the ROM
means:

- no second-stage bootloader of our own to write, debug and keep working;
- a power cut at any point during an upload leaves the old image in place;
- a new image that hangs or crashes before it confirms itself is abandoned
  on the next reboot.

The alternative, a separate bootloader such as picowota, would add a second
program to every unit and take flash for itself as well as for the image.

## Flash Layout

The layout fits a standard Pico 2 W (4 MB). The Pimoroni Pico Plus 2 W
(16 MB) uses the same slots, and the extra 12 MB is free for data.

| Offset (4 MB board) | Offset (16 MB board) | Size | Contents |
| --- | --- | --- | --- |
| `0x000000` | `0x000000` | 8 KB | Partition table (first sector), spare sector |
| `0x002000` | `0x002000` | 1536 KB | Firmware slot A (partition 0) |
| `0x182000` | `0x182000` | 1536 KB | Firmware slot B (partition 1, linked to A) |
| `0x302000` | `0x302000` | 1000 KB / 13288 KB | Unpartitioned, free |
| `0x3FC000` | `0xFFC000` | 8 KB | Pinter store (`pinter_store`, two sectors) |
| `0x3FE000` | `0xFFE000` | 8 KB | Runtime settings (`config_manager`, two sectors) |

Notes:

- **Slot size.** The image is about 810 KB (code plus the embedded CYW43
  Wi-Fi firmware), so a 1.5 MB slot leaves room to grow. When a build
  passes the slot size, the next step is to move the CYW43 firmware into its
  own A/B partitions (`pico_use_wifi_firmware_partition`, about 256 KB
  each), not to shrink the data area.
- **Data stays where it is.** Settings and Pinter state are found from the
  top of flash (`PICO_FLASH_SIZE_BYTES`), so they keep the same addresses
  after the change, and an update never writes there. The unpartitioned
  area is `absolute` family with read/write access.
- **Reading data.** The boot ROM maps only the running slot at `XIP_BASE`
  (`0x10000000`), so the same build runs from A or B. Anything else in
  flash is read through the untranslated alias
  `XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE` at its physical offset; the
  settings and Pinter stores do this. Flash writes (`flash_range_program`)
  already use physical offsets.
- **Board flash size.** A build for the wrong board reads its settings from
  the wrong place: a Pico 2 W build on the Pimoroni looks at the 4 MB
  mark, not the top of its 16 MB, and starts with defaults. Upload the
  build made for the board (`build-plus2` for the Pimoroni).

## How an Update Works

1. **Upload.** `POST /api/ota` with the `.bin` file as the body and the
   token in an `X-OTA-Token` header. The Firmware page
   (`http://<unit>/firmware`) sends exactly that; so does `curl`. The token
   comes from `config/ota_credentials.h` (copy the `.example.h`); with no
   token built in, every upload is refused. It stands in until web access
   control (#158) lands, because anyone who can upload can replace the
   firmware.
2. **Checks before anything is erased** (`ota_upload.cpp`, host-tested):
   the token, a `Content-Length` that fits the spare slot, and a running
   image that is already confirmed. The first sector must hold an RP2350
   metadata block, which turns away a `.uf2`, an `.elf` or the wrong file.
3. **Writing.** `ota_manager` writes the slot the unit is not running from,
   one 4 KB sector per main-loop pass, with interrupts off for each erase
   and program (as a settings save does), then reads the sector back. The
   web server holds received data unacknowledged until it is written, so
   TCP flow control slows the sender to the flash rate. About 810 KB takes
   about 17 seconds. Any other request gets "busy" meanwhile. If the
   connection drops part-way, the upload is abandoned and the running
   image is untouched.
4. **Restart.** After the reply goes out, the CCU restarts with a
   flash-update reboot pointing at the new slot, so the boot ROM picks it.
5. **Trial.** Every image is built try-before-you-buy (TBYB) and carries its
   own SHA-256 (`pico_hash_binary`); the boot ROM refuses a damaged image
   and stays on the old one. The new image runs on trial with a watchdog
   armed from the first instruction of `main()`.
6. **Confirm.** Once it has run 15 seconds and Wi-Fi has an address, it
   calls `rom_explicit_buy()` and becomes the image that boots. It then
   erases the first sector of the old image. Images carry no version
   number, so with two valid images the boot ROM's choice on a later power
   cycle would not be the confirmed one; with the old one gone, it is.
7. **Roll back.** If it crashes, hangs, loses power, or has not confirmed
   within 90 seconds, the next boot is an ordinary one. The boot ROM skips
   the unconfirmed image and runs the old one. A record kept in a POWMAN
   scratch register (it survives resets but not power cuts) lets that boot
   raise the `UPDATE FAILED` alert.

After confirmation there is no automatic way back: to undo an update,
upload the previous build.

### Uploading from the laptop

In VS Code, the Pico extension's **Compile** button builds the Pimoroni
image (`build-plus2`), and **Run** loads `build-plus2/MerlinCCU.uf2` over USB.
Run loads the UF2, never the ELF, so the partition table survives.

The tasks (Terminal > Run Task) build, then run the script below:

- **Upload to all units** builds both images and updates `merlinccu1`,
  `merlinccu2` and `merlinccu3` in turn, each with its own board's image.
- **Upload over Wi-Fi** updates `merlinccu3` with the Pimoroni image.
- **Upload over Wi-Fi (Pico 2 W)** updates `merlinccu2` with the Pico 2 W
  image (`build/`).

The units are named by board, oldest first; where each one hangs goes in
its location and room settings:

| Name | Board | Image |
| --- | --- | --- |
| `merlinccu1` | Pico 2 W in the real CCU | `build/` |
| `merlinccu2` | Pico 2 W, development | `build/` |
| `merlinccu3` | Pimoroni Pico Plus 2 W | `build-plus2/` |

`tools/ota_update.py` updates units one at a time:

```sh
python tools/ota_update.py merlinccu1 merlinccu2 merlinccu3
python tools/ota_update.py --bin build-plus2/MerlinCCU.bin 192.168.1.172
```

Each build stamps its version and board (`PICO_BOARD`) into
`merlin_generated/firmware_version.h` next to the `.bin`, and each unit
reports its board as `board=` in `/api/firmware`. The script loads every
`--bin` given, or by default whichever of `build-plus2/` and `build/` are
built, and gives each unit the image for its board. A Pimoroni image never
reaches a Pico 2 W, which would start with default settings.

For each unit it reads `/api/firmware`, uploads, then polls until the unit
reports `confirmed` from the other slot with the version the build
stamped. A rollback or a timeout stops the run, so a bad build never
reaches every unit. A unit that is offline, not yet on the A/B layout, or
on a board no image was built for is skipped; the run carries on and lists
it at the end. Firmware from before `board=` existed is updated only with a
single `--bin`, which the per-unit tasks pass. The token comes from
`--token`, `MERLIN_OTA_TOKEN` or `config/ota_credentials.h`.

A single unit can also be updated with `curl`:

```sh
curl -H "X-OTA-Token: <token>" --data-binary @build-plus2/MerlinCCU.bin \
  http://merlinccu3/api/ota
```

### Which build is running

Every build is stamped with its git commit (`tools/firmware_version.cmake`
runs on each build); a `+` means it was built with uncommitted changes.
It also records when it was built, local time to the minute, which tells
two builds of one commit apart. The build time shows at the right of the
Home page footer (`Build: 26 Nov 15:35`), beside the web address; it is left out
if the two would meet. The version and slot show on:

- the panel's Status > Overview page (`FIRMWARE: 2bf47da (A)`);
- the Firmware web page;
- `GET /api/firmware`: `version=`, `board=`, `slot=` and `state=` lines, where
  state is `confirmed`, `on_trial`, `rolled_back` or `not_partitioned`.
  Health lines follow for `tools/soak_monitor.py` (#155, docs/soak-test.md):
  `reset=`, `uptime_min=`, `wifi_connected=`, `mqtt_connected=`,
  `ha_connected=`, `stack_free=`, `heap_arena=` and `alerts=`, then
  `mqtt_unit=`, the unit's MQTT name for HA's Controls setup (#152), and
  `units=`, the other units it sees online of those it knows (#210);
- Home Assistant, over MQTT: a `Firmware` diagnostic sensor (the version,
  also the device's software version) and a `Firmware update` sensor (the
  state above). An automation on `rolled_back` can notify you.

HA's own update card is not used: it needs a "latest version" source, and
there is no release feed to read one from.

## Changing a Unit to the A/B Layout

Each unit needs one last USB flash to change over. Settings and Pinter data
are not touched.

1. Make the table: `picotool partition create tools/ota_partition_table.json
   pt.uf2`.
2. Load it: `picotool load -f pt.uf2`. The old firmware stops booting here.
   Then `picotool reboot -u -f` and check `picotool partition info` lists
   partitions A and B. The boot ROM reads the table only when it starts;
   without the reboot the next load goes to address 0 and overwrites the
   table.
3. Load the firmware **as a UF2**: `picotool load -x
   build-plus2/MerlinCCU.uf2`. The boot ROM picks the slot. Do not use
   `load -x MerlinCCU.elf` from now on: an ELF is written at address 0 and
   overwrites the partition table.
4. Leave it powered for 15 seconds so the image confirms itself. With
   nothing in the other slot it confirms without waiting for Wi-Fi.
5. Check `http://<unit>/firmware` shows a slot letter and `Confirmed`.

The serial log prints the boot ROM's view on every start:
`OTA: boot slot=… boot_type=… trial=… rolled_back=…`.

## Related

- #154: OTA updates
- #158: web config access control (replaces the token)
- `docs/alerts.md`: the `FirmwareUpdateRolledBack` alert
- `docs/architecture.md`: memory budgets and the decision log

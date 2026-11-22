# Over-the-Air Updates

How the CCU updates its firmware over Wi-Fi without anyone opening the case
(#154). This page covers the flash layout and the update flow. It describes the
design that the OTA work is building towards. The "Status" section lists the
parts that exist so far.

## Status

| Step | What | State |
| --- | --- | --- |
| 1 | Flash layout and partition table (`tools/ota_partition_table.json`) | Done (this page) |
| 2 | Web upload into the spare slot, SHA-256 check, reboot into it | To do |
| 3 | Try-before-you-buy confirm and rollback | To do |
| 4 | Firmware version on the Status page; MQTT `update` entity for HA | To do |
| 5 | Laptop script that updates every unit | To do |

Until step 2 lands, units are flashed over USB as before, and the partition
table is not loaded.

## Why A/B Slots

The RP2350 boot ROM understands partition tables. It can boot from one of two
linked partitions (A and B) and roll back if a new image fails. Using the ROM
means:

- no second-stage bootloader of our own to write, debug and keep working;
- a power cut at any point during an update leaves the old image in place;
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

- **Slot size.** The image is about 740 KB today (text plus the embedded
  CYW43 Wi-Fi firmware), so a 1.5 MB slot leaves about half the slot for
  growth. When a build passes the slot size, the next step is to move the
  CYW43 firmware into its own A/B partitions (`pico_use_wifi_firmware_partition`,
  about 256 KB each), not to shrink the data area.
- **Data stays where it is.** Settings and Pinter state are found from the
  top of flash (`PICO_FLASH_SIZE_BYTES`), so they keep the same addresses
  after the change, and an update never writes there. The unpartitioned
  area is `absolute` family with read/write access, so the firmware can
  still program it.
- **Addresses.** The boot ROM maps whichever slot it boots to the normal
  XIP address (`0x10000000`), so the same build runs from A or B. Flash
  writes (`flash_range_program`) use physical offsets, which do not move.
- **Partition table sector.** The table sits in the first 4 KB sector,
  where the boot ROM looks for it. Slot A starts at 8 KB to leave the second
  sector spare. `picotool partition create` accepts the table and places
  its block at the start of flash with slots at `0x2000` and `0x182000`;
  step 2 checks it on a board with `picotool partition info` before any
  unit relies on it.

## Update Flow (Steps 2 to 4)

1. The web config page uploads a `.bin` image with its SHA-256. The upload
   needs a shared token until web access control (#158) lands, because
   anyone who can upload can replace the firmware.
2. The CCU picks the slot it is not running from (`rom_get_b_partition` /
   partition table info), erases it, and writes the image as it arrives.
   Settings saves are blocked while this runs.
3. It hashes what it wrote and compares it with the uploaded SHA-256. On a
   mismatch it stops, and the running image is untouched.
4. It reboots with `rom_reboot(BOOT_TYPE_FLASH_UPDATE, …)` pointing at the
   new slot.
5. The new image is built as try-before-you-buy (TBYB). The boot ROM starts
   it with a watchdog running. The main loop feeds the watchdog as it does
   now. Once Wi-Fi is up and the web server answers, the firmware calls
   `rom_explicit_buy()`, and the new slot becomes the default.
6. If the new image crashes, hangs, loses power, or has not confirmed within
   a fixed deadline, the unit reboots and the boot ROM goes back to the old
   slot. An alert reports the failed update.

## Changing a Unit to the A/B Layout

Each unit needs one last USB flash to change over:

1. Load the partition table: `picotool partition create
   tools/ota_partition_table.json pt.uf2`, then `picotool load pt.uf2`.
2. Reboot to BOOTSEL and load the firmware UF2; the boot ROM places it in
   slot A.
3. Check the settings survived (Status Overview shows the unit's own name).

After that, every update goes over Wi-Fi.

## Related

- #154: OTA updates
- #158: web config access control (protects the upload)
- `docs/architecture.md`: memory budgets and the decision log

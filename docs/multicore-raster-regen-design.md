# Multicore Raster Regeneration Design (Gated)

Design only. Not implemented. Gated on hardware measurements described below.
Builds on `docs/greyscale-investigation.md` and `docs/greyscale-test-card-design.md`.

## Why This Exists

The spatial 2x2 dithering in #54 already reaches the actual near-term target
(0/25/50/75/100% perceived brightness, via `fill_rect_dithered`) at zero
frame-rate or extra RAM cost. This document is about a *further*, optional
enhancement: limited temporal dithering, for cases where spatial dithering's
resolution cost (halving detail in a 2x2 block) isn't acceptable — and it is
deliberately scoped down from a general multi-level temporal scheme to
something that might actually fit this hardware.

## Why Not Just Store Multiple Raster Buffers

The naive implementation of temporal dithering keeps one fully pre-built
native raster per cycle phase and cycles DMA's read pointer between them
every frame (extending the existing `dma_ctrl_irq_handler` swap mechanism,
which already does this once per content change).

This does not fit in RAM. Measured directly from the compiled firmware
(`arm-none-eabi-size build_cli_diag/MerlinCCU.elf`):

```
.ram_vector_table       272
.uninitialized_data      16
.data                  7808
.bss                 470496
                    -------
                     478592 bytes  (~467.4 KB of 520 KB total SRAM)
```

That leaves **~53 KB** of free SRAM for everything dynamic: heap, stack,
lwIP packet buffers, mbedTLS session state, JSON parsing. Each native raster
buffer is ~98 KB (`kRasterWords * 4` bytes, from `include/config/panel_config.h`
sizing). Even a single extra buffer for a 2-phase (3-level: off/half/full)
scheme does not fit in the remaining headroom, let alone the 4-5 buffers a
fuller 0/25/50/75/100% temporal cycle would need (~392-784 KB).

## The Alternative: Regenerate On The Fly From A Small Level Buffer

Instead of pre-storing N full rasters, keep a single small per-pixel
"level" source (cheap: a few KB to ~30 KB depending on bit depth) and have
something rebuild the *existing* 2 raster buffers' dirty content every
physical frame, based on the current frame's phase in the temporal cycle.

This avoids the RAM problem — RAM stays at today's ~196 KB for the 2 raster
buffers, regardless of cycle length — but it does not remove the work
itself. Something still has to walk pixels and decide on/off, every frame,
for as long as dithered content is on screen. Two hard facts rule out doing
that conversion in PIO or DMA:

- The current PIO program is one instruction (`out pins, 4`) with no ALU and
  no concept of a "level" or "phase." RP2350's PIO block is a modest update
  over RP2040's but is still a tiny instruction-limited state machine, not
  capable of per-pixel threshold comparisons while also managing HS/VS
  blanking timing.
- DMA is a byte/word mover — chaining, reloading, sniffing — with no
  per-pixel compare-and-branch capability.

So the per-pixel decision has to be CPU work, same as `rebuild_raster_from_fb`
does today for content changes. The question is which core does it, and how
often.

## Why Core 1, Not Core 0

`docs/architecture.md`'s current multicore direction assigns core 1 to
background/network work and keeps display state on the UI-owning core. This
design inverts that for this specific case: core 1 is dedicated to *only*
the mechanical raster-regeneration task, while core 0 keeps doing UI logic
*and* networking, unchanged.

Doing the rebuild on core 0 would mean it competes every frame with Wi-Fi,
the web server, Home Assistant/MQTT, and keypad polling — the same core
`MainLoopLoadStatus` already exists to measure. Dedicating core 1 removes
that contention entirely: the only question becomes whether core 1 alone is
fast enough, not whether there's spare time on an already-busy core.

`pico_multicore` is already linked into this build (visible in the CMake
file tree) even though multicore is not yet used anywhere.

This is treated as a narrow, explicit exception to the admission checklist
in `docs/architecture.md` (which forbids core 1 writing display state) — see
that document's Multicore Admission Checklist section for the exact scope of
the exception.

## Proposed Design

- **Core 0** (unchanged in scope): owns `ConsoleState`, decides *what* needs
  dithering (which pixels/lines, at what level), same as all existing UI
  logic.
- **Handoff**: core 0 sends core 1 a small, immutable descriptor each time
  dithered content changes — which raster lines are dirty, and what level
  each affected pixel should render at this cycle. This matches the
  "small queues or mailboxes containing immutable snapshots" pattern
  `docs/architecture.md` already prescribes for cross-core communication.
- **Core 1**: a tight, dedicated loop. Wait for the frame-boundary signal,
  rebuild only the dirty raster lines (not the full ~98 KB buffer) into the
  back raster buffer, done. No other responsibilities.
- **Frame-boundary signaling**: either route the DMA control-channel IRQ to
  fire on core 1 directly (RP2350 IRQ enables are per-core), or have core
  0's existing ISR push a signal through the inter-core FIFO. FIFO latency
  is sub-microsecond, negligible against frame timing either way.
- **Dirty-line tracking**: only lines containing a temporally-varying pixel
  are rebuilt each frame; static chrome, borders, and unrelated text are left
  untouched between frames. This is what makes the per-frame cost
  proportional to how much of the screen is actually dithered, not the whole
  panel.

## What This Does Not Change

- RAM math for #54's spatial dithering is unaffected — it already works,
  today, at zero extra cost.
- The panel's hard limits from `docs/greyscale-investigation.md` (25 MHz
  VCLK, 75 Hz VS) are unchanged. This design does not increase how many
  temporal levels are achievable — it only changes *how* a chosen temporal
  scheme would be implemented without blowing the RAM budget. The frame-rate
  ceiling still bounds cycle length to something modest (a 2-phase/3-level
  scheme at most is plausible; a 4-5 phase 0/25/50/75/100% cycle almost
  certainly is not, on both RAM and frame-rate grounds).

## Gating: What Must Be Measured Before Implementation

This design is not ready to implement. Two numbers, both unmeasured until
now, gate it:

1. **Actual physical frame rate.** Instrumented in this same change
   (`display::frame_count()`, sampled once per second alongside the existing
   main-loop-load telemetry, shown on the Resources status page as
   `FRAME RATE`). Determines the real time budget available per frame, and
   therefore whether even a 2-phase cycle is plausible without exceeding the
   panel's 75 Hz ceiling.
2. **Raster rebuild time.** Instrumented in this same change
   (`display::last_rebuild_us()`, timed around the existing
   `rebuild_raster_from_fb` call in `present()`, shown as `RASTER BUILD` on
   the Resources status page). A full-buffer rebuild today only happens on
   content change, not every frame — but its duration is a solid proxy for
   how expensive a per-frame dirty-line rebuild would be, scaled down by
   however small the dirty region is.

Also still open from `docs/greyscale-investigation.md`: phosphor/pixel
persistence is unpublished and unmeasured, and affects how forgiving the
panel actually is toward any temporal toggling, independent of the RAM/CPU
questions this document addresses.

## Next Steps

1. Flash the measurement instrumentation (this change) and read `FRAME RATE`
   and `RASTER BUILD` off the Resources status page (or the boot-time serial
   log, which now also prints the real `clk_sys` frequency).
2. If the numbers suggest a 2-phase cycle is plausible (frame rate leaves
   real headroom above whatever flicker-safe base rate bench testing
   confirms, and rebuild time comfortably fits inside one frame period even
   before dirty-line optimization), scope the core 1 implementation as a
   follow-up issue.
3. If not, this document stands as the record of why temporal dithering
   beyond #54's spatial approach was not pursued, and spatial dithering
   remains the answer for this panel.

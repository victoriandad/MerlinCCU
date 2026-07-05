# Greyscale Rendering Investigation (EL320.256-F6)

Tracks issue #27. Investigation only; no rendering code changes here.

## Practical Target: 4 Brightness Levels Via Spatial Dithering Only

The sections below were written against #28's full scratch list (up to 3-bit
/ 8-level greyscale), which is where the panel's hard 75 Hz ceiling becomes a
real problem. The actual near-term target is more modest: even one dimmed
level is useful, four would be great.

That target does not need temporal dithering, and therefore does not need
either of the two open hardware measurements below. A plain 2x2
ordered/Bayer dither block already yields **5** discrete perceived levels —
0, 1, 2, 3, or 4 of the 4 sub-pixels lit — entirely within a single frame, at
zero frame-rate cost and no panel-timing risk. At this panel's 0.3 mm pixel
pitch, a 2x2 block (0.6 mm) should still read as smooth dimming on
backgrounds, icons, status fills, and a "dimmed panel" state at normal
viewing distance; it will look blocky on single-pixel text or thin lines, so
initial scope should exclude those.

**This narrows #28 to a straightforwardly buildable first step:** an ordered
2x2-dither greyscale layer in the rendering path, scoped to non-text UI
elements. The temporal-dithering analysis and open hardware questions further
down remain relevant only if a future need for finer gradients or animated
brightness transitions revives interest in squeezing more levels out of the
panel.

## Source

Planar EL320.256-F6 / -FD6 Operations Manual, document 020-0352-00A (June
2004). PDF: https://highnessmicro.com/datasheets/EL320.256-F6.pdf

## Display Characteristics

- Technology: solid-state thin-film electroluminescent (EL) panel, amber
  (ZnS:Mn phosphor), 580 nm peak wavelength. Not an LCD or VFD.
- Resolution: 320 x 256, 0.3 mm pixel pitch, 49% pixel fill factor, 160°
  viewing angle.
- Interface: 4 signals — VID, VCLK, HS, VS — matching what
  `include/config/panel_config.h` and `src/display/display.cpp` already drive.
- **VCLK: 40 ns period minimum, i.e. 25 MHz maximum.** (`tVCLK` in the manual's
  Setup/Hold Timing table.)
- **VS (frame) frequency: 75 Hz maximum.** Optical characteristics (luminance,
  contrast) are specified "at 60 Hz frame rate," which reads as the vendor's
  reference operating point rather than a hard ceiling — 75 Hz is the
  documented max.
- No published phosphor/pixel persistence (decay) time constant. This is a
  genuine gap in the datasheet, not an oversight in this review — see
  "Open Questions" below.

## No Native Greyscale Exists On This Panel

This is the headline finding, and it changes the framing of #27 and #28.

The manual's "Display Features" section lists exactly three features: Low
Power Mode, Two-Bits-Parallel, and Brightness Control. None of these is a
greyscale mode:

- **VID is a single binary line.** Each pixel is either lit or unlit; there is
  no multi-bit-per-pixel intensity input anywhere in the interface.
- **"Two-Bits-Parallel" is a throughput feature, not a bit-depth feature.**
  With the `DCONFIG` jumper set, the panel accepts two adjacent *pixels* per
  video clock — even columns on `VID`, odd columns on a second line (`TVID`)
  — purely to halve the required clock frequency for a given frame rate. Each
  of those two pixels is still strictly on/off. This is easy to misread from
  secondary sources (some listings describe the panel as having a "1 or 2 bit
  video interface," which conflates this throughput trick with a grayscale
  bit-depth). The primary manual is unambiguous: both VID and TVID are binary.
- **Brightness Control (`LCa`/`LCb`) is a single global analog potentiometer
  input**, adjusting overall panel luminance from <10% to 100%. It has no
  per-pixel or per-region addressing and cannot be used to implement
  greyscale directly — only a uniform dimming level for the whole panel.

Conclusion: any perceived greyscale on this panel must come entirely from
*software* dithering (temporal, spatial, or both) against a strictly binary
pixel grid. There is no hardware assist available, and no way to reduce the
software burden by offloading bit-depth to the panel.

## Temporal Dithering: Tightly Bandwidth-Constrained

Temporal dithering (e.g. "pixel on 3 of every 4 frames" for 75% brightness)
trades frame-rate budget for perceived brightness levels. The panel's 75 Hz
absolute ceiling makes this the scarce resource.

For temporal dithering to look like brightness rather than flicker, the
*physical* update rate per pixel needs to stay above a flicker-fusion
threshold — commonly cited in the 50-85 Hz range for direct-view emissive
displays, though this depends on ambient brightness, viewing angle, and how
much the panel's own emissive decay smooths transitions (see "Open
Questions").

Given a 75 Hz hard ceiling:

- **2-level temporal dithering** (alternating two sub-frames to add one
  perceived brightness step) needs roughly double the perceptually-required
  base rate. If the base rate needs to be ~40 Hz or higher to look
  flicker-free, 2x that already meets or exceeds the panel's 75 Hz ceiling —
  there is little to no margin left over.
- **3-bit (8-level) temporal dithering as listed in #28's scratch list is not
  realistic on this panel.** It would need roughly 8x the base flicker-free
  rate, an order of magnitude past the 75 Hz ceiling.
- Even 2-bit (4-level) temporal-only dithering is doubtful without hardware
  measurement showing the panel tolerates a lower perceptual base rate than
  typical assumptions (plausible for an EL panel with real phosphor
  persistence, but unconfirmed — see below).

This directly affects scope: #28's list of "2-bit / 3-bit brightness" and
"pulse-density modulation" items should be treated as unlikely to be
achievable through temporal dithering alone, pending the hardware
measurement in "Open Questions."

## Spatial Dithering: The Realistic Primary Approach

Spatial dithering (Bayer/ordered dithering, checkerboard patterns, error
diffusion) trades spatial resolution for perceived brightness levels within a
*single* frame. This has two advantages specific to this panel:

- It consumes **zero** frame-rate budget — fully compatible with whatever
  frame rate the current firmware already runs at, with no risk of pushing
  VCLK or VS timing further toward their documented maximums.
  Ordered/Bayer dithering is a lookup-table comparison per pixel against a
  small (e.g. 4x4 or 8x8) threshold matrix — cheap on a Pico 2 W, and it
  slots into the existing CPU-built raster path in `display.cpp` without
  restructuring the DMA/PIO scanout loop.
- At 0.3 mm pixel pitch and normal viewing distance, a few perceived
  brightness levels from a small dither pattern should be visually usable for
  icons, gradients, and softened UI chrome, even though it costs spatial
  detail. Error diffusion (Floyd-Steinberg-style) is a reasonable option
  specifically for photographic/icon content, less so for small text or thin
  UI lines where it would blur legibility.

**Recommended primary direction for #28: build the greyscale framebuffer and
initial dithering support around spatial (ordered/Bayer) dithering first.**
Treat temporal dithering as a secondary, bandwidth-constrained enhancement
gated on the hardware measurement below — not a foundation to build on.

A hybrid (mild 2-phase temporal alternation between two different spatial
dither patterns) may add roughly one extra perceived brightness level if
bench measurement shows headroom under the 75 Hz ceiling, but this should
follow the measurement, not precede it.

## Memory Cost

Current raster buffers (`src/display/display.cpp`, sized from
`include/config/panel_config.h`): 384 pixels/line x 262 lines, packed 8
nibbles/word = 25,152 words = **100,608 bytes per raster buffer**, and the
scanout path keeps two (front/back) for tear-free swaps = **~196 KiB total**,
against the Pico 2's 520 KiB SRAM.

This matters for temporal dithering specifically: the existing architecture
regenerates one full raster from the UI framebuffer per `present()` call and
hands it to DMA to replay continuously — there is deliberately no CPU
involvement in steady-state scanout. Extending that model to N alternating
temporal sub-frames means keeping N precomputed raster buffers and cycling
which one DMA points to at each frame boundary (consistent with the existing
double-buffer swap mechanism), at a cost of roughly another 98 KiB of SRAM
per additional temporal phase. That is a real constraint alongside whatever
lwIP/mbedtls/UI state the rest of the firmware needs, and is a good input for
issue #15 (RAM/size budget tracking).

Spatial dithering has no equivalent memory cost beyond a small constant
threshold-matrix table (a handful of bytes).

## Open Questions Requiring Hardware, Not Static Analysis

Two things in this investigation cannot be resolved by reading the datasheet
or the source tree, and should not be guessed at:

1. **Phosphor/pixel persistence (decay) time.** Not published in the manual.
   This determines how forgiving the panel actually is toward temporal
   dithering — a longer natural decay smooths transitions and may allow a
   lower physical toggle rate than typical direct-view-display flicker
   assumptions. Needs a bench measurement: drive a single pixel with a short
   pulse and observe decay with a photodiode/oscilloscope, or even a simple
   visual comparison at a few candidate temporal rates.
2. **This firmware's actual current frame rate.** I derived the relationship
   between `kPanel.clkdiv`, the PIO program's autopull stall pattern, and
   resulting VCLK/frame frequency from the source in `display.cpp` and
   `el320_raster.pio`, expecting to cross-check it against the panel's 25 MHz
   VCLK / 75 Hz VS limits. Across every plausible RP2350 default system clock
   (125/133/150 MHz), the static calculation lands at 220-264 Hz — implausible
   given the panel's documented 75 Hz ceiling, and given the project's own
   notes that the current `clkdiv` is an "empirically stable" bring-up value
   that works on real hardware. That mismatch means either the PIO autopull
   cycle-cost model used here is wrong, or there's a per-frame timing detail
   this review didn't capture — not that the panel is being overdriven.
   Rather than assert a specific number I can't verify, the honest next step
   is to measure it directly: read `clock_get_hz(clk_sys)` over the existing
   USB serial diagnostics at boot, and/or toggle a spare GPIO once per DMA
   frame-boundary IRQ and confirm the real frame rate with a scope or logic
   analyzer. This should happen before any dithering timing is designed
   against an assumed frame-rate budget.

## Deliverables Against #27's Acceptance Criteria

- [x] Greyscale feasibility established: **yes, but software-only.** No
  hardware greyscale assist exists on this panel.
- [x] Suitable rendering technique identified: **spatial (ordered/Bayer)
  dithering as the primary approach**; temporal dithering only as a
  secondary, bandwidth-constrained enhancement.
- [x] Flicker risk characterised at the architecture level (75 Hz hard
  ceiling leaves little to no margin for multi-level temporal dithering);
  **not yet characterised at the panel-persistence level** — open question
  above.
- [x] Hardware and software constraints documented (this file).
- [x] Recommended implementation approach documented: spatial dithering
  first; gate any temporal component on the two open hardware measurements
  above.

## Suggested Follow-Up Issues

Per #27's own notes, this investigation should hand off to implementation
issues rather than close the topic. Given the practical target above, the
first of these is unblocked and buildable now; the rest are not required to
reach it:

- **Implement an ordered 2x2 (or 4x4, for finer gradation) spatial dithering
  greyscale layer**, scoped to non-text UI elements (backgrounds, icons,
  status fills, a dimmed-panel state). This alone reaches the "4 levels would
  be great" target with no dependency on the open questions below.
- Measure and document actual frame rate / `clk_sys` on real hardware
  (unblocks the open question above and issue #15's RAM/size budget work).
  Only needed if temporal dithering becomes interesting later.
- Revisit temporal dithering only after the hardware persistence measurement,
  scoped to at most a 2-level enhancement, and only if spatial dithering
  turns out not to be enough.

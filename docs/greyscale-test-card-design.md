# Greyscale Test Card Design (Issue #54)

Implements the practical target from `docs/greyscale-investigation.md`: one
dimmed level is useful, four would be great, reached entirely through 2x2
ordered (Bayer) spatial dithering with no dependency on temporal dithering or
its open hardware questions.

## Dither Matrix

Standard 2x2 Bayer threshold matrix:

|            | x % 2 = 0 | x % 2 = 1 |
| ---------- | --------- | --------- |
| y % 2 = 0  | 0         | 2         |
| y % 2 = 1  | 3         | 1         |

A pixel at `(x, y)` is lit when `matrix[x % 2][y % 2] < level`, for `level`
0-4. This produces exactly 0, 1, 2, 3, or 4 lit sub-pixels per 2x2 block, and
the Bayer ordering disperses the lit sub-pixels evenly rather than
clustering them, which would read as banding instead of a brightness step.

## Levels Produced

| Level | Lit / 4 | Suggested use |
| ----- | ------- | ------------- |
| 0     | 0/4     | Off / unlit |
| 1     | 1/4     | Dimmed fill, disabled state |
| 2     | 2/4     | Mid-tone fill, inactive icon |
| 3     | 3/4     | Near-full, active-but-secondary |
| 4     | 4/4     | Full on (existing behaviour, unchanged) |

## Scope

Deliberately excluded from this pass:

- **Text and thin single-pixel lines.** A 2x2 block halves effective
  resolution in both axes, which reads fine on fills and icons but blurs
  small glyphs.
- **Temporal dithering.** Gated on the two hardware measurements in
  `docs/greyscale-investigation.md` (actual current frame rate, phosphor
  persistence), neither of which this scope needs.

Cost: one 2x2 constant lookup table (4 bytes) plus a per-pixel comparison;
no additional framebuffer memory.

## Implementation

Implemented on branch `greyscale-test-card-dithering` (issue #54):

- **`include/display/framebuffer.h` / `src/display/framebuffer.cpp`** — new
  `fill_rect_dithered(fb, x, y, w, h, level)`, sibling to the existing
  `fill_rect`, using the matrix above via `set_pixel` (so it inherits the
  same bounds clipping as every other framebuffer primitive).
- **`include/core/console_model.h`** — new `MenuPage::GreyscaleTest` and
  `SoftKeyRoute::GoGreyscaleTest`.
- **`src/display/screens.cpp`** — new `draw_greyscale_test_card(fb,
  console_state)`: five bands (levels 0-4), each preceded by a small label
  drawn on the plain background rather than overlaid on the dithered fill.
  This is a 1-bit framebuffer, so there is no intermediate pixel colour that
  would guarantee label contrast against every brightness level — separating
  label from fill sidesteps that instead of trying to invert per-pixel.
- **`src/core/console_controller.cpp`** — reachable from the existing
  `Status` page via a new `GREYSCL` softkey (`Right3`, previously unused),
  following the same pattern as the existing `KEYPAD` softkey that reaches
  `MenuPage::KeypadDebug`. `parent_page()` returns `Settings`, matching
  `KeypadDebug`. No softkeys are defined on the page itself, also matching
  `KeypadDebug` — back navigation is via the hardware `BACK STEP` key.

### Correction from the original design sketch

The initial design referenced `MenuPage::Alignment` / `draw_calibration_screen`
as the precedent to follow. Closer inspection during implementation found
`draw_alignment_page` is an empty placeholder ("the route already exists so
menu navigation can stabilize before the dedicated alignment workflow is
implemented") and `draw_calibration_screen` is actually invoked through a
separate top-level `ScreenMode::Calibration` mechanism in `MerlinCCU.cpp`,
not through the `MenuPage` system at all. `MenuPage::KeypadDebug` — a real,
working, softkey-reachable diagnostic page — turned out to be the accurate
precedent, and is what this implementation follows.

## Validation

Per the existing Display Validation Workflow: flash, open
`http://merlinccu/preview`, navigate Home → STATUS → GREYSCL, and check all
five bands render distinctly on both the browser preview and the physical
panel.

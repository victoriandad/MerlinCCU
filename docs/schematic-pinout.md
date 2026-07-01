# MerlinCCU Schematic Pinout Draft

This is the working connection list for turning the bench harness into a
schematic. It reflects the current firmware configuration and the confirmed
front-panel matrix notes in `README.md`.

Treat this as a draft netlist, not a finished PCB design. Unknown front-panel
pins must be measured before they are connected to Pico GPIO.

## Pico 2 W Signals

| Pico signal | Current use | Schematic net |
| --- | --- | --- |
| `GPIO2` | Display video data | `DISP_VID` |
| `GPIO3` | Display video clock | `DISP_VCLK` |
| `GPIO4` | Display horizontal sync | `DISP_HS` |
| `GPIO5` | Display vertical sync | `DISP_VS` |
| `GPIO6` | Keypad connector pin 5 | `KP_P05` |
| `GPIO7` | Keypad connector pin 6 | `KP_P06` |
| `GPIO8` | Keypad connector pin 7 | `KP_P07` |
| `GPIO9` | Keypad connector pin 8 | `KP_P08` |
| `GPIO10` | Keypad connector pin 9 | `KP_P09` |
| `GPIO11` | Keypad connector pin 10 | `KP_P10` |
| `GPIO12` | Keypad connector pin 11 | `KP_P11` |
| `GPIO13` | Keypad connector pin 15 | `KP_P15` |
| `GPIO14` | Keypad connector pin 16 | `KP_P16` |
| `GPIO15` | Keypad connector pin 17 | `KP_P17` |
| `GPIO16` | Keypad connector pin 18 | `KP_P18` |
| `GPIO17` | Keypad connector pin 20 | `KP_P20` |
| `GPIO18` | Keypad connector pin 21 | `KP_P21` |
| `GPIO19` | Keypad connector pin 19 | `KP_P19` |
| `GPIO20` | Waveshare sensor HAT I2C0 SDA | `ENV_I2C0_SDA` |
| `GPIO21` | Waveshare sensor HAT I2C0 SCL | `ENV_I2C0_SCL` |
| `GPIO22` | Keypad connector pin 22 | `KP_P22` |
| `GPIO26` | Spare ADC-capable GPIO | `SPARE_ADC0_GPIO26` |
| `GPIO27` | Spare ADC-capable GPIO | `SPARE_ADC1_GPIO27` |
| `GPIO28` | Spare ADC-capable GPIO | `SPARE_ADC2_GPIO28` |

## Display Connector Draft

The firmware expects the EL320 scanout signals on four contiguous Pico GPIOs
starting at `GPIO2`. Connector pin numbers are still to be filled in from the
display hardware.

| Display connector pin | Schematic net | Pico signal | Notes |
| --- | --- | --- | --- |
| `TBD` | `DISP_VID` | `GPIO2` | Monochrome video data |
| `TBD` | `DISP_VCLK` | `GPIO3` | Video clock |
| `TBD` | `DISP_HS` | `GPIO4` | Horizontal sync |
| `TBD` | `DISP_VS` | `GPIO5` | Vertical sync |
| `TBD` | `DISP_GND` | `GND` | Common logic ground |
| `TBD` | `DISP_SUPPLY_TBD` | External/display supply | Measure before routing |

Do not assume the display power rail is Pico `3V3`. Confirm the panel logic and
backlight/display supply requirements independently.

## Keypad Connector Draft

Use a 30-way IDC-style symbol for the front-panel keypad connector. The current
firmware only treats confirmed matrix pins as GPIO-safe.

| Keypad connector pin | Schematic net | Pico signal | Matrix role / note |
| --- | --- | --- | --- |
| 1 | `KP_P01_TBD` | No connect | Unknown non-matrix line |
| 2 | `KP_P02_TBD` | No connect | Unknown non-matrix line |
| 3 | `KP_P03_TBD` | No connect | Unknown non-matrix line |
| 4 | `KP_P04_TBD` | No connect | Unknown non-matrix line |
| 5 | `KP_P05` | `GPIO6` | Matrix drive line; `ALERT`, `TEST`, `BRT`, `DIM` group |
| 6 | `KP_P06` | `GPIO7` | Matrix drive line; navigation row |
| 7 | `KP_P07` | `GPIO8` | Matrix drive line; `A..F`, plus `R1` via pin 15 |
| 8 | `KP_P08` | `GPIO9` | Matrix drive line; `G..L`, plus `R2` via pin 15 |
| 9 | `KP_P09` | `GPIO10` | Matrix drive line; `M..R`, plus `R3` via pin 15 |
| 10 | `KP_P10` | `GPIO11` | Matrix drive line; `S..X`, plus `R4` via pin 15 |
| 11 | `KP_P11` | `GPIO12` | Matrix drive line; `Y`, `Z`, `T FUNC`, `.`, `0`, `SPC`, plus `R5` |
| 12 | `KP_P12_TBD` | No connect | Unknown non-matrix line |
| 13 | `KP_P13_TBD` | No connect | Unknown non-matrix line |
| 14 | `KP_P14_TBD` | No connect | Unknown non-matrix line; not currently wired in firmware |
| 15 | `KP_P15` | `GPIO13` | Matrix sense line; right softkeys and `DIM` |
| 16 | `KP_P16` | `GPIO14` | Matrix sense line; `BRT`, `CLR`, `F`, `L`, `R`, `X`, `SPC` |
| 17 | `KP_P17` | `GPIO15` | Matrix sense line; `TEST`, `/`, `E`, `K`, `Q`, `W`, `0` |
| 18 | `KP_P18` | `GPIO16` | Matrix sense line; right arrow, `D`, `J`, `P`, `V`, `.` |
| 19 | `KP_P19` | `GPIO19` | Matrix sense line; left arrow, `C`, `I`, `O`, `U`, `T FUNC` |
| 20 | `KP_P20` | `GPIO17` | Matrix sense line; `ALERT`, `BACK STEP`, `B`, `H`, `N`, `T`, `Z` |
| 21 | `KP_P21` | `GPIO18` | Matrix sense line; `LTRS`, `A`, `G`, `M`, `S`, `Y` |
| 22 | `KP_P22` | `GPIO22` | Matrix sense line; left softkeys `L1..L5` |
| 23 | `KP_P23_TBD` | No connect | Unknown non-matrix line |
| 24 | `KP_P24_TBD` | No connect | Unknown non-matrix line |
| 25 | `KP_P25_TBD` | No connect | Unknown non-matrix line |
| 26 | `KP_P26_TBD` | No connect | Unknown non-matrix line |
| 27 | `KP_P27_TBD` | No connect | Unknown non-matrix line |
| 28 | `KP_P28_TBD` | No connect | Unknown non-matrix line |
| 29 | `KP_P29_TBD` | No connect | Unknown non-matrix line |
| 30 | `KP_P30_TBD` | No connect | Unknown non-matrix line |

## Confirmed Key Closures

| Closure | Key group |
| --- | --- |
| `KP_P05 x KP_P20` | `ALERT` |
| `KP_P05 x KP_P17`, `KP_P05 x KP_P16`, `KP_P05 x KP_P15` | `TEST`, `BRT`, `DIM` |
| `KP_P06 x KP_P21..KP_P16` | `LTRS`, `BACK STEP`, left arrow, right arrow, `/`, `CLR` |
| `KP_P07 x KP_P21..KP_P16` | `A..F` |
| `KP_P08 x KP_P21..KP_P16` | `G..L` |
| `KP_P09 x KP_P21..KP_P16` | `M..R` |
| `KP_P10 x KP_P21..KP_P16` | `S..X` |
| `KP_P11 x KP_P21..KP_P16` | `Y`, `Z`, `T FUNC`, `.`, `0`, `SPC` |
| `KP_P07..KP_P11 x KP_P22` | `L1..L5` |
| `KP_P07..KP_P11 x KP_P15` | `R1..R5` |

## Schematic Notes

- Add series resistors or resistor-array footprints on keypad GPIO nets if the
  PCB has space. They provide useful protection during front-panel bring-up.
- Keep the Waveshare sensor HAT I2C nets on `GPIO20` and `GPIO21`; do not route
  keypad pin 20 to `GPIO20`.
- Keep `GPIO26`, `GPIO27`, and `GPIO28` free for analogue experiments such as a
  photoresistor unless a later schematic revision allocates them.
- Tie Pico `GND`, display logic ground, keypad/front-panel ground, and any
  external display supply ground together at the schematic level.
- Unknown keypad connector pins should remain labelled and unconnected until
  continuity and powered-voltage tests prove their function.

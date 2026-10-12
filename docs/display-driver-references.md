# Display driver reference coverage

Matching a controller family is not enough to copy an initialization sequence
or waveform. Glass, gate count, analog rails, OTP contents, temperature policy,
and panel mounting remain device-specific. Use the most exact source available
for each FreeInk target.

| FreeInk target | Controller/path | Primary source | Useful secondary source | Coverage notes |
|---|---|---|---|---|
| Xteink X4 / de-link | SSD1677, GDEQ0426T82 | X4 stock firmware and inherited community-sdk tuning | [GxEPD2 GDEQ0426T82 driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdeq/GxEPD2_426_GDEQ0426T82.cpp) | Exact controller, glass, and 800×480 geometry. Compare command sequencing and RAM baseline handling directly; retain FreeInk's field-tested AA LUTs. |
| Xteink X4 Pro, UC8179 batch | UC8179, 800×480 visible / 800×600 addressed | X4 Pro factory firmware and hardware reference | [GxEPD2 GDEY075T7 driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_750_GDEY075T7.cpp) | Same controller family and visible resolution, but different glass and gate scan. Controller-level behavior such as PWS and old/new RAM lifecycle is useful; do not transplant TRES, booster, temperature, or LUT values wholesale. |
| Xteink X4 Pro, UC8279 batch | UC8279, 800×480 visible / 800×600 addressed | X4 Pro hardware reference and factory firmware | None in GxEPD2 | Keep separate from both the UC8179 X4 Pro driver and the UC8279d X3 driver. |
| Xteink X3, original batch | UC8253, 792×528 | X3 stock firmware and inherited community-sdk tuning | [GxEPD2 UC8253 drivers](https://github.com/ZinggJM/GxEPD2/tree/master/src/gdey) | Controller-family reference only. The X3 glass, addressing, and waveform banks are specific to the device. |
| Xteink X3, newer batch | UC8279d, 792×528 | X3 stock firmware, recovered LUTs, and UC8279d datasheet | None in GxEPD2 | Device-specific MTP/OTP behavior and 99-byte visible rows require the dedicated driver. |
| Murphy M3 | UC8253, 240×416 | Murphy vendor sequences and panel LUTs | [GxEPD2 GDEY037T03 driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_370_GDEY037T03.cpp) | Close panel-level comparison. Preserve Murphy's verified reset cadence, rotation, and supplied LUTs unless hardware testing supports a change. |
| M5Stack Paper Mono | SSD1677, 800×480 | M5 board implementation and on-glass measurements | [GxEPD2 SSD1677 drivers](https://github.com/ZinggJM/GxEPD2) | This panel uses a dedicated FreeInk driver with host-authored, panel-measured grayscale waveforms. |
| M5Paper v1.1 | IT8951E + ED047TC1 | M5Paper hardware and IT8951 firmware/protocol | [GxEPD2 IT8951 drivers](https://github.com/ZinggJM/GxEPD2/tree/master/src/it8951) | Protocol reference only: GxEPD2 lists other IT8951 panel/HAT combinations. VCOM, geometry, rotation, and waveform availability come from the attached controller. |
| LilyGo T5 S3 | Raw-parallel ED047TC1 via LovyanGFX | LilyGo hardware and LovyanGFX `Panel_EPD` | None directly applicable | There is no on-glass SPI controller to compare with GxEPD2's panel drivers. |
| M5Stack PaperColor | ED2208 / optional M5GFX backend | M5Stack vendor libraries and measured interrupted-refresh behavior | None in GxEPD2 | Six-color full-refresh operation is outside GxEPD2's matching controller set. |
| Glyph v1 | SSD1683, GDEY042T81, 400×300 | [GxEPD2 GDEY042T81 driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_420_GDEY042T81.cpp) (`GxEPD2_420_GDEY042T81`, ZinggJM), validated on glass by the Glyph hwtest | Good Display GDEY042T81 datasheet | Exact controller and glass. Sequences follow GxEPD2; OTP waveforms only, no custom LUT. See [SSD1683 / GDEY042T81](#ssd1683--gdey042t81). |

## SSD1683 / GDEY042T81

`Ssd1683Driver` values, from `ssd1683DefaultConfig()` unless noted. A board can
supply its own `Ssd1683Config` with `-DFREEINK_SSD1683_CONFIG=yourConfig`.

| Step | Sequence | Notes |
|---|---|---|
| Init | `0x12`, 10 ms, BUSY; `0x01 2B 01 00`; `0x3C 01`; `0x18 80`; `0x11 03` | 300 gate lines; `mirrorY` ORs TB into the scan byte. Internal temperature sensor. RAM X is byte addressed. |
| FULL | `0x21 40 00`, `0x22 F7`, `0x20` | OTP full waveform with temperature load. RED bypassed, so not differential. Powers down after. |
| HALF (warm) | `0x21 40 00`, `0x1A 6E`, `0x22 D7`, `0x20` | Forced temperature selects the shorter OTP waveform. `0x6E` is GxEPD2's 2024-panel value, noted there as ok for 2023 panels. Powers down after. |
| FAST | `0x21 00 00`, `0x22 FC`, `0x20` | OTP differential against RED (the previous frame). Rails stay up; `0x22 83` powers off when asked. |
| Deep sleep | `0x22 83` if powered, then `0x10 01` | Mode 1 retains RAM; only RES# wakes it. `skipInitialResync()` and `beginFromDeepSleep()` rely on this. Sends nothing while the controller is already asleep. |

BUSY is active high. The first paint after `begin()` promotes FAST to FULL,
because a differential cannot clear whatever is on the glass at boot;
`skipInitialResync()` waives that when the host knows the glass still shows the
frame held in controller RAM. No grayscale LUT is characterised for this glass,
so `grayscaleCapabilities()` advertises nothing.

`beginFromDeepSleep()` is the wake path: it touches nothing, and RES# + init run
just before the first command (a refresh, a window, a RED seed), so the first
refresh after a wake costs about 40 ms more (the 30 ms RES# pulse, 10 ms after
SWRESET, BUSY). It implies `skipInitialResync()`. `deepSleep()` before any
refresh sends nothing, because the controller never woke. The reason is measured, not stylistic: on a Glyph v1, `0x10` (mode 1 or 2)
sent after RES# + SWRESET + init with no display sequence (`0x22` with the
display bit, `0x20`) since darkened the whole active area on every such cycle.
A LUT load (`0x22 B1`) or a power on/off (`0x22 E0` / `0x22 83`) before it did
not help; a refresh before it, or no `0x10` at all, did. GxEPD2-style drivers
reset and init only when they draw, which avoids it by habit.

## Change policy

- Exact panel matches may justify sequence-level comparisons, but still require
  hardware validation before replacing known-good FreeInk behavior.
- Controller-only matches are evidence for command semantics, RAM lifecycle,
  and optional registers—not for analog values, gate geometry, or LUT timing.
- Factory firmware or a device-specific vendor reference wins when it conflicts
  with a generic library using different glass.
- Waveforms and forced-temperature values are panel-specific. Never copy them
  solely because the controller name matches.

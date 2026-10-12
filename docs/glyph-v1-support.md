# Glyph v1 support

`-DFREEINK_DEVICE_GLYPH=1` (`Board::Glyph`, profile name `glyph_v1`).

A custom ESP32-S3-MINI-1-N4R2 board (4 MB quad flash, 2 MB quad PSRAM) behind a
4.2" 400x300 B/W e-paper panel (GDEY042T81, SSD1683). Buttons only: two keys plus
BOOT. It has a PDM microphone, a LIS2DW12 accelerometer, an ADC battery divider and
a TP4056 charger. There is no SD slot, touch digitizer, frontlight or audio output.

The pin source of truth in this SDK is the `GLYPH_V1` profile in
`libs/hardware/BoardConfig/include/BoardConfig.h` and the comment block above it.
The tables below restate it.

## Pins

From `GLYPH_V1`:

| Function | Pins |
|---|---|
| EPD SPI | SCK 41, MOSI 42 (write-only, no MISO) |
| EPD control | CS 40, DC 39, RST 38, BUSY 37 (BUSY HIGH = busy) |
| EPD power rail | none: the on-board booster is driven by the panel's GDR/RESE |
| EPD SPI clock | 4 MHz (`displaySpiHz`) |
| Buttons | A = GPIO12 (confirm), B = GPIO13 (back), BOOT = GPIO0 (power); all active-low |
| Battery | ADC GPIO10 (ADC1_CH9), 1 MΩ / 1 MΩ divider, multiplier 2.0 |
| Charge status | CHRG GPIO47 (open drain, LOW = charging) |
| PDM mic | CLK 5, DATA 4, no enable pin; `pdmDownsample` 16 (2.048 MHz PDM clock at 16 kHz) |
| I²C bus | SDA 34, SCL 48, 400 kHz |
| Accelerometer | LIS2DW12 @ 0x19 (`ImuType::Lis2dw12`) |

From the comment above `GLYPH_V1` (no profile field; see
[Consumer-side by design](#consumer-side-by-design)):

| Function | Pins |
|---|---|
| Accelerometer interrupts | INT1 GPIO15, INT2 GPIO16 |
| Charge complete | STDBY GPIO33 (LOW = charge complete) |
| Haptic driver | DRV2605L @ 0x5A on the sensor bus, EN GPIO1 |
| Touch pad | GPIO14 (TOUCH14, single pad) |
| FSR | supply GPIO36, sense GPIO6 (ADC1_CH5); bodge wires |
| Reserved | GPIO35 is driven by the fitted but unused NAU7802 (0x2A); leave it an input |

GPIO12 and GPIO13 have **no external pull-up**. `InputManager` configures every
`DigitalButtons` pin with `INPUT_PULLUP`, which supplies it. BOOT has a 10 kΩ
external pull-up.

## In the profile

`GLYPH_V1` carries the display, buttons, ADC battery and CHRG pin, the PDM mic and
the sensor bus with `ImuType::Lis2dw12`. Capabilities: `FREEINK_CAP_MIC` and
`FREEINK_CAP_IMU` turn on. Touch, frontlight, RTC, buzzer, LED, haptic, SDMMC and
the I²C battery gauge stay off. The panel uses its own `FREEINK_DRIVER_SSD1683`
driver (`DisplayController::SSD1683`); register sequences and their provenance are
in [display driver references](display-driver-references.md).

The `Imu` lib's LIS2DW12 backend runs the part at 100 Hz in high-performance mode
(14-bit), ±2 g, with block data update. `read()` returns g; the gyro fields are zero
and `hasGyroscope()` is false. `sleep()` powers the part down (ODR = 0) and `wake()`
restores 100 Hz.

## Consumer-side by design

These have no `BoardProfile` field and stay in the consumer's own pin header:

* **FSR force sensor** (GPIO36 supply, GPIO6 sense).
* **Capacitive touch pad** on GPIO14. It is one pad, not an I²C digitizer, so the
  profile says `NO_TOUCH`.
* **DRV2605L haptics.** It is an I²C waveform engine, not the GPIO-PWM motor that
  `HapticConfig` describes, so `haptic` stays unassigned.
* **LIS2DW12 interrupts and wake-on-motion.** The `Imu` lib has no interrupt API.
  The wake-up interrupt routes only to INT1 (GPIO15).
* **STDBY** (GPIO33). The profile's `batteryChargeStatus` is CHRG. USB presence is
  inferred from CHRG and STDBY together, since there is no VBUS sense pin.

## Validated on hardware

**Confirmed on hardware** on a Glyph v1, 2026-10-09/10:

* **Panel (`Ssd1683Driver`).** FULL (`0x22 F7`) 1.64 s; FAST differential
  (`0x22 FC`) 462 ms; a windowed refresh (`displayWindow`) 400 ms.
* **Deep sleep and wake.** Deep sleep (`0x10 01`) with ext1 motion wake: the panel
  image and RAM are retained across sleep. On wake the consumer calls
  `beginFromDeepSleep()`, which leaves the controller asleep: RES#, SWRESET and
  init run on the first refresh (about 40 ms more on that one), and that
  refresh stays a differential instead of a promoted FULL. A sleep with no
  refresh since the wake sends nothing. Measured 2026-10-12: entering deep sleep
  (mode 1 or 2) from the reset + init state, with no display sequence since,
  darkened the whole active area a little on every such wake-and-sleep cycle;
  the lazy wake does not send that sequence.
* **LIS2DW12.** `Imu::read()` reads ~1.0 g at rest.
* **Orientation.** `NO_FLIP` with the UI's landscape base reads upright on the glass;
  the consumer's four-way facing table was checked on the glass 2026-10-10.
* **PDM mic.** Streams at 16 kHz with `pdmDownsample` 16. The consumer's rate check
  passed: 3072 bytes in 105 ms against 3200 expected.

## Pending hardware validation

* **HALF refresh.** The duration of the warm `0x22 D7` sequence on this unit has
  not been measured.
* **Tap thresholds.** The consumer's LIS2DW12 tap thresholds are ST's AN5038
  starting values and have not been tuned.
* **Viewable insets.** Default values; measure on the bezel.
* **SPI clock.** 4 MHz is the GxEPD2 bring-up value. The driver may raise it.
* **IMU axes.** `imuSwapXY` and the flips stay false; the consumer maps raw axes to
  panel edges.

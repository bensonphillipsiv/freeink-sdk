# Glyph v1 support

`-DFREEINK_DEVICE_GLYPH=1` (`Board::Glyph`, profile name `glyph_v1`).

A custom ESP32-S3-MINI-1-N4R2 board (4 MB quad flash, 2 MB quad PSRAM) behind a
4.2" 400x300 B/W e-paper panel (GDEY042T81, SSD1683). Buttons only: two keys plus
BOOT. It has a PDM microphone, a LIS2DW12 accelerometer, an ADC battery divider and
a TP4056 charger. There is no SD slot, touch digitizer, frontlight or audio output.

The pin truth is the Glyph repository's `HARDWARE.md` (§1 GPIO map, §2 I²C, §3 mic,
§4 LIS2DW12, §5 e-paper, §7 buttons, §8 power). The profile must agree with it.

## Pins

| Function | Pins | HARDWARE.md |
|---|---|---|
| EPD SPI | SCK 41, MOSI 42, CS 40, DC 39, RST 38, BUSY 37 (BUSY HIGH = busy) | §1, §5 |
| EPD power rail | none: the on-board booster is driven by the panel's GDR/RESE | §5, §8 |
| EPD SPI clock | 4 MHz (measured working under GxEPD2) | §5 |
| Buttons | A = GPIO12 (confirm), B = GPIO13 (back), BOOT = GPIO0 (power); all active-low | §1, §7 |
| Battery | ADC GPIO10 (ADC1_CH9), 1 MΩ / 1 MΩ divider, ×2 | §1, §8 |
| Charge status | CHRG GPIO47 (LOW = charging); STDBY GPIO33 (LOW = charge complete) | §1, §8 |
| PDM mic | CLK 5, DATA 4, no enable pin | §3 |
| I²C bus | SDA 34, SCL 48, 4.7 kΩ pull-ups | §2 |
| Accelerometer | LIS2DW12 @ 0x19 (SA0 high); INT1 15, INT2 16 | §2, §4 |
| Haptic driver | DRV2605L @ 0x5A, EN GPIO1 | §2, §7 |
| Touch pad | GPIO14 (TOUCH14, single pad) | §1, §7 |
| FSR | supply GPIO36, sense GPIO6 (ADC1_CH5); bodge wires | §6 |
| Reserved | GPIO35 is driven by the fitted but unused NAU7802 (0x2A) | §1, §2 |

GPIO12 and GPIO13 have **no external pull-up**. `InputManager` configures every
`DigitalButtons` pin with `INPUT_PULLUP`, which supplies it. BOOT has a 10 kΩ
external pull-up.

## In the profile

`GLYPH_V1` carries the display, buttons, ADC battery and CHRG pin, the PDM mic and
the sensor bus with `ImuType::Lis2dw12`. Capabilities: `FREEINK_CAP_MIC` and
`FREEINK_CAP_IMU` turn on. Touch, frontlight, RTC, buzzer, LED, haptic, SDMMC and
the I²C battery gauge stay off. The panel uses its own `FREEINK_DRIVER_SSD1683`
driver (`DisplayController::SSD1683`).

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

## Pending hardware validation

* **Orientation.** `NO_FLIP` has not been checked against the SSD1683 driver.
* **Viewable insets.** Default values; measure on the bezel.
* **SPI clock.** 4 MHz is the GxEPD2 bring-up value. The driver may raise it.
* **LIS2DW12 backend.** Host-tested only (`libs/hardware/Imu/test/host`, glyph
  mode). It has not run on the board. `imuSwapXY` and the flips stay false; the
  consumer maps raw axes to panel edges with the table measured in HARDWARE.md §4.

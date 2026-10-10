#pragma once

// SSD1683 panel driver — Glyph v1 (Good Display GDEY042T81, 4.2" 400x300 mono).
// Dual-RAM (BW 0x24 / RED 0x26) controller: OTP full (0xF7), warmed full
// (0xD7) and differential partial (0xFC) waveforms. RAM X addressing is in
// bytes. Active-HIGH BUSY, write-only 4-wire SPI.
//
// Register sequences follow GxEPD2's GxEPD2_420_GDEY042T81, the class the
// Glyph hwtest validated on the real panel. Geometry and SPI clock come from
// BoardConfig; waveform values come from an Ssd1683Config.

#include "PanelDriver.h"

namespace freeink {

struct Ssd1683Config {
  uint8_t muxLines[3];         // CMD 0x01: gate lines - 1 (lo, hi), scan byte; mirrorY ORs TB
  uint8_t borderWaveformInit;  // CMD 0x3C value written during controller init
  uint8_t halfRefreshTemp;     // CMD 0x1A forced temperature for the warmed HALF waveform
  // Display Update Control 2 (0x22) sequence per refresh type.
  uint8_t fullSeq;  // FULL: OTP full waveform, temp load, powers down after
  uint8_t halfSeq;  // HALF: same, with the forced temperature (no load)
  uint8_t fastSeq;  // FAST: OTP mode-2 differential, rails stay up
  // No grayscale LUT is characterised for this panel yet; nothing is advertised.
  bool overlayGrayscale = false;
  bool absoluteGrayscale = false;
};

// GDEY042T81 values. Panel mounting comes from BoardProfile.orientation.
const Ssd1683Config& ssd1683DefaultConfig();

class Ssd1683Driver : public PanelDriver {
 public:
  explicit Ssd1683Driver(const Ssd1683Config& cfg = ssd1683DefaultConfig());

  uint32_t spiHz() const override;
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveHigh; }
  PanelGeometry geometry() const override { return {_w, _h, _wb, _bufferSize}; }

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  // The host asserts that the glass still shows the frame the controller RAM
  // holds (deep sleep mode 1 retains RAM; RES# and SWRESET do not clear it), so
  // the first paint may be a differential against it instead of a promoted FULL.
  void skipInitialResync() override { _needsInitialFull = false; }

  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  // Deferred refresh: displayStart() writes RAM and fires MASTER_ACTIVATION;
  // displayFinish() waits on BUSY, re-seeds RED in single-buffer mode, and runs
  // any requested power-off.
  bool displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  void displayFinish(EpdBus& bus, const uint8_t* fb) override;
  bool supportsAsyncDisplay() const override { return true; }
  void displayWindow(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, uint16_t x, uint16_t y, uint16_t w,
                     uint16_t h, bool turnOff) override;

  void seedPreviousFrame(EpdBus& bus, const uint8_t* buf) override;

  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode mode = GrayscaleMode::Overlay) const override {
    (void)mode;
    return {};
  }

 private:
  void initController(EpdBus& bus);
  void setRamArea(EpdBus& bus, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRam(EpdBus& bus, uint8_t ramCmd, const uint8_t* data, uint32_t size);
  // async: fire MASTER_ACTIVATION and return without waiting on BUSY.
  void refresh(EpdBus& bus, RefreshMode mode, bool turnOff, bool async);
  // CLOCK/ANALOG off (0x83); no-op when the last sequence already powered down.
  void powerOffController(EpdBus& bus);
  void displayImpl(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff, bool async);

  const Ssd1683Config& _cfg;

  uint16_t _w;
  uint16_t _h;
  uint16_t _wb;
  uint32_t _bufferSize;

  // mirrorY flips the gate scan (TB). mirrorX is not applied: RAM X is byte
  // addressed here, so an X-decrement entry mode reverses bytes but not the
  // pixels inside them; a clean horizontal flip needs a host-side bit reverse.
  bool _mirrorY = false;

  bool _isScreenOn = false;
  // Async FAST cannot power down until the waveform completes; displayFinish()
  // consumes this.
  bool _pendingPowerOff = false;
  // Async single-buffer FAST: RED must be re-seeded with the new frame after the
  // waveform, never during it.
  bool _pendingFrameSync = false;
  // First paint after begin(): a differential cannot clear whatever is on the
  // glass at boot, so FAST is promoted to FULL once. skipInitialResync() clears
  // it when the host knows the glass still shows the frame in controller RAM.
  bool _needsInitialFull = false;
};

// Singleton accessor (Meyers, zero-heap).
PanelDriver& ssd1683Driver();

}  // namespace freeink

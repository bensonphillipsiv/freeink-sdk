#include "Ssd1683Driver.h"

#include <BoardConfig.h>

#include <cstring>
#include <vector>

namespace freeink {
namespace {

// SSD1683 command set.
constexpr uint8_t CMD_DRIVER_OUTPUT_CONTROL = 0x01;
constexpr uint8_t CMD_DEEP_SLEEP = 0x10;
constexpr uint8_t CMD_DATA_ENTRY_MODE = 0x11;
constexpr uint8_t CMD_SOFT_RESET = 0x12;
constexpr uint8_t CMD_TEMP_SENSOR_CONTROL = 0x18;
constexpr uint8_t CMD_WRITE_TEMP = 0x1A;
constexpr uint8_t CMD_MASTER_ACTIVATION = 0x20;
constexpr uint8_t CMD_DISPLAY_UPDATE_CTRL1 = 0x21;
constexpr uint8_t CMD_DISPLAY_UPDATE_CTRL2 = 0x22;
constexpr uint8_t CMD_WRITE_RAM_BW = 0x24;
constexpr uint8_t CMD_WRITE_RAM_RED = 0x26;
constexpr uint8_t CMD_BORDER_WAVEFORM = 0x3C;
constexpr uint8_t CMD_SET_RAM_X_RANGE = 0x44;
constexpr uint8_t CMD_SET_RAM_Y_RANGE = 0x45;
constexpr uint8_t CMD_SET_RAM_X_COUNTER = 0x4E;
constexpr uint8_t CMD_SET_RAM_Y_COUNTER = 0x4F;

constexpr uint8_t CTRL1_NORMAL = 0x00;
constexpr uint8_t CTRL1_BYPASS_RED = 0x40;  // RED read as 0: non-differential
constexpr uint8_t CTRL1_SINGLE_CHIP = 0x00;
constexpr uint8_t CTRL2_POWER_OFF = 0x83;   // ANALOG_OFF | CLOCK_OFF
constexpr uint8_t CTRL2_OFF_BITS = 0x03;    // a sequence with both ends powered down
constexpr uint8_t DATA_ENTRY_XINC_YINC = 0x03;
constexpr uint8_t TEMP_SENSOR_INTERNAL = 0x80;
constexpr uint8_t SCAN_TB_FLIP = 0x01;      // OR into the scan byte for mirrorY
constexpr uint8_t DEEP_SLEEP_MODE1 = 0x01;  // RAM retained; RES# needed to wake

}  // namespace

const Ssd1683Config& ssd1683DefaultConfig() {
  // GDEY042T81 (GxEPD2_420_GDEY042T81):
  // INIT: 01=2B 01 00, 3C=01, 18=80, 11=03
  // FULL: 21=40 00, 22=F7, 20
  // HALF: 21=40 00, 1A=6E (2024 panel, ok for 2023), 22=D7, 20
  // PART: 21=00 00, 22=FC, 20
  static const Ssd1683Config cfg = {
      {0x2B, 0x01, 0x00},
      0x01,
      0x6E,
      0xF7,
      0xD7,
      0xFC,
  };
  return cfg;
}

Ssd1683Driver::Ssd1683Driver(const Ssd1683Config& cfg)
    : _cfg(cfg),
      _w(BoardConfig::ACTIVE.displayWidth),
      _h(BoardConfig::ACTIVE.displayHeight),
      _wb(BoardConfig::ACTIVE.displayWidth / 8),
      _bufferSize(static_cast<uint32_t>(BoardConfig::ACTIVE.displayWidth / 8) * BoardConfig::ACTIVE.displayHeight),
      _mirrorY(BoardConfig::ACTIVE.orientation.mirrorY) {}

uint32_t Ssd1683Driver::spiHz() const {
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : 4000000;
}

void Ssd1683Driver::begin(EpdBus& bus) {
  bus.reset();
  initController(bus);
  // A differential cannot clear whatever the glass shows at boot.
  _needsInitialFull = true;
}

void Ssd1683Driver::beginFromDeepSleep(EpdBus& bus) {
  (void)bus;
  _panelAsleep = true;
  _isScreenOn = false;
  _pendingPowerOff = false;
  _pendingFrameSync = false;
  _needsInitialFull = false;
}

void Ssd1683Driver::ensureAwake(EpdBus& bus) {
  if (!_panelAsleep) return;
  bus.reset();
  initController(bus);
}

void Ssd1683Driver::initController(EpdBus& bus) {
  bus.cmd(CMD_SOFT_RESET);
  // Datasheet: 10 ms after SWRESET. BUSY may not have asserted by the first
  // sample, so the wait alone is not enough.
  delay(10);
  bus.waitBusy(" CMD_SOFT_RESET");

  bus.cmd(CMD_DRIVER_OUTPUT_CONTROL);
  bus.data(_cfg.muxLines[0]);
  bus.data(_cfg.muxLines[1]);
  bus.data(_mirrorY ? (_cfg.muxLines[2] | SCAN_TB_FLIP) : _cfg.muxLines[2]);

  bus.cmd(CMD_BORDER_WAVEFORM);
  bus.data(_cfg.borderWaveformInit);

  bus.cmd(CMD_TEMP_SENSOR_CONTROL);
  bus.data(TEMP_SENSOR_INTERNAL);

  setRamArea(bus, 0, 0, _w, _h);

  _isScreenOn = false;
  _pendingPowerOff = false;
  _pendingFrameSync = false;
  _panelAsleep = false;
}

void Ssd1683Driver::setRamArea(EpdBus& bus, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  // X is addressed in bytes, Y in gate lines; both increment.
  const uint16_t yEnd = y + h - 1;

  bus.cmd(CMD_DATA_ENTRY_MODE);
  bus.data(DATA_ENTRY_XINC_YINC);

  bus.cmd(CMD_SET_RAM_X_RANGE);
  bus.data(x / 8);
  bus.data((x + w - 1) / 8);

  bus.cmd(CMD_SET_RAM_Y_RANGE);
  bus.data(y % 256);
  bus.data(y / 256);
  bus.data(yEnd % 256);
  bus.data(yEnd / 256);

  bus.cmd(CMD_SET_RAM_X_COUNTER);
  bus.data(x / 8);

  bus.cmd(CMD_SET_RAM_Y_COUNTER);
  bus.data(y % 256);
  bus.data(y / 256);
}

void Ssd1683Driver::writeRam(EpdBus& bus, uint8_t ramCmd, const uint8_t* data, uint32_t size) {
  bus.cmd(ramCmd);
  bus.data(data, static_cast<uint16_t>(size));
}

void Ssd1683Driver::refresh(EpdBus& bus, RefreshMode mode, bool turnOff, bool async) {
  _pendingPowerOff = false;
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL1);
  bus.data(mode == RefreshMode::Fast ? CTRL1_NORMAL : CTRL1_BYPASS_RED);
  bus.data(CTRL1_SINGLE_CHIP);

  uint8_t seq = _cfg.fastSeq;
  if (mode == RefreshMode::Full) {
    seq = _cfg.fullSeq;
  } else if (mode == RefreshMode::Half) {
    // Forced temperature selects the shorter warm waveform from OTP.
    bus.cmd(CMD_WRITE_TEMP);
    bus.data(_cfg.halfRefreshTemp);
    seq = _cfg.halfSeq;
  }

  // F7/D7/FC all carry CLOCK_ON|ANALOG_ON, so no separate power-on is needed.
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL2);
  bus.data(seq);
  bus.cmd(CMD_MASTER_ACTIVATION);
  if (!async) bus.waitRefreshComplete("refresh");

  // F7/D7 end with ANALOG_OFF|CLOCK_OFF; FC leaves the rails up.
  _isScreenOn = (seq & CTRL2_OFF_BITS) != CTRL2_OFF_BITS;
  _pendingPowerOff = turnOff && _isScreenOn;
}

void Ssd1683Driver::powerOffController(EpdBus& bus) {
  // Never true while asleep (begin paths and deepSleep() clear it), so this
  // cannot talk to a sleeping controller.
  if (!_isScreenOn) return;
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL2);
  bus.data(CTRL2_POWER_OFF);
  bus.cmd(CMD_MASTER_ACTIVATION);
  bus.waitBusy(" display power-down");
  _isScreenOn = false;
}

void Ssd1683Driver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  displayImpl(bus, fb, prev, mode, turnOff, /*async=*/false);
}

bool Ssd1683Driver::displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode,
                                 bool turnOff) {
  displayImpl(bus, fb, prev, mode, turnOff, /*async=*/true);
  return true;
}

void Ssd1683Driver::displayFinish(EpdBus& bus, const uint8_t* fb) {
  bus.waitRefreshComplete("refresh");
  if (_pendingFrameSync && fb) {
    setRamArea(bus, 0, 0, _w, _h);
    writeRam(bus, CMD_WRITE_RAM_RED, fb, _bufferSize);
  }
  _pendingFrameSync = false;
  if (_pendingPowerOff) {
    _pendingPowerOff = false;
    powerOffController(bus);
  }
}

void Ssd1683Driver::displayImpl(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff,
                                bool async) {
  if (!fb) return;
  ensureAwake(bus);
  // A FULL or HALF the caller asked for already clears the glass; only FAST
  // needs promoting.
  if (_needsInitialFull) {
    if (mode == RefreshMode::Fast) mode = RefreshMode::Full;
    _needsInitialFull = false;
  }

  setRamArea(bus, 0, 0, _w, _h);
  writeRam(bus, CMD_WRITE_RAM_BW, fb, _bufferSize);
  if (mode != RefreshMode::Fast) {
    // Both planes, so the next differential starts from this frame.
    writeRam(bus, CMD_WRITE_RAM_RED, fb, _bufferSize);
  } else if (prev != nullptr) {
    // Dual-buffer: RED holds the previous frame for the differential compare.
    // Single-buffer: RED already holds it from the last refresh.
    writeRam(bus, CMD_WRITE_RAM_RED, prev, _bufferSize);
  }

  refresh(bus, mode, turnOff, async);

  // FC does not copy BW into RED, so in single-buffer mode re-seed RED with the
  // displayed frame as the next baseline. Async defers this to displayFinish().
  const bool needsSync = mode == RefreshMode::Fast && prev == nullptr;
  if (async) {
    _pendingFrameSync = needsSync;
    return;
  }
  if (needsSync) {
    setRamArea(bus, 0, 0, _w, _h);
    writeRam(bus, CMD_WRITE_RAM_RED, fb, _bufferSize);
  }
  if (_pendingPowerOff) {
    _pendingPowerOff = false;
    powerOffController(bus);
  }
}

void Ssd1683Driver::displayWindow(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, uint16_t x, uint16_t y,
                                  uint16_t w, uint16_t h, bool turnOff) {
  if (w == 0 || h == 0 || x + w > _w || y + h > _h) return;
  if (x % 8 != 0 || w % 8 != 0) return;  // RAM X is byte addressed
  if (!fb) return;
  ensureAwake(bus);

  if (_needsInitialFull) {
    display(bus, fb, prev, RefreshMode::Full, turnOff);
    return;
  }

  const uint16_t windowWidthBytes = w / 8;
  const uint32_t windowBufferSize = static_cast<uint32_t>(windowWidthBytes) * h;
  auto copyWindow = [&](const uint8_t* src, std::vector<uint8_t>& dst) {
    dst.resize(windowBufferSize);
    for (uint16_t row = 0; row < h; row++) {
      const uint32_t srcOffset = static_cast<uint32_t>(y + row) * _wb + (x / 8);
      memcpy(&dst[static_cast<uint32_t>(row) * windowWidthBytes], &src[srcOffset], windowWidthBytes);
    }
  };

  std::vector<uint8_t> windowBuffer;
  copyWindow(fb, windowBuffer);
  setRamArea(bus, x, y, w, h);
  writeRam(bus, CMD_WRITE_RAM_BW, windowBuffer.data(), windowBufferSize);
  if (prev != nullptr) {
    std::vector<uint8_t> previousWindow;
    copyWindow(prev, previousWindow);
    writeRam(bus, CMD_WRITE_RAM_RED, previousWindow.data(), windowBufferSize);
  }

  refresh(bus, RefreshMode::Fast, turnOff, /*async=*/false);

  if (prev == nullptr) {
    setRamArea(bus, x, y, w, h);
    writeRam(bus, CMD_WRITE_RAM_RED, windowBuffer.data(), windowBufferSize);
  }
  setRamArea(bus, 0, 0, _w, _h);
  if (_pendingPowerOff) {
    _pendingPowerOff = false;
    powerOffController(bus);
  }
}

void Ssd1683Driver::seedPreviousFrame(EpdBus& bus, const uint8_t* buf) {
  if (!buf) return;
  ensureAwake(bus);
  setRamArea(bus, 0, 0, _w, _h);
  writeRam(bus, CMD_WRITE_RAM_RED, buf, _bufferSize);
}

void Ssd1683Driver::deepSleep(EpdBus& bus) {
  // Already asleep: a second 0x10 would need RES# + init first, and deep sleep
  // from that state is what darkens the glass. Send nothing.
  if (_panelAsleep) return;
  powerOffController(bus);
  bus.cmd(CMD_DEEP_SLEEP);
  bus.data(DEEP_SLEEP_MODE1);
  // Only RES# wakes the controller: the next command runs ensureAwake(). The
  // next paint is promoted to FULL unless the host calls skipInitialResync()
  // or wakes through beginFromDeepSleep().
  _panelAsleep = true;
  _needsInitialFull = true;
}

// Per-board config injection, same idiom as the other drivers: define
// `const Ssd1683Config& yourConfig();` in namespace freeink and build with
// -DFREEINK_SSD1683_CONFIG=yourConfig.
#ifdef FREEINK_SSD1683_CONFIG
const Ssd1683Config& FREEINK_SSD1683_CONFIG();
static const Ssd1683Config& ssd1683ActiveConfig() { return FREEINK_SSD1683_CONFIG(); }
#else
static const Ssd1683Config& ssd1683ActiveConfig() { return ssd1683DefaultConfig(); }
#endif

PanelDriver& ssd1683Driver() {
  static Ssd1683Driver instance(ssd1683ActiveConfig());
  return instance;
}

}  // namespace freeink

// Glyph v1 (GDEY042T81 / SSD1683): register sequences per FIRMWARE.md §5,
// checked against the recording bus.
#include <cassert>
#include <cstdio>
#include <vector>
#include <BoardConfig.h>
#define private public
#include "src/driver/Ssd1683Driver.h"
#undef private

using namespace freeink;
using Bytes = std::vector<uint8_t>;
using Write = EpdBus::Write;

static bool is(const Write& w, uint8_t command, const Bytes& bytes) { return w.command == command && w.bytes == bytes; }

// Index of the first write of `command` at or after `from`; -1 if none.
static int find(const EpdBus& bus, uint8_t command, size_t from = 0) {
  for (size_t i = from; i < bus.writes.size(); ++i)
    if (bus.writes[i].command == command) return static_cast<int>(i);
  return -1;
}

static int count(const EpdBus& bus, uint8_t command) {
  int n = 0;
  for (const Write& w : bus.writes) n += w.command == command;
  return n;
}

// The last three writes are CTRL1, [1A], CTRL2, 0x20 for `ctrl1`/`ctrl2`.
static void expectTail(const EpdBus& bus, const Bytes& ctrl1, uint8_t ctrl2, int temp) {
  const size_t n = bus.writes.size();
  assert(is(bus.writes[n - 1], 0x20, {}));
  assert(is(bus.writes[n - 2], 0x22, {ctrl2}));
  if (temp >= 0) {
    assert(is(bus.writes[n - 3], 0x1A, {static_cast<uint8_t>(temp)}));
    assert(is(bus.writes[n - 4], 0x21, ctrl1));
  } else {
    assert(is(bus.writes[n - 3], 0x21, ctrl1));
    assert(count(bus, 0x1A) == 0);
  }
}

static void expectFullScreenArea(const EpdBus& bus, size_t at) {
  assert(is(bus.writes[at], 0x11, {0x03}));
  assert(is(bus.writes[at + 1], 0x44, {0, 49}));
  assert(is(bus.writes[at + 2], 0x45, {0, 0, 0x2B, 0x01}));
  assert(is(bus.writes[at + 3], 0x4E, {0}));
  assert(is(bus.writes[at + 4], 0x4F, {0, 0}));
}

int main() {
  Ssd1683Driver d;
  const Bytes fb(15000, 0x5A), prev(15000, 0xC3);

  // Bus configuration and geometry come from the Glyph profile.
  const PanelGeometry g = d.geometry();
  assert(g.width == 400 && g.height == 300 && g.widthBytes == 50 && g.bufferSize == 15000);
  assert(d.spiHz() == 4000000);
  assert(d.busyPolarity() == BusyPolarity::ActiveHigh);
  assert(d.supportsAsyncDisplay());
  assert(!d.grayscaleCapabilities(GrayscaleMode::Overlay).supported());
  assert(!d.grayscaleCapabilities(GrayscaleMode::Absolute).supported());

  // begin: RES#, SWRESET + wait, MUX, border, temp sensor, full-screen RAM area.
  EpdBus bus;
  d.begin(bus);
  assert(bus.writes.size() == 9 && bus.waits == 1 && bus.resets == 1);
  assert(is(bus.writes[0], 0x12, {}));
  assert(is(bus.writes[1], 0x01, {0x2B, 0x01, 0x00}));
  assert(is(bus.writes[2], 0x3C, {0x01}));
  assert(is(bus.writes[3], 0x18, {0x80}));
  expectFullScreenArea(bus, 4);

  // First paint after begin: FAST is promoted to FULL.
  bus.clear();
  d.display(bus, fb.data(), prev.data(), RefreshMode::Fast, false);
  expectTail(bus, {0x40, 0x00}, 0xF7, -1);
  assert(bus.waits == 1 && !d._isScreenOn);

  // Deep-sleep wake: begin() re-arms the promotion; skipInitialResync() (the
  // host knows the glass still shows the frame in controller RAM) lets the
  // first paint stay a differential.
  bus.clear();
  d.deepSleep(bus);
  d.begin(bus);
  d.skipInitialResync();
  bus.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  {
    const int act = find(bus, 0x20);
    assert(act > 0 && is(bus.writes[act - 1], 0x22, {0xFC}) && is(bus.writes[act - 2], 0x21, {0x00, 0x00}));
  }

  // FULL: both planes get fb, bypass RED, F7.
  bus.clear();
  d.display(bus, fb.data(), prev.data(), RefreshMode::Full, false);
  expectFullScreenArea(bus, 0);
  assert(is(bus.writes[5], 0x24, fb) && is(bus.writes[6], 0x26, fb));
  assert(bus.writes.size() == 10);
  expectTail(bus, {0x40, 0x00}, 0xF7, -1);
  assert(bus.waits == 1);

  // HALF: warmed full (forced temperature, D7).
  bus.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Half, false);
  assert(is(bus.writes[5], 0x24, fb) && is(bus.writes[6], 0x26, fb));
  expectTail(bus, {0x40, 0x00}, 0xD7, 0x6E);
  assert(bus.waits == 1);

  // FAST, dual-buffer: BW = fb, RED = prev, differential FC. No resync after.
  bus.clear();
  d.display(bus, fb.data(), prev.data(), RefreshMode::Fast, false);
  assert(is(bus.writes[5], 0x24, fb) && is(bus.writes[6], 0x26, prev));
  assert(bus.writes.size() == 10);
  expectTail(bus, {0x00, 0x00}, 0xFC, -1);
  assert(bus.waits == 1 && d._isScreenOn);

  // FAST, single-buffer: only BW before activation, then RED re-seeded with fb.
  bus.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  const int act = find(bus, 0x20);
  assert(act > 0 && is(bus.writes[act - 1], 0x22, {0xFC}));
  assert(find(bus, 0x24) >= 0 && find(bus, 0x24) < act && is(bus.writes[find(bus, 0x24)], 0x24, fb));
  assert(find(bus, 0x26) > act && is(bus.writes[find(bus, 0x26)], 0x26, fb));
  expectFullScreenArea(bus, act + 1);
  assert(bus.waits == 1);

  // FAST + turnOff: power down after the waveform.
  bus.clear();
  d.display(bus, fb.data(), prev.data(), RefreshMode::Fast, true);
  {
    const size_t n = bus.writes.size();
    assert(is(bus.writes[n - 2], 0x22, {0x83}) && is(bus.writes[n - 1], 0x20, {}));
    assert(bus.waits == 2 && !d._isScreenOn);
  }

  // Window: byte-aligned RAM area, window rows only, FC, full area restored.
  Bytes framed(15000, 0x00);
  for (uint16_t row = 0; row < 20; ++row)
    for (uint16_t b = 0; b < 2; ++b) framed[(10 + row) * 50 + 1 + b] = static_cast<uint8_t>(row * 2 + b + 1);
  bus.clear();
  d.displayWindow(bus, framed.data(), nullptr, 8, 10, 16, 20, false);
  assert(is(bus.writes[0], 0x11, {0x03}));
  assert(is(bus.writes[1], 0x44, {1, 2}));
  assert(is(bus.writes[2], 0x45, {10, 0, 29, 0}));
  assert(is(bus.writes[3], 0x4E, {1}));
  assert(is(bus.writes[4], 0x4F, {10, 0}));
  assert(bus.writes[5].command == 0x24 && bus.writes[5].bytes.size() == 40);
  for (uint8_t i = 0; i < 40; ++i) assert(bus.writes[5].bytes[i] == i + 1);
  const int wAct = find(bus, 0x20);
  assert(is(bus.writes[wAct - 1], 0x22, {0xFC}) && is(bus.writes[wAct - 2], 0x21, {0x00, 0x00}));
  assert(find(bus, 0x26) > wAct && bus.writes[find(bus, 0x26)].bytes == bus.writes[5].bytes);
  expectFullScreenArea(bus, bus.writes.size() - 5);
  assert(bus.waits == 1);

  // Window with prev: RED gets prev's window rows before activation.
  bus.clear();
  d.displayWindow(bus, framed.data(), prev.data(), 8, 10, 16, 20, false);
  assert(bus.writes[6].command == 0x26 && bus.writes[6].bytes == Bytes(40, 0xC3));
  assert(find(bus, 0x26) < find(bus, 0x20) && count(bus, 0x26) == 1);

  // Unaligned or out-of-range windows are rejected without touching the bus.
  bus.clear();
  d.displayWindow(bus, fb.data(), nullptr, 3, 10, 16, 20, false);
  d.displayWindow(bus, fb.data(), nullptr, 8, 10, 12, 20, false);
  d.displayWindow(bus, fb.data(), nullptr, 392, 0, 16, 20, false);
  assert(bus.writes.empty() && bus.waits == 0);

  // seedPreviousFrame: RED only, no refresh.
  bus.clear();
  d.seedPreviousFrame(bus, prev.data());
  assert(bus.writes.size() == 6 && is(bus.writes[5], 0x26, prev) && find(bus, 0x20) < 0);

  // Async: start fires the waveform without waiting; finish waits once.
  bus.clear();
  assert(d.displayStart(bus, fb.data(), prev.data(), RefreshMode::Fast, false));
  assert(bus.waits == 0 && bus.writes.back().command == 0x20);
  const size_t started = bus.writes.size();
  d.displayFinish(bus, fb.data());
  assert(bus.waits == 1 && bus.writes.size() == started);

  // Async single-buffer + turnOff: RED re-seed and power-off deferred to finish.
  bus.clear();
  assert(d.displayStart(bus, fb.data(), nullptr, RefreshMode::Fast, true));
  assert(bus.waits == 0 && bus.writes.back().command == 0x20 && count(bus, 0x26) == 0);
  d.displayFinish(bus, fb.data());
  assert(count(bus, 0x26) == 1 && is(bus.writes[find(bus, 0x26)], 0x26, fb));
  assert(is(bus.writes[bus.writes.size() - 2], 0x22, {0x83}) && bus.waits == 2);

  // Deep sleep: power off (rails up after a FAST), then 0x10 01.
  bus.clear();
  d.display(bus, fb.data(), prev.data(), RefreshMode::Fast, false);
  bus.clear();
  d.deepSleep(bus);
  assert(bus.writes.size() == 3);
  assert(is(bus.writes[0], 0x22, {0x83}) && is(bus.writes[1], 0x20, {}) && is(bus.writes[2], 0x10, {0x01}));

  // Wake: begin() re-arms the first-paint FULL, also for a window.
  bus.clear();
  d.begin(bus);
  bus.clear();
  d.displayWindow(bus, fb.data(), nullptr, 8, 10, 16, 20, false);
  expectTail(bus, {0x40, 0x00}, 0xF7, -1);
  assert(count(bus, 0x24) == 1 && bus.writes[find(bus, 0x24)].bytes.size() == 15000);

  // Lazy wake: beginFromDeepSleep() leaves the controller in the deep sleep the
  // last boot sent. No RES#, no SWRESET, no registers.
  Ssd1683Driver lazy;
  bus.clear();
  lazy.beginFromDeepSleep(bus);
  assert(bus.writes.empty() && bus.resets == 0 && bus.waits == 0 && lazy.panelAsleep());

  // A quiet cycle: deepSleep() before any paint sends nothing (the panel is
  // already asleep; deep sleep from reset + init is what darkens the glass).
  lazy.deepSleep(bus);
  assert(bus.writes.empty() && bus.resets == 0 && bus.waits == 0);

  // The first paint after a lazy wake runs RES# + SWRESET + init, then the
  // frame, and stays a FAST differential (the retained image is the baseline).
  lazy.beginFromDeepSleep(bus);
  bus.clear();
  lazy.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  assert(bus.resets == 1 && count(bus, 0x12) == 1 && is(bus.writes[0], 0x12, {}));
  assert(is(bus.writes[1], 0x01, {0x2B, 0x01, 0x00}) && is(bus.writes[2], 0x3C, {0x01}));
  assert(is(bus.writes[3], 0x18, {0x80}));
  expectFullScreenArea(bus, 4);
  expectFullScreenArea(bus, 9);
  assert(is(bus.writes[14], 0x24, fb));
  {
    const int a = find(bus, 0x20);
    assert(a > 0 && is(bus.writes[a - 1], 0x22, {0xFC}) && is(bus.writes[a - 2], 0x21, {0x00, 0x00}));
    assert(find(bus, 0x26) > a && is(bus.writes[find(bus, 0x26)], 0x26, fb));
  }
  assert(bus.waits == 2 && lazy._isScreenOn);

  // A second paint does not re-init.
  assert(!lazy.panelAsleep());
  bus.clear();
  lazy.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  assert(bus.resets == 0 && count(bus, 0x12) == 0 && bus.waits == 1);

  // Sleep after a refresh is unchanged: power off, 0x10 01. A second deepSleep()
  // sends nothing.
  bus.clear();
  lazy.deepSleep(bus);
  assert(bus.writes.size() == 3 && bus.resets == 0);
  assert(is(bus.writes[0], 0x22, {0x83}) && is(bus.writes[1], 0x20, {}) && is(bus.writes[2], 0x10, {0x01}));
  assert(lazy.panelAsleep());
  bus.clear();
  lazy.deepSleep(bus);
  assert(bus.writes.empty());

  // A paint after deepSleep() in the same boot wakes the controller first and,
  // without skipInitialResync(), is promoted to FULL.
  bus.clear();
  lazy.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  assert(bus.resets == 1 && count(bus, 0x12) == 1);
  expectTail(bus, {0x40, 0x00}, 0xF7, -1);

  // ... and with skipInitialResync() before the lazy init, it stays FAST: the
  // init does not clobber the host's request.
  lazy.deepSleep(bus);
  lazy.skipInitialResync();
  bus.clear();
  lazy.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  assert(bus.resets == 1 && count(bus, 0x12) == 1);
  {
    const int a = find(bus, 0x20);
    assert(is(bus.writes[a - 1], 0x22, {0xFC}));
  }

  // Every entry point wakes the controller: a window (FAST, not promoted),
  // displayStart(), seedPreviousFrame().
  lazy.deepSleep(bus);
  lazy.beginFromDeepSleep(bus);
  bus.clear();
  lazy.displayWindow(bus, framed.data(), nullptr, 8, 10, 16, 20, false);
  assert(bus.resets == 1 && is(bus.writes[0], 0x12, {}));
  assert(is(bus.writes[find(bus, 0x20) - 1], 0x22, {0xFC}));
  assert(bus.writes[find(bus, 0x24)].bytes.size() == 40);

  lazy.deepSleep(bus);
  lazy.beginFromDeepSleep(bus);
  bus.clear();
  assert(lazy.displayStart(bus, fb.data(), nullptr, RefreshMode::Fast, false));
  assert(bus.resets == 1 && is(bus.writes[0], 0x12, {}) && bus.writes.back().command == 0x20);
  assert(is(bus.writes[bus.writes.size() - 2], 0x22, {0xFC}));
  lazy.displayFinish(bus, fb.data());

  lazy.deepSleep(bus);
  lazy.beginFromDeepSleep(bus);
  bus.clear();
  lazy.seedPreviousFrame(bus, prev.data());
  assert(bus.resets == 1 && is(bus.writes[0], 0x12, {}) && is(bus.writes.back(), 0x26, prev));

  // A cold begin() always resets and re-arms the first-paint FULL.
  lazy.deepSleep(bus);
  bus.clear();
  lazy.begin(bus);
  assert(bus.resets == 1 && bus.writes.size() == 9 && lazy._needsInitialFull && !lazy._panelAsleep);

  // The PanelDriver default for drivers without a lazy path: begin() +
  // skipInitialResync() at once.
  struct Eager : PanelDriver {
    int begins = 0, skips = 0;
    uint32_t spiHz() const override { return 0; }
    BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveHigh; }
    PanelGeometry geometry() const override { return {}; }
    void begin(EpdBus&) override { ++begins; }
    void deepSleep(EpdBus&) override {}
    void display(EpdBus&, const uint8_t*, const uint8_t*, RefreshMode, bool) override {}
    void skipInitialResync() override { skips += begins; }
  } eager;
  eager.beginFromDeepSleep(bus);
  assert(eager.begins == 1 && eager.skips == 1);

  // mirrorY sets the TB bit in the scan byte.
  BoardConfig::ACTIVE.orientation.mirrorY = true;
  Ssd1683Driver flipped;
  bus.clear();
  flipped.begin(bus);
  assert(is(bus.writes[1], 0x01, {0x2B, 0x01, 0x01}));
  BoardConfig::ACTIVE.orientation.mirrorY = false;

  std::puts("ssd1683 host test: ok");
  return 0;
}

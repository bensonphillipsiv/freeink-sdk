#include <BoardConfig.h>
#include <Imu.h>
#include <Wire.h>
#include <cassert>
#include <cmath>
#include <cstdio>
// LIS2DW12 power-on state: WHO_AM_I 0x44, CTRL2 = 0x04 (IF_ADD_INC default 1).
static void chip(uint8_t address, uint8_t who = 0x44) {
  Wire.devices[address] = true;
  Wire.regs[address][0x0F] = who;
  Wire.regs[address][0x21] = 0x04;
}
// Writes left-justified 14-bit samples (value << 2) little-endian at OUT_X_L.
static void xyz14(uint8_t address, int16_t x, int16_t y, int16_t z) {
  uint8_t reg = 0x28;
  for (int16_t v : {x, y, z}) {
    const uint16_t word = uint16_t(v) << 2;
    Wire.regs[address][reg++] = uint8_t(word);
    Wire.regs[address][reg++] = uint8_t(word >> 8);
  }
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
int main() {
  Wire.lis2dw12 = true;
  Imu imu;
  Imu::Sample sample = {42, 42, 42, 42, 42, 42};
  static_assert(BoardConfig::DEFAULT_DEVICE.board == BoardConfig::Board::Glyph);
  static_assert(BoardConfig::GLYPH_V1.sensors.imuType == BoardConfig::ImuType::Lis2dw12);
  static_assert(BoardConfig::GLYPH_V1.sensors.imuAddr == 0x19);
  static_assert(BoardConfig::MAX_FRAMEBUFFER_BYTES == 15000);
  assert(BoardConfig::isGlyph());
  assert(!imu.read(sample) && !imu.sleep() && !imu.wake() && !imu.hasGyroscope());

  // Absent, then present with the wrong identity: no sensor writes.
  assert(!imu.begin() && !imu.present());
  chip(0x19, 0x33);  // LIS3DH identity at the same address is not a LIS2DW12
  Wire.writes.clear();
  assert(!imu.begin() && !imu.present() && Wire.writes.empty());

  // Correct identity at 0x19: CTRL2, CTRL6, CTRL1 written in that order.
  chip(0x19);
  Wire.regs[0x19][0x25] = 0xFF;  // prove CTRL6 is actually written
  Wire.writes.clear();
  assert(imu.begin() && imu.present() && !imu.hasGyroscope());
  assert(Wire.writes.size() == 3);
  assert(Wire.writes[0].addr == 0x19 && Wire.writes[0].reg == 0x21 && Wire.writes[0].value == 0x0C);
  assert(Wire.writes[1].addr == 0x19 && Wire.writes[1].reg == 0x25 && Wire.writes[1].value == 0x00);
  assert(Wire.writes[2].addr == 0x19 && Wire.writes[2].reg == 0x20 && Wire.writes[2].value == 0x54);

  // read(): 14-bit left-justified, 0.244 mg/digit at ±2 g; gyro zero.
  xyz14(0x19, 4096, -4096, 2048);
  assert(imu.read(sample) && Wire.lastRead == 0x28);
  assert(near(sample.ax, 0.999424f) && near(sample.ay, -0.999424f) && near(sample.az, 0.499712f));
  assert(sample.gx == 0 && sample.gy == 0 && sample.gz == 0);
  xyz14(0x19, -8192, 8191, -1);
  assert(imu.read(sample));
  assert(near(sample.ax, -1.998848f) && near(sample.ay, 1.998604f) && near(sample.az, -0.000244f));
  // I2C failures surface as false and leave the sample untouched.
  Wire.shortRead = true;
  assert(!imu.read(sample) && near(sample.ax, -1.998848f));
  Wire.shortRead = false;
  Wire.failReadReg = 0x28;
  assert(!imu.read(sample));
  Wire.failReadReg = -1;

  // sleep() powers down (CTRL1 ODR=0000), wake() restores 100 Hz HP.
  Wire.failWriteReg = 0x20;
  assert(!imu.sleep());
  Wire.failWriteReg = -1;
  Wire.writes.clear();
  assert(imu.sleep() && Wire.regs[0x19][0x20] == 0x00 && !imu.read(sample));
  assert(Wire.writes.size() == 1 && Wire.writes[0].reg == 0x20);
  Wire.failWriteReg = 0x20;
  assert(!imu.wake() && !imu.read(sample));
  Wire.failWriteReg = -1;
  assert(imu.wake() && Wire.regs[0x19][0x20] == 0x54 && imu.read(sample));
  assert(Wire.regs[0x19][0x21] == 0x0C && Wire.regs[0x19][0x25] == 0x00);

  // Each setup write can fail; begin() reports absent and retries cleanly.
  for (int reg : {0x21, 0x25, 0x20}) {
    Wire.failWriteReg = reg;
    assert(!imu.begin() && !imu.present() && !imu.read(sample));
    Wire.failWriteReg = -1;
    assert(imu.begin() && imu.present());
  }

  // Wrong identity at the profile address: alternate SA0 address 0x18 is used
  // and 0x19 receives no writes.
  chip(0x19, 0x33);
  chip(0x18);
  Wire.writes.clear();
  assert(imu.begin() && imu.present());
  for (auto w : Wire.writes) assert(w.addr == 0x18);
  assert(Wire.regs[0x18][0x20] == 0x54);
  xyz14(0x18, 0, 4096, 0);
  assert(imu.read(sample) && near(sample.ay, 0.999424f));
  assert(imu.sleep() && Wire.regs[0x18][0x20] == 0x00);
  assert(imu.wake() && Wire.regs[0x18][0x20] == 0x54);
  puts("LIS2DW12 identity/address, CTRL2/CTRL6/CTRL1 setup, 14-bit scaling, failures and sleep/wake passed");
}

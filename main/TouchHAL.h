#pragma once
#include <stdint.h>

struct TouchPoint {
  bool pressed = false;
  int16_t x = 0, y = 0;
};

struct MultiTouch {
  static const int MAXP = 5;
  uint8_t count = 0;
  int16_t x[MAXP] = {0};
  int16_t y[MAXP] = {0};
  uint8_t id[MAXP] = {0};
};

// ST77922 in-cell touch (I2C 0x55, 16-bit registers, 100kHz).
// Acquisition runs in its own task on core 0, gated by the INT pin so the
// I2C bus is only used while a finger is down. The UI core reads a snapshot.
class TouchHAL {
public:
  bool begin(uint8_t rotation);
  void startTask();                 // spawn poller on core 0
  void setRotation(uint8_t r) { _rot = r & 3; }
  TouchPoint read();                // lock-free latest snapshot, no I2C
  MultiTouch readMulti();           // all active points (up to panel max)
  uint8_t maxPoints() const { return _maxPoints; }
private:
  void poll();                      // one acquisition step (task context)
  static void taskEntry(void* self);

  uint8_t _rot = 0;
  uint8_t _maxPoints = 1;
  bool _ok = false;
  // snapshot shared core0 -> core1
  volatile bool _pressed = false;
  volatile int16_t _sx = 0, _sy = 0;
  volatile uint8_t _mtCount = 0;
  volatile int16_t _mx[MultiTouch::MAXP];
  volatile int16_t _my[MultiTouch::MAXP];
  volatile uint8_t _mid[MultiTouch::MAXP];
  uint32_t _lastActiveMs = 0;
};

extern TouchHAL Touch;

// Shared I2C bus guard (touch + ES8311 codec live on the same bus).
void i2cLock();
void i2cUnlock();

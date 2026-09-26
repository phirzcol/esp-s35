#include "TouchHAL.h"
#include "HW_Pins.h"
#include <Arduino.h>
#include <Wire.h>

TouchHAL Touch;

static SemaphoreHandle_t s_i2cMutex = nullptr;
void i2cLock()   { if (s_i2cMutex) xSemaphoreTake(s_i2cMutex, portMAX_DELAY); }
void i2cUnlock() { if (s_i2cMutex) xSemaphoreGive(s_i2cMutex); }

// ST77922 in-cell touch: protocol per vendor Arduino lib (Install libraries/ST77922_TOUCH)
static const uint8_t TP_ADDR = 0x55;
static const uint16_t REG_STATUS      = 0x0001;
static const uint16_t REG_MAX_TOUCHES = 0x0009;
static const uint16_t REG_TOUCH_INFO  = 0x0010;
static const uint16_t REG_TOUCH_P0    = 0x0014;
static const uint8_t MAX_POINTS = 10;
static const uint32_t RELEASE_HOLD_MS = 60;   // keep pressed across INT pulses

static bool readReg(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TP_ADDR, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool TouchHAL::begin(uint8_t rotation) {
  _rot = rotation & 3;
  if (!s_i2cMutex) s_i2cMutex = xSemaphoreCreateMutex();

  pinMode(PIN_TP_INT, INPUT);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, HIGH);
  digitalWrite(PIN_TP_RST, LOW);
  delay(100);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(100);

  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 100000);
  Wire.setTimeOut(20);

  // Wait for controller boot: STATUS low nibble must clear
  uint8_t st = 0xFF;
  uint32_t t0 = millis();
  do {
    if (!readReg(REG_STATUS, &st, 1)) { _ok = false; return false; }
  } while ((st & 0x0F) && millis() - t0 < 1000);

  uint8_t maxTouches = 0;
  _ok = readReg(REG_MAX_TOUCHES, &maxTouches, 1);
  _maxPoints = (maxTouches > 0 && maxTouches <= MAX_POINTS) ? maxTouches : 1;
  return _ok;
}

void TouchHAL::poll() {
  uint32_t now = millis();

  // INT is low while a touch is active; skip the bus entirely when idle.
  if (digitalRead(PIN_TP_INT) == HIGH) {
    if (_pressed && now - _lastActiveMs > RELEASE_HOLD_MS) _pressed = false;
    return;
  }

  i2cLock();
  uint8_t info = 0;
  bool got = false;
  int16_t rx = 0, ry = 0;
  uint8_t mtN = 0;
  int16_t mrx[MultiTouch::MAXP], mry[MultiTouch::MAXP];
  uint8_t mids[MultiTouch::MAXP];
  if (readReg(REG_TOUCH_INFO, &info, 1) && (info & 0x08)) {
    uint8_t d[7 * MAX_POINTS] = {0};
    // Drain the full point buffer; controller stalls otherwise
    if (readReg(REG_TOUCH_P0, d, 7 * (size_t)_maxPoints)) {
      for (uint8_t i = 0; i < _maxPoints; i++) {
        const uint8_t* q = &d[i * 7];
        if (!(q[0] & 0x80)) continue;
        int16_t px = ((q[0] & 0x3F) << 8) | q[1];
        int16_t py = ((q[2] & 0x3F) << 8) | q[3];
        if (!got) { rx = px; ry = py; got = true; }
        if (mtN < MultiTouch::MAXP) {
          mrx[mtN] = px;
          mry[mtN] = py;
          mids[mtN] = i;
          mtN++;
        }
      }
    }
  }
  i2cUnlock();

  if (got) {
    int16_t x = 0, y = 0;
    switch (_rot) {
      case 0: x = rx;                    y = ry;                    break;
      case 1: x = ry;                    y = LCD_NATIVE_W - 1 - rx; break;
      case 2: x = LCD_NATIVE_W - 1 - rx; y = LCD_NATIVE_H - 1 - ry; break;
      case 3: x = LCD_NATIVE_H - 1 - ry; y = rx;                    break;
    }
    _sx = x;
    _sy = y;
    for (uint8_t i = 0; i < mtN; i++) {
      int16_t px = mrx[i], py = mry[i], ox, oy;
      switch (_rot) {
        default:
        case 0: ox = px;                    oy = py;                    break;
        case 1: ox = py;                    oy = LCD_NATIVE_W - 1 - px; break;
        case 2: ox = LCD_NATIVE_W - 1 - px; oy = LCD_NATIVE_H - 1 - py; break;
        case 3: ox = LCD_NATIVE_H - 1 - py; oy = px;                    break;
      }
      _mx[i] = ox;
      _my[i] = oy;
      _mid[i] = mids[i];
    }
    _mtCount = mtN;
    _pressed = true;
    _lastActiveMs = now;
  } else if (_pressed && now - _lastActiveMs > RELEASE_HOLD_MS) {
    _pressed = false;
    _mtCount = 0;
  }
}

void TouchHAL::taskEntry(void* self) {
  TouchHAL* t = (TouchHAL*)self;
  for (;;) {
    t->poll();
    // Faster cadence while touching, relaxed when idle
    vTaskDelay(pdMS_TO_TICKS(t->_pressed ? 15 : 25));
  }
}

void TouchHAL::startTask() {
  if (!_ok) return;
  xTaskCreatePinnedToCore(taskEntry, "touchTask", 4096, this, 3, nullptr, 0);
}

TouchPoint TouchHAL::read() {
  TouchPoint p;
  p.pressed = _pressed;
  p.x = _sx;
  p.y = _sy;
  return p;
}

MultiTouch TouchHAL::readMulti() {
  MultiTouch m;
  uint8_t n = _mtCount;
  if (n > MultiTouch::MAXP) n = MultiTouch::MAXP;
  m.count = _pressed ? n : 0;
  for (uint8_t i = 0; i < m.count; i++) {
    m.x[i] = _mx[i];
    m.y[i] = _my[i];
    m.id[i] = _mid[i];
  }
  return m;
}

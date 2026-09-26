#pragma once
#include <Arduino.h>

// Shared system state updated by the background task on core 0 and read by the UI
// on core 1. Simple types only; written atomically enough for status display.
struct SystemStatus {
  volatile uint16_t batteryMv = 0;
  volatile uint8_t  batteryPct = 0;
  volatile bool     wifiConnected = false;
  volatile bool     timeValid = false;
  volatile int8_t   wifiRssi = 0;
};

struct Notification {
  char text[48];
  uint32_t atMs;
};

class NotificationCenter {
public:
  void post(const char* text);
  int  count() const { return _count; }
  const Notification* get(int i) const { return (i >= 0 && i < _count) ? &_items[i] : nullptr; }
  void clear() { _count = 0; }
private:
  static const int MAXN = 8;
  Notification _items[MAXN];
  int _count = 0;
};

extern SystemStatus Sys;
extern NotificationCenter Notify;

// Core-0 background task: battery sampling, WiFi management, NTP sync.
void systemTaskStart();
// Ask the background task to (re)connect WiFi with current Settings credentials.
void systemWifiApply();

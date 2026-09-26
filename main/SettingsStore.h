#pragma once
#include <Arduino.h>

// Global settings persisted in NVS (Preferences).
class SettingsStore {
public:
  // Display
  uint8_t brightness = 200;      // 0..255
  uint8_t rotation = 0;          // 0..3
  bool    screensaverOn = true;
  uint16_t screensaverSec = 60;  // idle seconds before dim
  // Audio
  uint8_t volume = 60;           // 0..100
  bool    visualizerOn = true;
  uint8_t visSens = 50;          // radial visualizer sensitivity 0..100
  uint8_t visAmp = 50;           // radial visualizer amplitude 0..100
  // Network
  bool   wifiOn = true;
  String wifiSsid;
  String wifiPass;
  bool   btOn = false;
  // Clock
  int32_t utcOffsetMin = 0;
  // Weather (wttr.in)
  String weatherCity;
  bool weatherMetric = false;

  void begin();
  void save();
};

extern SettingsStore Settings;

#include "SystemTask.h"
#include "SettingsStore.h"
#include "HW_Pins.h"
#include <WiFi.h>
#include <time.h>

SystemStatus Sys;
NotificationCenter Notify;

static volatile bool s_wifiDirty = true;

void NotificationCenter::post(const char* text) {
  if (_count == MAXN) {
    memmove(&_items[0], &_items[1], sizeof(Notification) * (MAXN - 1));
    _count--;
  }
  strlcpy(_items[_count].text, text, sizeof(_items[_count].text));
  _items[_count].atMs = millis();
  _count++;
}

void systemWifiApply() { s_wifiDirty = true; }

static uint8_t pctFromMv(uint16_t mv) {
  // LiPo rough curve, 3300..4200mV
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;
  return (uint8_t)(((uint32_t)(mv - 3300) * 100) / 900);
}

static void sysTask(void*) {
  bool wasConnected = false;
  bool ntpDone = false;
  uint32_t lastBat = 0;

  for (;;) {
    uint32_t now = millis();

    if (now - lastBat > 3000 || lastBat == 0) {
      lastBat = now;
      uint32_t acc = 0;
      for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(PIN_BAT_ADC);
      uint16_t mv = (uint16_t)((acc / 8) * 2);  // onboard divider halves VBAT
      Sys.batteryMv = mv;
      Sys.batteryPct = pctFromMv(mv);
    }

    if (s_wifiDirty) {
      s_wifiDirty = false;
      ntpDone = false;
      if (Settings.wifiOn && Settings.wifiSsid.length()) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);   // modem power-save bursts couple into the audio amp
        WiFi.begin(Settings.wifiSsid.c_str(), Settings.wifiPass.c_str());
      } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
    }

    bool conn = (WiFi.status() == WL_CONNECTED);
    Sys.wifiConnected = conn;
    Sys.wifiRssi = conn ? (int8_t)WiFi.RSSI() : 0;
    if (conn && !wasConnected) Notify.post("WiFi connected");
    if (!conn && wasConnected) Notify.post("WiFi disconnected");
    wasConnected = conn;

    if (conn && !ntpDone) {
      configTime(Settings.utcOffsetMin * 60, 0, "pool.ntp.org", "time.nist.gov");
      ntpDone = true;
    }
    if (ntpDone && !Sys.timeValid) {
      struct tm ti;
      if (getLocalTime(&ti, 0)) Sys.timeValid = true;
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void systemTaskStart() {
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  // Keep amp and RGB LED off until an app needs them
  pinMode(PIN_AMP_EN, OUTPUT);
  digitalWrite(PIN_AMP_EN, HIGH);  // active low -> off
  xTaskCreatePinnedToCore(sysTask, "sysTask", 6144, nullptr, 1, nullptr, 0);
}

#pragma once
#include "AppBase.h"

// Weather via wttr.in (mechanism from D:\done code\esp32-rss-reader\weather.cpp).
// Settings: city + units, via the in-app gear.
class WeatherApp : public AppBase {
public:
  const char* name() const override { return "Weather"; }
  void onEnter() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
  bool hasSettings() const override { return true; }
  void onSettings() override;   // city entry

private:
  enum St { IDLE, FETCHING, OK, FAIL };
  volatile St _st = IDLE;
  char _now[24] = "";          // "+22C"
  char _cond[48] = "";
  char _line2[64] = "";        // humidity/wind
  char _forecast[3][56];       // next days, one line each
  uint32_t _fetchedAt = 0;
  bool _naming = false;
  bool _wasPressed = false;

  void startFetch();
  static void fetchTask(void* self);
};

extern WeatherApp Weather;

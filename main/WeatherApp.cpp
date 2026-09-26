#include "WeatherApp.h"
#include "SettingsStore.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include <WiFi.h>
#include <HTTPClient.h>

WeatherApp Weather;

// One wttr.in request: current temp/condition/humidity/wind + 3-day min/max.
// %t temp, %C condition, %h humidity, %w wind; forecast via v2-style format lines.
void WeatherApp::fetchTask(void* self) {
  WeatherApp* a = (WeatherApp*)self;
  HTTPClient http;
  http.setTimeout(12000);
  char url[224];
  String city = Settings.weatherCity;
  city.replace(" ", "+");
  snprintf(url, sizeof(url), "http://wttr.in/%s?format=%%t|%%C|%%h|%%w&%s",
           city.c_str(), Settings.weatherMetric ? "m" : "u");
  bool ok = false;
  if (http.begin(url)) {
    if (http.GET() == 200) {
      String r = http.getString();
      r.trim();
      int p1 = r.indexOf('|'), p2 = r.indexOf('|', p1 + 1), p3 = r.indexOf('|', p2 + 1);
      if (p1 > 0 && p2 > p1 && p3 > p2) {
        String t = r.substring(0, p1);
        t.replace("\xC2\xB0", "");   // strip degree symbol bytes
        strlcpy(a->_now, t.c_str(), sizeof(a->_now));
        strlcpy(a->_cond, r.substring(p1 + 1, p2).c_str(), sizeof(a->_cond));
        snprintf(a->_line2, sizeof(a->_line2), "Humidity %s   Wind %s",
                 r.substring(p2 + 1, p3).c_str(), r.substring(p3 + 1).c_str());
        ok = true;
      }
    }
    http.end();
  }

  // 3-day min/max in a second lightweight request (JSON is too heavy; use the
  // one-line-per-day text endpoint)
  for (int i = 0; i < 3; i++) a->_forecast[i][0] = 0;
  if (ok && http.begin((String("http://wttr.in/") + city + "?format=j2&" +
                        (Settings.weatherMetric ? "m" : "u")).c_str())) {
    // j2 = trimmed JSON; parse mintempX/maxtempX crudely to avoid a JSON lib dependency
    if (http.GET() == 200) {
      String j = http.getString();
      const char* key = Settings.weatherMetric ? "tempC" : "tempF";
      char minKey[24], maxKey[24];
      snprintf(minKey, sizeof(minKey), "\"mintemp%c\":\"", Settings.weatherMetric ? 'C' : 'F');
      snprintf(maxKey, sizeof(maxKey), "\"maxtemp%c\":\"", Settings.weatherMetric ? 'C' : 'F');
      (void)key;
      int pos = 0;
      for (int d = 0; d < 3; d++) {
        int mn = j.indexOf(minKey, pos);
        int mx = j.indexOf(maxKey, pos);
        if (mn < 0 || mx < 0) break;
        int mnEnd = j.indexOf('"', mn + strlen(minKey));
        int mxEnd = j.indexOf('"', mx + strlen(maxKey));
        String lo = j.substring(mn + strlen(minKey), mnEnd);
        String hi = j.substring(mx + strlen(maxKey), mxEnd);
        const char* dayName = d == 0 ? "Today" : (d == 1 ? "Tomorrow" : "Day after");
        snprintf(a->_forecast[d], sizeof(a->_forecast[d]), "%s: %s to %s%c",
                 dayName, lo.c_str(), hi.c_str(), Settings.weatherMetric ? 'C' : 'F');
        pos = max(mnEnd, mxEnd);
      }
    }
    http.end();
  }

  a->_fetchedAt = millis();
  a->_st = ok ? OK : FAIL;
  vTaskDelete(nullptr);
}

void WeatherApp::startFetch() {
  if (_st == FETCHING) return;
  if (!Sys.wifiConnected) { Notify.post("WiFi not connected"); _st = FAIL; return; }
  if (Settings.weatherCity.length() == 0) {
    Notify.post("Set a city (gear button)");
    _st = FAIL;
    return;
  }
  _st = FETCHING;
  xTaskCreatePinnedToCore(fetchTask, "wxFetch", 8192, this, 1, nullptr, 0);
}

void WeatherApp::onEnter() {
  _naming = false;
  // refresh if stale (>15 min) or never fetched
  if (_st != OK || millis() - _fetchedAt > 15 * 60 * 1000UL) startFetch();
}

void WeatherApp::onSettings() {
  Kbd.open("City (e.g. Denver or 80202)", Settings.weatherCity.c_str());
  _naming = true;
}

void WeatherApp::draw(lgfx::LGFX_Sprite& g) {
  if (_naming && Kbd.done()) {
    if (!Kbd.cancelled()) {
      Settings.weatherCity = Kbd.text();
      Settings.save();
      startFetch();
    }
    Kbd.close();
    _naming = false;
  }

  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_center);

  if (Settings.weatherCity.length() == 0) {
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Tap the gear to set your city", c.w / 2, c.y + c.h / 2);
    return;
  }
  g.setTextColor(Theme::ACCENT);
  g.drawString(Settings.weatherCity.c_str(), c.w / 2, c.y + 14);

  if (_st == FETCHING) {
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Fetching...", c.w / 2, c.y + c.h / 2);
    int t = (millis() / 120) % 12;
    for (int i = 0; i < 12; i++) {
      float a = i * PI / 6;
      g.fillCircle(c.w / 2 + (int)(26 * cosf(a)), c.y + c.h / 2 + 42 + (int)(26 * sinf(a)), 3,
                   i == t ? Theme::ACCENT : Theme::PANEL_HI);
    }
    return;
  }
  if (_st != OK) {
    g.setTextColor(Theme::BAD);
    g.drawString("Weather unavailable", c.w / 2, c.y + c.h / 2 - 10);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Tap to retry", c.w / 2, c.y + c.h / 2 + 14);
    return;
  }

  // Big current temp
  g.setTextSize(4);
  g.setTextColor(Theme::TEXT);
  g.drawString(_now, c.w / 2, c.y + 52);
  g.setTextSize(1);
  g.setTextColor(Theme::ACCENT);
  g.drawString(_cond, c.w / 2, c.y + 110);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString(_line2, c.w / 2, c.y + 132);

  int y = c.y + 175;
  g.drawFastHLine(20, y - 12, c.w - 40, Theme::PANEL_HI);
  g.setTextDatum(lgfx::top_left);
  for (int d = 0; d < 3; d++) {
    if (!_forecast[d][0]) continue;
    g.setTextColor(d == 0 ? Theme::TEXT : Theme::TEXT_DIM);
    g.drawString(_forecast[d], 24, y);
    y += 24;
  }

  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::PANEL_HI);
  char age[48];
  snprintf(age, sizeof(age), "updated %lu min ago - tap to refresh - %s",
           (unsigned long)(millis() - _fetchedAt) / 60000UL,
           Settings.weatherMetric ? "metric" : "imperial");
  g.drawString(age, c.w / 2, c.y + c.h - 40);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString("long-press: toggle units", c.w / 2, c.y + c.h - 22);
}

void WeatherApp::handleTouch(int x, int y, bool pressed) {
  static uint32_t pressStart = 0;
  if (pressed && !_wasPressed) pressStart = millis();
  if (!pressed && _wasPressed) {
    if (millis() - pressStart > 700) {
      Settings.weatherMetric = !Settings.weatherMetric;
      Settings.save();
      startFetch();
    } else {
      startFetch();
    }
  }
  _wasPressed = pressed;
}

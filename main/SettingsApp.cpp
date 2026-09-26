#include "SettingsApp.h"
#include "SettingsStore.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include "TouchHAL.h"
#include "AudioHAL.h"
#include <WiFi.h>

SettingsApp SettingsUI;

static const char* MENU_ITEMS[] = {"Display", "WiFi", "Bluetooth", "Audio", "About"};
static const int MENU_N = 5;

static UIRect rowRect(int i, int rowH = 44) {
  UIRect c = AppBase::contentArea();
  return { c.x + 8, c.y + 8 + i * (rowH + 6), c.w - 16, rowH };
}

void SettingsApp::onEnter() { _page = PG_MENU; _scanState = 0; }

bool SettingsApp::onBack() {
  if (_page != PG_MENU) { _page = PG_MENU; _scanState = 0; return true; }
  return false;
}

void SettingsApp::drawMenu(lgfx::LGFX_Sprite& g) {
  for (int i = 0; i < MENU_N; i++) {
    UIRect r = rowRect(i);
    g.fillRoundRect(r.x, r.y, r.w, r.h, 6, Theme::PANEL);
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::TEXT);
    g.drawString(MENU_ITEMS[i], r.x + 14, r.y + r.h / 2);
    g.fillTriangle(r.x + r.w - 18, r.y + r.h / 2 - 6, r.x + r.w - 18, r.y + r.h / 2 + 6,
                   r.x + r.w - 10, r.y + r.h / 2, Theme::TEXT_DIM);
  }
}

void SettingsApp::touchMenu(int x, int y, bool tap) {
  if (!tap) return;
  for (int i = 0; i < MENU_N; i++) {
    if (rowRect(i).contains(x, y)) {
      _page = (Page)(PG_DISPLAY + i);
      _scanState = 0;
      return;
    }
  }
}

// ---- Display page ----
void SettingsApp::drawDisplay(lgfx::LGFX_Sprite& g) {
  UIRect r0 = rowRect(0, 50);
  UIDraw::slider(g, r0, Settings.brightness, 8, 255, "Brightness");
  UIRect r1 = rowRect(1);
  UIDraw::toggle(g, r1, "Screensaver", Settings.screensaverOn);
  UIRect r2 = rowRect(2, 50);
  UIDraw::slider(g, r2, Settings.screensaverSec, 10, 600, "Timeout (sec)");
}

void SettingsApp::touchDisplay(int x, int y, bool tap, bool pressed) {
  UIRect r0 = rowRect(0, 50);
  if (pressed && r0.contains(x, y)) {
    Settings.brightness = (uint8_t)UIDraw::sliderHit(r0, x, Settings.brightness, 8, 255);
    Gfx.setBrightness(Settings.brightness);
    Settings.save();
    return;
  }
  UIRect r2 = rowRect(2, 50);
  if (pressed && r2.contains(x, y)) {
    Settings.screensaverSec = (uint16_t)UIDraw::sliderHit(r2, x, Settings.screensaverSec, 10, 600);
    Settings.save();
    return;
  }
  if (tap && rowRect(1).contains(x, y)) {
    Settings.screensaverOn = !Settings.screensaverOn;
    Settings.save();
  }
}

// ---- WiFi page ----
void SettingsApp::drawWifi(lgfx::LGFX_Sprite& g) {
  UIRect r0 = rowRect(0);
  UIDraw::toggle(g, r0, "WiFi enabled", Settings.wifiOn);

  UIRect r1 = rowRect(1);
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(Theme::TEXT_DIM);
  char st[64];
  if (Sys.wifiConnected)
    snprintf(st, sizeof(st), "Connected: %s (%d dBm)", Settings.wifiSsid.c_str(), Sys.wifiRssi);
  else if (Settings.wifiSsid.length())
    snprintf(st, sizeof(st), "Saved: %s (not connected)", Settings.wifiSsid.c_str());
  else
    snprintf(st, sizeof(st), "No network configured");
  g.drawString(st, r1.x, r1.y + r1.h / 2);

  UIRect r2 = rowRect(2);
  UIDraw::button(g, r2, _scanState == 1 ? "Scanning..." : "Scan networks");

  if (_scanState == 2) {
    int shown = _scanCount > 5 ? 5 : _scanCount;
    for (int i = 0; i < shown; i++) {
      UIRect rr = rowRect(3 + i, 34);
      g.fillRoundRect(rr.x, rr.y, rr.w, rr.h, 4, Theme::PANEL);
      g.setTextDatum(lgfx::middle_left);
      g.setTextColor(Theme::TEXT);
      char line[48];
      snprintf(line, sizeof(line), "%s (%d)", WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i));
      g.drawString(line, rr.x + 10, rr.y + rr.h / 2);
    }
  }
}

void SettingsApp::touchWifi(int x, int y, bool tap) {
  if (!tap) return;
  if (rowRect(0).contains(x, y)) {
    Settings.wifiOn = !Settings.wifiOn;
    Settings.save();
    systemWifiApply();
    return;
  }
  if (rowRect(2).contains(x, y) && _scanState != 1) {
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true);
    _scanState = 1;
    return;
  }
  if (_scanState == 2) {
    int shown = _scanCount > 5 ? 5 : _scanCount;
    for (int i = 0; i < shown; i++) {
      if (rowRect(3 + i, 34).contains(x, y)) {
        _scanSel = i;
        char title[64];
        snprintf(title, sizeof(title), "Password for %s", WiFi.SSID(i).c_str());
        Kbd.open(title);
        return;
      }
    }
  }
}

// ---- Bluetooth page (radio management placeholder until BT apps arrive) ----
void SettingsApp::drawBluetooth(lgfx::LGFX_Sprite& g) {
  UIRect r0 = rowRect(0);
  UIDraw::toggle(g, r0, "Bluetooth enabled", Settings.btOn);
  UIRect r1 = rowRect(1);
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString("Radio starts when a BT app is used", r1.x, r1.y + r1.h / 2);
}

void SettingsApp::touchBluetooth(int x, int y, bool tap) {
  if (tap && rowRect(0).contains(x, y)) {
    Settings.btOn = !Settings.btOn;
    Settings.save();
  }
}

// ---- Audio page ----
void SettingsApp::drawAudio(lgfx::LGFX_Sprite& g) {
  UIRect r0 = rowRect(0, 50);
  UIDraw::slider(g, r0, Settings.volume, 0, 100, "Volume");
  UIRect r1 = rowRect(1);
  UIDraw::toggle(g, r1, "Visualizer", Settings.visualizerOn);
  UIRect r2 = rowRect(2, 50);
  UIDraw::slider(g, r2, Settings.visSens, 0, 100, "Vis sensitivity");
  UIRect r3 = rowRect(3, 50);
  UIDraw::slider(g, r3, Settings.visAmp, 0, 100, "Vis amplitude");
}

void SettingsApp::touchAudio(int x, int y, bool tap, bool pressed) {
  UIRect r0 = rowRect(0, 50);
  if (pressed && r0.contains(x, y)) {
    Settings.volume = (uint8_t)UIDraw::sliderHit(r0, x, Settings.volume, 0, 100);
    AudioSys.setVolume(Settings.volume);   // live if engine is running
    Settings.save();
    return;
  }
  UIRect r2 = rowRect(2, 50);
  if (pressed && r2.contains(x, y)) {
    Settings.visSens = (uint8_t)UIDraw::sliderHit(r2, x, Settings.visSens, 0, 100);
    Settings.save();
    return;
  }
  UIRect r3 = rowRect(3, 50);
  if (pressed && r3.contains(x, y)) {
    Settings.visAmp = (uint8_t)UIDraw::sliderHit(r3, x, Settings.visAmp, 0, 100);
    Settings.save();
    return;
  }
  if (tap && rowRect(1).contains(x, y)) {
    Settings.visualizerOn = !Settings.visualizerOn;
    Settings.save();
  }
}

// ---- About page ----
void SettingsApp::drawAbout(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::TEXT);
  int y = c.y + 12, x = c.x + 12;
  char line[64];
  g.drawString("ES3C35P Framework", x, y); y += 22;
  g.setTextColor(Theme::TEXT_DIM);
  snprintf(line, sizeof(line), "Heap free: %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  g.drawString(line, x, y); y += 18;
  snprintf(line, sizeof(line), "PSRAM free: %u KB", (unsigned)(ESP.getFreePsram() / 1024));
  g.drawString(line, x, y); y += 18;
  snprintf(line, sizeof(line), "Battery: %u mV (%u%%)", Sys.batteryMv, Sys.batteryPct);
  g.drawString(line, x, y); y += 18;
  snprintf(line, sizeof(line), "CPU: %u MHz  Flash: %u MB", (unsigned)getCpuFrequencyMhz(),
           (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  g.drawString(line, x, y);
}

void SettingsApp::draw(lgfx::LGFX_Sprite& g) {
  // Async scan completion
  if (_scanState == 1) {
    int n = WiFi.scanComplete();
    if (n >= 0) { _scanCount = n; _scanState = 2; }
  }
  // Keyboard result for WiFi password
  if (Kbd.done()) {
    if (!Kbd.cancelled() && _scanSel >= 0) {
      Settings.wifiSsid = WiFi.SSID(_scanSel);
      Settings.wifiPass = Kbd.text();
      Settings.wifiOn = true;
      Settings.save();
      systemWifiApply();
      Notify.post("Connecting to WiFi...");
    }
    _scanSel = -1;
    Kbd.close();
  }

  switch (_page) {
    case PG_MENU:    drawMenu(g); break;
    case PG_DISPLAY: drawDisplay(g); break;
    case PG_WIFI:    drawWifi(g); break;
    case PG_BT:      drawBluetooth(g); break;
    case PG_AUDIO:   drawAudio(g); break;
    case PG_ABOUT:   drawAbout(g); break;
  }
  if (_page != PG_MENU) {
    // back-to-menu hint
    g.setTextDatum(lgfx::bottom_left);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("< Back returns to settings menu", 8, Gfx.height() - Theme::NAV_H - 4);
  }
}

void SettingsApp::handleTouch(int x, int y, bool pressed) {
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;
  switch (_page) {
    case PG_MENU:    touchMenu(x, y, tap); break;
    case PG_DISPLAY: touchDisplay(x, y, tap, pressed); break;
    case PG_WIFI:    touchWifi(x, y, tap); break;
    case PG_BT:      touchBluetooth(x, y, tap); break;
    case PG_AUDIO:   touchAudio(x, y, tap, pressed); break;
    case PG_ABOUT:   break;
  }
}

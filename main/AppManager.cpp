#include "AppManager.h"
#include "DisplayHAL.h"
#include "SystemTask.h"
#include "SettingsStore.h"
#include <time.h>

AppManager Apps;

UIRect AppBase::contentArea() {
  return { 0, Theme::STATUS_H, Gfx.width(), Gfx.height() - Theme::STATUS_H - Theme::NAV_H };
}

void AppManager::begin(AppBase* launcher) {
  _stack[0] = launcher;
  _depth = 1;
  launcher->onEnter();
}

AppBase* AppManager::current() { return _depth ? _stack[_depth - 1] : nullptr; }

void AppManager::push(AppBase* app) {
  if (_depth >= MAXDEPTH || !app) return;
  if (current()) current()->onExit();
  _stack[_depth++] = app;
  app->onEnter();
}

void AppManager::back() {
  if (current() && current()->onBack()) return;
  if (_depth <= 1) return;
  current()->onExit();
  _depth--;
  current()->onEnter();
}

void AppManager::home() {
  if (_depth <= 1) return;
  current()->onExit();
  _depth = 1;
  current()->onEnter();
}

void AppManager::drawStatusBar(lgfx::LGFX_Sprite& g) {
  int W = g.width();
  g.fillRect(0, 0, W, Theme::STATUS_H, Theme::PANEL);
  g.drawFastHLine(0, Theme::STATUS_H - 1, W, Theme::PANEL_HI);

  // Clock (left)
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(Theme::TEXT);
  char ts[16] = "--:--";
  if (Sys.timeValid) {
    struct tm ti;
    if (getLocalTime(&ti, 0)) snprintf(ts, sizeof(ts), "%02d:%02d", ti.tm_hour, ti.tm_min);
  }
  g.drawString(ts, 6, Theme::STATUS_H / 2);

  // App name (center)
  g.setTextDatum(lgfx::middle_center);
  g.setTextColor(Theme::TEXT_DIM);
  if (current()) g.drawString(current()->name(), W / 2, Theme::STATUS_H / 2);

  // Right side: notif count, BT, WiFi, battery
  int x = W - 4;

  // Battery: outline + fill
  int bw = 24, bh = 12, by = (Theme::STATUS_H - bh) / 2;
  x -= bw;
  uint8_t pct = Sys.batteryPct;
  uint16_t bc = pct > 40 ? Theme::GOOD : (pct > 15 ? Theme::WARN : Theme::BAD);
  g.drawRoundRect(x, by, bw - 3, bh, 2, Theme::TEXT_DIM);
  g.fillRect(x + bw - 3, by + 3, 2, bh - 6, Theme::TEXT_DIM);
  int fw = ((bw - 7) * pct) / 100;
  if (fw > 0) g.fillRect(x + 2, by + 2, fw, bh - 4, bc);

  // WiFi arcs
  x -= 22;
  int cx = x + 9, cy = Theme::STATUS_H - 7;
  uint16_t wc = Sys.wifiConnected ? Theme::ACCENT : Theme::PANEL_HI;
  g.fillCircle(cx, cy, 2, wc);
  g.drawCircle(cx, cy, 5, wc);
  g.drawCircle(cx, cy, 8, wc);
  g.fillRect(cx - 9, cy + 1, 19, 9, Theme::PANEL); // mask lower half -> arcs

  // BT glyph (simple)
  x -= 14;
  uint16_t btc = Settings.btOn ? Theme::ACCENT : Theme::PANEL_HI;
  int bx = x + 5, byy = 5;
  g.drawLine(bx, byy + 3, bx + 6, byy + 11, btc);
  g.drawLine(bx, byy + 11, bx + 6, byy + 3, btc);
  g.drawLine(bx + 3, byy, bx + 3, byy + 14, btc);
  g.drawLine(bx + 3, byy, bx + 6, byy + 3, btc);
  g.drawLine(bx + 3, byy + 14, bx + 6, byy + 11, btc);

  // Notification badge
  if (Notify.count() > 0) {
    x -= 18;
    g.fillCircle(x + 7, Theme::STATUS_H / 2, 7, Theme::WARN);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(TFT_BLACK);
    char n[4];
    snprintf(n, sizeof(n), "%d", Notify.count());
    g.drawString(n, x + 7, Theme::STATUS_H / 2);
  }
}

void AppManager::drawNavBar(lgfx::LGFX_Sprite& g) {
  int W = g.width(), H = g.height();
  int y = H - Theme::NAV_H;
  g.fillRect(0, y, W, Theme::NAV_H, Theme::PANEL);
  g.drawFastHLine(0, y, W, Theme::PANEL_HI);

  int cy = y + Theme::NAV_H / 2;
  int third = W / 3;

  // Back: left triangle
  g.fillTriangle(third / 2 - 6, cy, third / 2 + 6, cy - 8, third / 2 + 6, cy + 8, Theme::TEXT);
  // Home: circle
  g.drawCircle(W / 2, cy, 9, Theme::TEXT);
  g.fillCircle(W / 2, cy, 4, Theme::TEXT);
  // Settings gear (only when current app has settings): simple asterisk-gear
  AppBase* app = current();
  if (app && app->hasSettings()) {
    int gx = W - third / 2;
    for (int i = 0; i < 4; i++) {
      float a = i * 3.14159f / 4;
      g.drawLine(gx - (int)(9 * cosf(a)), cy - (int)(9 * sinf(a)),
                 gx + (int)(9 * cosf(a)), cy + (int)(9 * sinf(a)), Theme::TEXT);
    }
    g.fillCircle(gx, cy, 5, Theme::PANEL);
    g.drawCircle(gx, cy, 4, Theme::TEXT);
  }
}

void AppManager::drawChrome(lgfx::LGFX_Sprite& g) {
  drawStatusBar(g);
  drawNavBar(g);
}

void AppManager::tick(uint32_t dtMs) {
  AppBase* app = current();
  if (!app) return;
  app->update(dtMs);

  lgfx::LGFX_Sprite& g = Gfx.canvas();
  bool fs = app->wantsFullscreen();
  if (!fs) {
    g.fillRect(0, Theme::STATUS_H, g.width(), g.height() - Theme::STATUS_H - Theme::NAV_H, Theme::BG);
  } else if (app->wantsCanvasClear()) {
    g.fillScreen(TFT_BLACK);
  }
  app->draw(g);
  if (!fs) {
    drawChrome(g);
  } else {
    // Floating home dot, top-right corner
    g.fillCircle(g.width() - 14, 14, 10, Theme::PANEL);
    g.drawCircle(g.width() - 14, 14, 10, Theme::PANEL_HI);
    g.fillCircle(g.width() - 14, 14, 3, Theme::TEXT);
  }
  if (Kbd.isOpen()) Kbd.draw(g);
  Gfx.present();
}

void AppManager::handleTouch(int x, int y, bool pressed) {
  AppBase* app = current();
  if (!app) return;

  if (Kbd.isOpen()) {
    Kbd.handleTouch(x, y, pressed);
    return;
  }

  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;

  if (app->wantsFullscreen()) {
    if (tap && x > Gfx.width() - 28 && y < 28) { home(); return; }
    app->handleTouch(x, y, pressed);
    return;
  }

  int navY = Gfx.height() - Theme::NAV_H;
  if (y >= navY) {
    if (!tap) return;
    int third = Gfx.width() / 3;
    if (x < third) back();
    else if (x < 2 * third) home();
    else if (app->hasSettings()) app->onSettings();
    return;
  }
  if (y < Theme::STATUS_H) {
    if (tap) Notify.clear();
    return;
  }
  app->handleTouch(x, y, pressed);
}

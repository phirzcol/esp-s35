#include "LauncherApp.h"
#include "AppManager.h"
#include "SettingsApp.h"
#include "CalculatorApp.h"
#include "FileManagerApp.h"
#include "MP3PlayerApp.h"
#include "PongApp.h"
#include "BreakoutApp.h"
#include "NesApp.h"
#include "NotesApp.h"
#include "ImageViewerApp.h"
#include "RSSApp.h"
#include "WeatherApp.h"
#include "FileServerApp.h"
#include "RpgApp.h"
#include "SystemTask.h"
#include "DisplayHAL.h"

LauncherApp Launcher;

struct LaunchEntry {
  const char* label;
  AppBase* app;        // nullptr = not yet ported
};

// Ported apps get wired in here as they come online.
static LaunchEntry entries[] = {
  {"Settings",  nullptr /* set in draw-time init */},
  {"Calc",      nullptr},
  {"Files",     nullptr},
  {"Notes",     nullptr},
  {"Pong",      nullptr},
  {"Breakout",  nullptr},
  {"MP3",       nullptr},
  {"RSS",       nullptr},
  {"Calendar",  nullptr},
  {"Images",    nullptr},
  {"NES",       nullptr},
  {"Weather",   nullptr},
  {"Server",    nullptr},
  {"RPG",       nullptr},
};
static bool s_wired = false;
static void wire() {
  if (s_wired) return;
  entries[0].app = &SettingsUI;
  entries[1].app = &Calculator;
  entries[2].app = &FileMan;
  entries[3].app = &Notes;
  entries[4].app = &Pong;
  entries[5].app = &Breakout;
  entries[6].app = &MP3Player;
  entries[7].app = &RSS;
  entries[9].app = &ImageViewer;
  entries[10].app = &Nes;
  entries[11].app = &Weather;
  entries[12].app = &FileServer;
  entries[13].app = &Rpg;
  s_wired = true;
}

static const int COLS = 3;

static UIRect cellRect(int i) {
  UIRect c = AppBase::contentArea();
  int n = sizeof(entries) / sizeof(entries[0]);
  int rows = (n + COLS - 1) / COLS;
  int cw = c.w / COLS;
  int ch = c.h / rows;
  return { c.x + (i % COLS) * cw, c.y + (i / COLS) * ch, cw, ch };
}

static void drawIcon(lgfx::LGFX_Sprite& g, int i, int cx, int cy, bool enabled) {
  uint16_t col = enabled ? Theme::ACCENT : Theme::PANEL_HI;
  switch (i) {
    case 0: // Settings gear
      for (int k = 0; k < 4; k++) {
        float a = k * 3.14159f / 4;
        g.drawLine(cx - (int)(14 * cosf(a)), cy - (int)(14 * sinf(a)),
                   cx + (int)(14 * cosf(a)), cy + (int)(14 * sinf(a)), col);
      }
      g.fillCircle(cx, cy, 8, Theme::BG);
      g.drawCircle(cx, cy, 7, col);
      break;
    case 1: // Calc
      g.drawRoundRect(cx - 12, cy - 15, 24, 30, 3, col);
      g.drawFastHLine(cx - 8, cy - 8, 16, col);
      for (int r = 0; r < 3; r++)
        for (int cc = 0; cc < 3; cc++)
          g.fillRect(cx - 9 + cc * 8, cy - 2 + r * 7, 3, 3, col);
      break;
    case 2: // Files folder
      g.fillRoundRect(cx - 14, cy - 8, 28, 18, 2, col);
      g.fillRect(cx - 14, cy - 12, 12, 6, col);
      break;
    case 3: // Notes
      g.drawRoundRect(cx - 11, cy - 14, 22, 28, 2, col);
      for (int l = 0; l < 4; l++) g.drawFastHLine(cx - 7, cy - 8 + l * 6, 14, col);
      break;
    case 4: // Pong
      g.fillRect(cx - 16, cy - 10, 3, 12, col);
      g.fillRect(cx + 13, cy - 2, 3, 12, col);
      g.fillCircle(cx, cy, 3, col);
      break;
    case 5: // Breakout
      for (int r = 0; r < 2; r++)
        for (int cc = 0; cc < 3; cc++)
          g.fillRect(cx - 15 + cc * 11, cy - 13 + r * 7, 9, 5, col);
      g.fillRect(cx - 7, cy + 10, 14, 3, col);
      g.fillCircle(cx, cy + 2, 3, col);
      break;
    case 6: // MP3 note
      g.fillCircle(cx - 6, cy + 8, 5, col);
      g.fillCircle(cx + 8, cy + 5, 5, col);
      g.drawLine(cx - 2, cy + 7, cx - 2, cy - 12, col);
      g.drawLine(cx + 12, cy + 4, cx + 12, cy - 15, col);
      g.drawLine(cx - 2, cy - 12, cx + 12, cy - 15, col);
      break;
    case 7: // RSS
      g.fillCircle(cx - 10, cy + 10, 3, col);
      g.drawCircle(cx - 10, cy + 10, 9, col);
      g.drawCircle(cx - 10, cy + 10, 15, col);
      g.fillRect(cx - 26, cy + 11, 17, 16, Theme::BG);
      break;
    case 8: // Calendar
      g.drawRoundRect(cx - 14, cy - 12, 28, 26, 2, col);
      g.drawFastHLine(cx - 14, cy - 5, 28, col);
      g.fillRect(cx - 8, cy - 16, 3, 6, col);
      g.fillRect(cx + 5, cy - 16, 3, 6, col);
      break;
    case 9: // Images
      g.drawRoundRect(cx - 15, cy - 11, 30, 22, 2, col);
      g.fillCircle(cx - 7, cy - 4, 3, col);
      g.fillTriangle(cx - 10, cy + 9, cx, cy - 2, cx + 8, cy + 9, col);
      break;
    case 10: // NES gamepad
      g.fillRoundRect(cx - 17, cy - 8, 34, 17, 4, col);
      g.fillRect(cx - 12, cy - 4, 3, 9, Theme::BG);
      g.fillRect(cx - 15, cy - 1, 9, 3, Theme::BG);
      g.fillCircle(cx + 7, cy, 2, Theme::BG);
      g.fillCircle(cx + 13, cy, 2, Theme::BG);
      break;
    case 11: // Weather sun
      g.fillCircle(cx, cy, 8, col);
      for (int k = 0; k < 8; k++) {
        float a = k * PI / 4;
        g.drawLine(cx + (int)(11 * cosf(a)), cy + (int)(11 * sinf(a)),
                   cx + (int)(15 * cosf(a)), cy + (int)(15 * sinf(a)), col);
      }
      break;
    case 12: // Server globe
      g.drawCircle(cx, cy, 14, col);
      g.drawEllipse(cx, cy, 6, 14, col);
      g.drawFastHLine(cx - 14, cy, 28, col);
      g.drawFastHLine(cx - 12, cy - 7, 24, col);
      g.drawFastHLine(cx - 12, cy + 7, 24, col);
      break;
    case 13: // RPG sword
      g.drawLine(cx - 8, cy + 10, cx + 8, cy - 6, col);
      g.drawLine(cx - 7, cy + 11, cx + 9, cy - 5, col);
      g.fillTriangle(cx + 8, cy - 6, cx + 12, cy - 10, cx + 9, cy - 5, col);
      g.drawLine(cx - 10, cy + 4, cx - 4, cy + 10, col);   // guard
      g.fillCircle(cx - 12, cy + 14, 2, col);              // pommel
      break;
  }
}

void LauncherApp::draw(lgfx::LGFX_Sprite& g) {
  wire();
  int n = sizeof(entries) / sizeof(entries[0]);
  for (int i = 0; i < n; i++) {
    UIRect r = cellRect(i);
    bool en = entries[i].app != nullptr;
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2 - 8;
    drawIcon(g, i, cx, cy, en);
    g.setTextDatum(lgfx::top_center);
    g.setTextColor(en ? Theme::TEXT : Theme::TEXT_DIM);
    g.drawString(entries[i].label, cx, cy + 22);
  }
}

void LauncherApp::handleTouch(int x, int y, bool pressed) {
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;
  if (!tap) return;
  wire();
  int n = sizeof(entries) / sizeof(entries[0]);
  for (int i = 0; i < n; i++) {
    if (cellRect(i).contains(x, y)) {
      if (entries[i].app) Apps.push(entries[i].app);
      else Notify.post("App not installed yet");
      return;
    }
  }
}

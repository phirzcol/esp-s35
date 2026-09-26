#pragma once
#include "AppBase.h"

// Stack-based navigation with shared chrome (status bar + nav bar).
class AppManager {
public:
  void begin(AppBase* launcher);
  void push(AppBase* app);
  void back();
  void home();
  AppBase* current();

  // Called from loop() on the UI core.
  void tick(uint32_t dtMs);
  void handleTouch(int x, int y, bool pressed);

private:
  void drawChrome(lgfx::LGFX_Sprite& g);
  void drawStatusBar(lgfx::LGFX_Sprite& g);
  void drawNavBar(lgfx::LGFX_Sprite& g);

  static const int MAXDEPTH = 6;
  AppBase* _stack[MAXDEPTH] = {nullptr};
  int _depth = 0;
  bool _wasPressed = false;
  uint32_t _notifFlashUntil = 0;
};

extern AppManager Apps;

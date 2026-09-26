#pragma once
#include "AppBase.h"

// Home screen: grid of app icons. Apps not yet ported are shown dimmed.
class LauncherApp : public AppBase {
public:
  const char* name() const override { return "Home"; }
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
private:
  bool _wasPressed = false;
};

extern LauncherApp Launcher;

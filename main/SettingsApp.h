#pragma once
#include "AppBase.h"

class SettingsApp : public AppBase {
public:
  const char* name() const override { return "Settings"; }
  void onEnter() override;
  bool onBack() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
private:
  enum Page { PG_MENU, PG_DISPLAY, PG_WIFI, PG_BT, PG_AUDIO, PG_ABOUT };
  Page _page = PG_MENU;
  bool _wasPressed = false;
  bool _dragging = false;
  int  _dragTarget = -1;
  // wifi scan state
  int _scanState = 0;      // 0=idle 1=scanning 2=results
  int _scanSel = -1;
  int _scanCount = 0;

  void drawMenu(lgfx::LGFX_Sprite& g);
  void drawDisplay(lgfx::LGFX_Sprite& g);
  void drawWifi(lgfx::LGFX_Sprite& g);
  void drawBluetooth(lgfx::LGFX_Sprite& g);
  void drawAudio(lgfx::LGFX_Sprite& g);
  void drawAbout(lgfx::LGFX_Sprite& g);
  void touchMenu(int x, int y, bool tap);
  void touchDisplay(int x, int y, bool tap, bool pressed);
  void touchWifi(int x, int y, bool tap);
  void touchBluetooth(int x, int y, bool tap);
  void touchAudio(int x, int y, bool tap, bool pressed);
};

extern SettingsApp SettingsUI;

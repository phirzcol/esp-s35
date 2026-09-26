#pragma once
#include "AppBase.h"

// RPG port of H:\game\game (PanelLan wizard RPG) onto the framework.
// Playfield 320x240 on top, party HUD + menu panel below. Drag on the map to
// move (original axis-sweeping collision); SD save files replace the RAM buffer.
class RpgApp : public AppBase {
public:
  const char* name() const override { return "RPG"; }
  bool wantsFullscreen() const override { return true; }
  bool inhibitScreensaver() const override { return _st == EXPLORE; }
  void onEnter() override;
  void onExit() override;
  bool onBack() override;
  void update(uint32_t dtMs) override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;

private:
  enum St { EXPLORE, PAUSE, STATUS };
  St _st = EXPLORE;

  // camera/player (world = 30x24 tiles of 32px)
  int _camX = 224, _camY = 144;
  int _lastCamX = 224, _lastCamY = 144;
  bool _walking = false;
  int _animFrame = 0;
  int _spriteId = 2;              // facing: level_sprites index 1..4

  // drag state
  bool _fingerDown = false;
  int _anchorX = 0, _anchorY = 0;
  int _startCamX = 0, _startCamY = 0;
  bool _wasPressed = false;

  // joystick + buttons (multi-touch, read in update)
  float _joyDX = 0, _joyDY = 0;   // -1..1
  bool _joyActive = false;
  bool _btnA = false, _btnB = false;

  void newGame();
  bool saveGame();
  bool loadGame();
  void moveCamera(int targetX, int targetY);
  void readPad();
  void drawWorld(lgfx::LGFX_Sprite& g);
  void drawPanel(lgfx::LGFX_Sprite& g);
  void drawPause(lgfx::LGFX_Sprite& g);
  void drawStatus(lgfx::LGFX_Sprite& g);
};

extern RpgApp Rpg;

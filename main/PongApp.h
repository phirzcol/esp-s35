#pragma once
#include "AppBase.h"

// Pong vs AI. Ported from magic8ball/esp32-pong-game (encoder -> touch drag).
class PongApp : public AppBase {
public:
  const char* name() const override { return "Pong"; }
  bool wantsFullscreen() const override { return true; }
  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
private:
  enum St { READY, PLAY, OVER };
  St _st = READY;
  float _playerY = 0, _aiY = 0;
  float _bx = 0, _by = 0, _vx = 0, _vy = 0;
  int _pScore = 0, _aScore = 0;
  int _touchTargetY = -1;
  bool _wasPressed = false;
  void resetBall();
  void newGame();
};

extern PongApp Pong;

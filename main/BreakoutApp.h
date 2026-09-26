#pragma once
#include "AppBase.h"

// Breakout with levels/lives. Ported from magic8ball breakout (encoder -> touch drag).
class BreakoutApp : public AppBase {
public:
  const char* name() const override { return "Breakout"; }
  bool wantsFullscreen() const override { return true; }
  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
private:
  enum St { READY, PLAY, OVER };
  static const int ROWS = 6, COLS = 8;
  St _st = READY;
  bool _bricks[ROWS][COLS];
  float _padX = 0;
  float _bx = 0, _by = 0, _vx = 0, _vy = 0;
  bool _launched = false;
  int _score = 0, _lives = 3, _level = 1;
  uint32_t _hiScore = 0;
  char _hiName[8] = "----";
  bool _newHigh = false;
  bool _nameEntry = false;
  int _touchX = -1;
  bool _wasPressed = false;
  void fillBricks();
  void resetBall();
  void launch();
  void newGame();
  void saveHigh();
};

extern BreakoutApp Breakout;

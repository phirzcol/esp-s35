#include "BreakoutApp.h"
#include "DisplayHAL.h"
#include <Preferences.h>
#include <math.h>

BreakoutApp Breakout;

static const int PAD_W = 70, PAD_H = 12, BALL = 9;
static const int BRICK_GAP = 3, BRICK_H = 16, BRICK_TOP = 40;
static const float START_SPEED = 280.0f;
static const float LEVEL_SPEEDUP = 40.0f;
static const float MAX_VY_RATIO = 0.9f;

static uint16_t rowColor(int r) {
  static const uint16_t cols[6] = {0xF986, 0xFD20, 0xFFE0, 0x2FA4, 0x05DF, 0xA95F};
  return cols[r % 6];
}

void BreakoutApp::fillBricks() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      _bricks[r][c] = true;
}

void BreakoutApp::resetBall() {
  _bx = _padX + PAD_W / 2.0f - BALL / 2.0f;
  _by = Gfx.height() - 60 - PAD_H - BALL - 2;
  _vx = _vy = 0;
  _launched = false;
}

void BreakoutApp::launch() {
  float speed = START_SPEED + (_level - 1) * LEVEL_SPEEDUP;
  float ang = random(50, 131) * 3.14159f / 180.0f;   // upward
  _vx = speed * cosf(ang);
  _vy = -speed * sinf(ang);
  _launched = true;
}

void BreakoutApp::newGame() {
  _score = 0;
  _lives = 3;
  _level = 1;
  _padX = (Gfx.width() - PAD_W) / 2.0f;
  fillBricks();
  resetBall();
  _st = PLAY;
}

void BreakoutApp::onEnter() {
  _st = READY;
  _touchX = -1;
  _nameEntry = false;
  _newHigh = false;
  Preferences p;
  p.begin("breakout", true);
  _hiScore = p.getUInt("hi", 0);
  String n = p.getString("hiName", "----");
  strlcpy(_hiName, n.c_str(), sizeof(_hiName));
  p.end();
}

void BreakoutApp::saveHigh() {
  Preferences p;
  p.begin("breakout", false);
  p.putUInt("hi", _hiScore);
  p.putString("hiName", _hiName);
  p.end();
}

void BreakoutApp::update(uint32_t dtMs) {
  if (_st != PLAY) return;
  float dt = dtMs / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;
  int W = Gfx.width(), H = Gfx.height();
  int padTop = H - 60;
  int brickW = (W - (COLS + 1) * BRICK_GAP) / COLS;

  if (_touchX >= 0) {
    float target = _touchX - PAD_W / 2.0f;
    _padX += (target - _padX) * 0.6f;
    _padX = constrain(_padX, 0.0f, (float)(W - PAD_W));
    if (!_launched) _bx = _padX + PAD_W / 2.0f - BALL / 2.0f;
  }
  if (!_launched) return;

  _bx += _vx * dt;
  _by += _vy * dt;

  if (_bx <= 0) { _bx = 0; _vx = -_vx; }
  if (_bx >= W - BALL) { _bx = W - BALL; _vx = -_vx; }
  if (_by <= 0) { _by = 0; _vy = -_vy; }

  // Paddle
  if (_vy > 0 && _by + BALL >= padTop && _by < padTop + PAD_H &&
      _bx + BALL >= _padX && _bx <= _padX + PAD_W) {
    _by = padTop - BALL;
    _vy = -_vy;
    float off = (_bx + BALL / 2.0f - _padX - PAD_W / 2.0f) / (PAD_W / 2.0f);
    float speed = sqrtf(_vx * _vx + _vy * _vy);
    _vx += off * speed * 0.6f;
    // keep the ball from going too flat or too vertical
    float maxVx = speed * MAX_VY_RATIO;
    _vx = constrain(_vx, -maxVx, maxVx);
    _vy = -sqrtf(speed * speed - _vx * _vx);
  }

  // Bricks (one hit per frame, matching original)
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (!_bricks[r][c]) continue;
      int bxp = BRICK_GAP + c * (brickW + BRICK_GAP);
      int byp = BRICK_TOP + r * (BRICK_H + BRICK_GAP);
      if (_bx + BALL > bxp && _bx < bxp + brickW &&
          _by + BALL > byp && _by < byp + BRICK_H) {
        _bricks[r][c] = false;
        _score += 10 * (ROWS - r) * _level;   // higher rows worth more (original scoring)
        float oL = (_bx + BALL) - bxp, oR = (bxp + brickW) - _bx;
        float oT = (_by + BALL) - byp, oB = (byp + BRICK_H) - _by;
        if (min(oL, oR) < min(oT, oB)) _vx = -_vx; else _vy = -_vy;
        r = ROWS; break;
      }
    }
  }

  // Bottom: lose a life
  if (_by >= H) {
    _lives--;
    if (_lives <= 0) {
      _st = OVER;
      if ((uint32_t)_score > _hiScore) {
        _hiScore = _score;
        _newHigh = true;
        _nameEntry = true;
        Kbd.open("New high score! Your name:", "");
      }
      return;
    }
    resetBall();
  }

  // Level clear
  bool any = false;
  for (int r = 0; r < ROWS && !any; r++)
    for (int c = 0; c < COLS; c++)
      if (_bricks[r][c]) { any = true; break; }
  if (!any) {
    _level++;
    fillBricks();
    resetBall();
  }
}

void BreakoutApp::draw(lgfx::LGFX_Sprite& g) {
  int W = g.width(), H = g.height();
  int padTop = H - 60;
  int brickW = (W - (COLS + 1) * BRICK_GAP) / COLS;

  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (_bricks[r][c])
        g.fillRoundRect(BRICK_GAP + c * (brickW + BRICK_GAP),
                        BRICK_TOP + r * (BRICK_H + BRICK_GAP), brickW, BRICK_H, 2, rowColor(r));

  g.fillRoundRect((int)_padX, padTop, PAD_W, PAD_H, 4, Theme::ACCENT);
  g.fillCircle((int)_bx + BALL / 2, (int)_by + BALL / 2, BALL / 2, Theme::TEXT);

  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::TEXT_DIM);
  char s[40];
  snprintf(s, sizeof(s), "Score %d   Lv %d", _score, _level);
  g.drawString(s, 6, 6);
  g.setTextDatum(lgfx::top_right);
  snprintf(s, sizeof(s), "Lives %d", _lives);
  g.drawString(s, W - 34, 6);   // clear of the home dot

  if (_st == PLAY && !_launched) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Tap to launch", W / 2, padTop - 40);
  }

  if (_st == READY || _st == OVER) {
    // Name-entry result for a new high score
    if (_nameEntry && Kbd.done()) {
      if (!Kbd.cancelled() && strlen(Kbd.text()) > 0)
        strlcpy(_hiName, Kbd.text(), sizeof(_hiName));
      saveHigh();
      Kbd.close();
      _nameEntry = false;
    }
    if (_nameEntry) return;   // keyboard overlay is showing

    g.fillRoundRect(W / 2 - 110, H / 2 - 60, 220, 120, 8, Theme::PANEL);
    g.drawRoundRect(W / 2 - 110, H / 2 - 60, 220, 120, 8, Theme::ACCENT);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT);
    if (_st == OVER) {
      snprintf(s, sizeof(s), "GAME OVER - Score %d", _score);
      g.drawString(s, W / 2, H / 2 - 30);
      if (_newHigh) {
        g.setTextColor(Theme::WARN);
        g.drawString("NEW HIGH SCORE!", W / 2, H / 2 - 10);
      }
    } else {
      g.drawString("BREAKOUT", W / 2, H / 2 - 30);
    }
    snprintf(s, sizeof(s), "Hi: %lu - %s", (unsigned long)_hiScore, _hiName);
    g.setTextColor(Theme::WARN);
    g.drawString(s, W / 2, H / 2 + 8);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Drag paddle - tap to start", W / 2, H / 2 + 34);
  }
}

void BreakoutApp::handleTouch(int x, int y, bool pressed) {
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;
  if (_st != PLAY) {
    if (tap && !_nameEntry) { _newHigh = false; newGame(); }
    return;
  }
  _touchX = pressed ? x : -1;
  if (tap && !_launched) launch();
}

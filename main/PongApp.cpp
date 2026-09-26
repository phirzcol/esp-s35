#include "PongApp.h"
#include "DisplayHAL.h"
#include <math.h>

PongApp Pong;

static const int PAD_W = 10, PAD_H = 70, PAD_MARGIN = 12, BALL = 10;
static const int WIN_SCORE = 7;
static const float START_SPEED = 300.0f;   // px/s
static const float SPEEDUP = 1.07f;
static const float MAX_SPEED = 900.0f;
static const float AI_SPEED = 260.0f;      // px/s

void PongApp::resetBall() {
  int W = Gfx.width(), H = Gfx.height();
  _bx = W / 2.0f - BALL / 2.0f;
  _by = H / 2.0f - BALL / 2.0f;
  float ang = random(-45, 46) * 3.14159f / 180.0f;
  float dir = random(2) ? 1.0f : -1.0f;
  _vx = START_SPEED * cosf(ang) * dir;
  _vy = START_SPEED * sinf(ang);
  if (fabsf(_vx) < 120.0f) _vx = _vx < 0 ? -120.0f : 120.0f;
}

void PongApp::newGame() {
  int H = Gfx.height();
  _playerY = _aiY = (H - PAD_H) / 2.0f;
  _pScore = _aScore = 0;
  resetBall();
  _st = PLAY;
}

void PongApp::onEnter() {
  _st = READY;
  _touchTargetY = -1;
}

void PongApp::update(uint32_t dtMs) {
  if (_st != PLAY) return;
  float dt = dtMs / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  int W = Gfx.width(), H = Gfx.height();

  // Player follows finger
  if (_touchTargetY >= 0) {
    float target = _touchTargetY - PAD_H / 2.0f;
    _playerY += (target - _playerY) * 0.5f;
    _playerY = constrain(_playerY, 0.0f, (float)(H - PAD_H));
  }

  _bx += _vx * dt;
  _by += _vy * dt;

  if (_by <= 0) { _by = 0; _vy = -_vy; }
  if (_by >= H - BALL) { _by = H - BALL; _vy = -_vy; }

  // Player paddle (left)
  if (_vx < 0 && _bx <= PAD_MARGIN + PAD_W && _bx + BALL >= PAD_MARGIN &&
      _by + BALL >= _playerY && _by <= _playerY + PAD_H) {
    _bx = PAD_MARGIN + PAD_W;
    _vx = -_vx * SPEEDUP;
    // english: hit offset bends the return
    float off = (_by + BALL / 2.0f - _playerY - PAD_H / 2.0f) / (PAD_H / 2.0f);
    _vy += off * 180.0f;
  }
  // AI paddle (right)
  int aiX = W - PAD_MARGIN - PAD_W;
  if (_vx > 0 && _bx + BALL >= aiX && _bx <= aiX + PAD_W &&
      _by + BALL >= _aiY && _by <= _aiY + PAD_H) {
    _bx = aiX - BALL;
    _vx = -_vx * SPEEDUP;
    float off = (_by + BALL / 2.0f - _aiY - PAD_H / 2.0f) / (PAD_H / 2.0f);
    _vy += off * 180.0f;
  }
  float sp = sqrtf(_vx * _vx + _vy * _vy);
  if (sp > MAX_SPEED) { _vx *= MAX_SPEED / sp; _vy *= MAX_SPEED / sp; }

  // Scoring
  if (_bx + BALL < 0) {
    _aScore++;
    if (_aScore >= WIN_SCORE) _st = OVER; else resetBall();
  } else if (_bx > W) {
    _pScore++;
    if (_pScore >= WIN_SCORE) _st = OVER; else resetBall();
  }

  // AI tracking, speed-limited
  float aiCenter = _aiY + PAD_H / 2.0f;
  float delta = (_by + BALL / 2.0f) - aiCenter;
  float maxStep = AI_SPEED * dt;
  if (delta > maxStep) delta = maxStep;
  if (delta < -maxStep) delta = -maxStep;
  _aiY = constrain(_aiY + delta, 0.0f, (float)(H - PAD_H));
}

void PongApp::draw(lgfx::LGFX_Sprite& g) {
  int W = g.width(), H = g.height();
  for (int y = 0; y < H; y += 12) g.fillRect(W / 2 - 1, y, 2, 6, Theme::PANEL_HI);

  g.fillRoundRect(PAD_MARGIN, (int)_playerY, PAD_W, PAD_H, 3, Theme::ACCENT);
  g.fillRoundRect(W - PAD_MARGIN - PAD_W, (int)_aiY, PAD_W, PAD_H, 3, Theme::BAD);
  g.fillRect((int)_bx, (int)_by, BALL, BALL, Theme::TEXT);

  g.setTextSize(2);
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::TEXT_DIM);
  char s[8];
  snprintf(s, sizeof(s), "%d", _pScore);
  g.drawString(s, W / 4, 8);
  snprintf(s, sizeof(s), "%d", _aScore);
  g.drawString(s, 3 * W / 4, 8);
  g.setTextSize(1);

  if (_st == READY || _st == OVER) {
    g.fillRoundRect(W / 2 - 110, H / 2 - 50, 220, 100, 8, Theme::PANEL);
    g.drawRoundRect(W / 2 - 110, H / 2 - 50, 220, 100, 8, Theme::ACCENT);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT);
    if (_st == OVER)
      g.drawString(_pScore >= WIN_SCORE ? "YOU WIN!" : "AI WINS!", W / 2, H / 2 - 20);
    else
      g.drawString("PONG", W / 2, H / 2 - 20);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Drag to move - tap to start", W / 2, H / 2 + 16);
  }
}

void PongApp::handleTouch(int x, int y, bool pressed) {
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;
  if (_st != PLAY) {
    if (tap) newGame();
    return;
  }
  _touchTargetY = pressed ? y : -1;
}

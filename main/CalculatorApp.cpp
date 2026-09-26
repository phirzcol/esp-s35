#include "CalculatorApp.h"
#include "DisplayHAL.h"
#include <math.h>

CalculatorApp Calculator;

static const char KEYS[5][4] = {
  {'C', '/', '*', '<'},
  {'7', '8', '9', '-'},
  {'4', '5', '6', '+'},
  {'1', '2', '3', '='},
  {'0', '0', '.', '='},
};

static UIRect keyRect(int r, int c) {
  UIRect area = AppBase::contentArea();
  int dispH = 56;
  int gy = area.y + dispH;
  int gh = area.h - dispH;
  int kw = area.w / 4;
  int kh = gh / 5;
  UIRect rc = { area.x + c * kw + 2, gy + r * kh + 2, kw - 4, kh - 4 };
  // '0' spans two columns; '=' spans two rows
  if (r == 4 && c == 0) rc.w = kw * 2 - 4;
  if (r == 3 && c == 3) rc.h = kh * 2 - 4;
  return rc;
}

void CalculatorApp::onEnter() {
  _acc = 0; _entry = 0; _op = 0; _entering = false; _fresh = true; _decimals = -1;
  fmt(0);
}

void CalculatorApp::fmt(double v) {
  if (fabs(v - (int64_t)v) < 1e-9 && fabs(v) < 1e15)
    snprintf(_shown, sizeof(_shown), "%lld", (long long)v);
  else
    snprintf(_shown, sizeof(_shown), "%.6g", v);
}

void CalculatorApp::pressKey(char k) {
  if (k >= '0' && k <= '9') {
    if (_fresh) { _entry = 0; _decimals = -1; _fresh = false; }
    if (_decimals < 0) _entry = _entry * 10 + (k - '0');
    else { _decimals++; _entry += (k - '0') / pow(10, _decimals); }
    _entering = true;
    fmt(_entry);
    return;
  }
  switch (k) {
    case '.':
      if (_fresh) { _entry = 0; _fresh = false; _entering = true; }
      if (_decimals < 0) _decimals = 0;
      return;
    case 'C': onEnter(); return;
    case '<':
      _entry = (_decimals > 0) ? _entry : floor(_entry / 10);
      if (_decimals >= 0) _decimals = -1;  // simple: backspace clears fraction
      fmt(_entry);
      return;
    case '+': case '-': case '*': case '/': case '=': {
      if (_op && _entering) {
        switch (_op) {
          case '+': _acc += _entry; break;
          case '-': _acc -= _entry; break;
          case '*': _acc *= _entry; break;
          case '/': _acc = (_entry != 0) ? _acc / _entry : 0; break;
        }
      } else if (_entering) {
        _acc = _entry;
      }
      fmt(_acc);
      _op = (k == '=') ? 0 : k;
      _entering = false;
      _fresh = true;
      _decimals = -1;
      return;
    }
  }
}

void CalculatorApp::draw(lgfx::LGFX_Sprite& g) {
  UIRect area = contentArea();
  // Display
  g.fillRoundRect(area.x + 4, area.y + 4, area.w - 8, 48, 6, Theme::PANEL);
  g.setTextDatum(lgfx::middle_right);
  g.setTextColor(Theme::TEXT);
  g.setTextSize(2);
  g.drawString(_shown, area.x + area.w - 14, area.y + 28);
  g.setTextSize(1);
  if (_op) {
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::ACCENT);
    char o[2] = {_op, 0};
    g.drawString(o, area.x + 12, area.y + 28);
  }
  // Keys
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 4; c++) {
      if (r == 4 && c == 1) continue;             // '0' span
      if (r == 4 && c == 3) continue;             // '=' span
      char k = KEYS[r][c];
      char lbl[2] = {k, 0};
      uint16_t bg = (k == '=') ? Theme::ACCENT
                  : (k == 'C') ? Theme::BAD
                  : (k == '+' || k == '-' || k == '*' || k == '/' || k == '<') ? Theme::PANEL_HI
                  : Theme::PANEL;
      UIDraw::button(g, keyRect(r, c), lbl, false, bg);
    }
  }
}

void CalculatorApp::handleTouch(int x, int y, bool pressed) {
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;
  if (!tap) return;
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 4; c++) {
      if (r == 4 && c == 1) continue;
      if (r == 4 && c == 3) continue;
      if (keyRect(r, c).contains(x, y)) { pressKey(KEYS[r][c]); return; }
    }
  }
}

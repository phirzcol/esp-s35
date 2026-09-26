#include "UIWidgets.h"
#include "DisplayHAL.h"
#include <string.h>

Keyboard Kbd;

namespace UIDraw {

void button(lgfx::LGFX_Sprite& g, const UIRect& r, const char* label, bool pressed, uint16_t bg) {
  g.fillRoundRect(r.x, r.y, r.w, r.h, 6, pressed ? Theme::ACCENT : bg);
  g.drawRoundRect(r.x, r.y, r.w, r.h, 6, Theme::PANEL_HI);
  g.setTextDatum(lgfx::middle_center);
  g.setTextColor(pressed ? TFT_BLACK : Theme::TEXT);
  g.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

void slider(lgfx::LGFX_Sprite& g, const UIRect& r, int value, int vmin, int vmax, const char* label) {
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString(label, r.x, r.y);
  int ty = r.y + 16;
  int th = r.h - 16;
  int cy = ty + th / 2;
  g.fillRoundRect(r.x, cy - 3, r.w, 6, 3, Theme::PANEL_HI);
  int range = (vmax > vmin) ? (vmax - vmin) : 1;
  int kx = r.x + (int)((int64_t)(value - vmin) * (r.w - 1) / range);
  g.fillRoundRect(r.x, cy - 3, kx - r.x, 6, 3, Theme::ACCENT);
  g.fillCircle(kx, cy, 9, Theme::ACCENT);
  g.setTextDatum(lgfx::top_right);
  g.setTextColor(Theme::TEXT);
  char v[12];
  snprintf(v, sizeof(v), "%d", value);
  g.drawString(v, r.x + r.w, r.y);
}

int sliderHit(const UIRect& r, int tx, int value, int vmin, int vmax) {
  int rel = tx - r.x;
  if (rel < 0) rel = 0;
  if (rel > r.w - 1) rel = r.w - 1;
  return vmin + (int)((int64_t)rel * (vmax - vmin) / (r.w - 1));
}

void toggle(lgfx::LGFX_Sprite& g, const UIRect& r, const char* label, bool on) {
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(Theme::TEXT);
  g.drawString(label, r.x, r.y + r.h / 2);
  int tw = 44, th = 22;
  int tx = r.x + r.w - tw, ty = r.y + (r.h - th) / 2;
  g.fillRoundRect(tx, ty, tw, th, th / 2, on ? Theme::GOOD : Theme::PANEL_HI);
  int kc = on ? (tx + tw - th / 2) : (tx + th / 2);
  g.fillCircle(kc, ty + th / 2, th / 2 - 3, Theme::TEXT);
}

} // namespace UIDraw

// ---------------- Keyboard ----------------

static const char* ROWS_L[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
static const char* ROWS_U[4] = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

void Keyboard::open(const char* title, const char* initial) {
  strlcpy(_title, title, sizeof(_title));
  strlcpy(_buf, initial, sizeof(_buf));
  _open = true; _done = false; _cancel = false; _shift = false; _wasPressed = false;
}

void Keyboard::draw(lgfx::LGFX_Sprite& g) {
  if (!_open) return;
  int W = g.width(), H = g.height();
  int kbH = H / 2;
  int top = H - kbH;

  g.fillRect(0, top, W, kbH, Theme::PANEL);
  g.drawFastHLine(0, top, W, Theme::ACCENT);

  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString(_title, 8, top + 4);
  g.fillRoundRect(8, top + 20, W - 16, 24, 4, Theme::BG);
  g.setTextColor(Theme::TEXT);
  g.drawString(_buf, 14, top + 25);
  int cx = 14 + g.textWidth(_buf);
  g.drawFastVLine(cx + 1, top + 24, 16, Theme::ACCENT);

  const char** rows = _shift ? ROWS_U : ROWS_L;
  int rowY = top + 50;
  int rowH = (kbH - 50 - 30) / 4;
  for (int r = 0; r < 4; r++) {
    int n = strlen(rows[r]);
    int kw = W / (n > 0 ? n : 1);
    int xoff = (W - kw * n) / 2;
    for (int i = 0; i < n; i++) {
      int bx = xoff + i * kw;
      g.drawRoundRect(bx + 1, rowY + 1, kw - 2, rowH - 2, 3, Theme::PANEL_HI);
      g.setTextDatum(lgfx::middle_center);
      char s[2] = {rows[r][i], 0};
      g.setTextColor(Theme::TEXT);
      g.drawString(s, bx + kw / 2, rowY + rowH / 2);
    }
    rowY += rowH;
  }
  // Bottom row: shift, space, backspace, cancel, OK
  int by = rowY, bh = 28;
  const char* labels[5] = {"^", "space", "<-", "X", "OK"};
  int widths[5] = {W / 8, W * 3 / 8, W / 8, W / 8, W / 4};
  int bx = 0;
  for (int i = 0; i < 5; i++) {
    uint16_t bg = (i == 4) ? Theme::GOOD : (i == 3) ? Theme::BAD : Theme::PANEL_HI;
    g.fillRoundRect(bx + 2, by + 2, widths[i] - 4, bh - 4, 4, bg);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(i >= 3 ? TFT_BLACK : Theme::TEXT);
    g.drawString(labels[i], bx + widths[i] / 2, by + bh / 2);
    bx += widths[i];
  }
}

void Keyboard::handleTouch(int x, int y, bool pressed) {
  if (!_open) return;
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;
  if (!tap) return;

  int W = Gfx.width(), H = Gfx.height();
  int kbH = H / 2;
  int top = H - kbH;
  if (y < top) return;

  const char** rows = _shift ? ROWS_U : ROWS_L;
  int rowY = top + 50;
  int rowH = (kbH - 50 - 30) / 4;
  size_t len = strlen(_buf);

  for (int r = 0; r < 4; r++) {
    if (y >= rowY && y < rowY + rowH) {
      int n = strlen(rows[r]);
      int kw = W / n;
      int xoff = (W - kw * n) / 2;
      int idx = (x - xoff) / kw;
      if (idx >= 0 && idx < n && len < sizeof(_buf) - 1) {
        _buf[len] = rows[r][idx];
        _buf[len + 1] = 0;
        if (_shift) _shift = false;
      }
      return;
    }
    rowY += rowH;
  }
  // Bottom row
  if (y >= rowY) {
    int widths[5] = {W / 8, W * 3 / 8, W / 8, W / 8, W / 4};
    int bx = 0;
    for (int i = 0; i < 5; i++) {
      if (x >= bx && x < bx + widths[i]) {
        switch (i) {
          case 0: _shift = !_shift; break;
          case 1: if (len < sizeof(_buf) - 1) { _buf[len] = ' '; _buf[len + 1] = 0; } break;
          case 2: if (len) _buf[len - 1] = 0; break;
          case 3: _cancel = true; _done = true; break;
          case 4: _done = true; break;
        }
        return;
      }
      bx += widths[i];
    }
  }
}

#pragma once
#include <LovyanGFX.hpp>

// Shared look & feel. All UI drawing targets the LovyanGFX canvas.
namespace Theme {
  constexpr uint16_t BG        = 0x10A2;  // near-black
  constexpr uint16_t PANEL     = 0x2124;
  constexpr uint16_t PANEL_HI  = 0x39C7;
  constexpr uint16_t ACCENT    = 0x05DF;  // cyan-blue
  constexpr uint16_t TEXT      = 0xEF7D;
  constexpr uint16_t TEXT_DIM  = 0x8C71;
  constexpr uint16_t GOOD      = 0x2FA4;
  constexpr uint16_t WARN      = 0xFD20;
  constexpr uint16_t BAD       = 0xF986;

  constexpr int STATUS_H = 24;   // top status bar
  constexpr int NAV_H    = 36;   // bottom nav bar
}

struct UIRect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// Immediate-mode helpers drawn onto the canvas.
namespace UIDraw {
  void button(lgfx::LGFX_Sprite& g, const UIRect& r, const char* label,
              bool pressed = false, uint16_t bg = Theme::PANEL_HI);
  void slider(lgfx::LGFX_Sprite& g, const UIRect& r, int value, int vmin, int vmax,
              const char* label);
  // Returns new value if the touch point is inside the slider track, else `value`.
  int  sliderHit(const UIRect& r, int tx, int value, int vmin, int vmax);
  void toggle(lgfx::LGFX_Sprite& g, const UIRect& r, const char* label, bool on);
}

// Simple modal QWERTY keyboard. Occupies the lower part of the screen.
// Usage: open(), then feed touch via handleTouch(); when done() returns true,
// read text(). Reusable by any app needing text input.
class Keyboard {
public:
  void open(const char* title, const char* initial = "");
  bool isOpen() const { return _open; }
  bool done() const { return _done; }
  bool cancelled() const { return _cancel; }
  const char* text() const { return _buf; }
  void close() { _open = false; _done = false; _cancel = false; }
  void draw(lgfx::LGFX_Sprite& g);
  void handleTouch(int x, int y, bool pressed);
private:
  char _buf[64] = {0};
  char _title[32] = {0};
  bool _open = false, _done = false, _cancel = false, _shift = false;
  bool _wasPressed = false;
};

extern Keyboard Kbd;

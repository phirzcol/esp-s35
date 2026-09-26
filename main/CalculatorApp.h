#pragma once
#include "AppBase.h"

// Simple 4-function calculator; framework verification app.
class CalculatorApp : public AppBase {
public:
  const char* name() const override { return "Calculator"; }
  void onEnter() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
private:
  void pressKey(char k);
  double _acc = 0;
  double _entry = 0;
  char _op = 0;
  bool _entering = false;
  bool _fresh = true;
  int _decimals = -1;   // -1 = integer part, else count of decimal digits
  bool _wasPressed = false;
  char _shown[24] = "0";
  void fmt(double v);
};

extern CalculatorApp Calculator;

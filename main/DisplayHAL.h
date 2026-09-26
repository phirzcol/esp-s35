#pragma once
#include <LovyanGFX.hpp>

// ST77922 QSPI display driver + LovyanGFX offscreen canvas (back buffer in PSRAM).
// All drawing goes through canvas() using the LovyanGFX API; present() pushes the
// finished frame to the panel over QSPI. This gives flicker-free double buffering:
// canvas = back buffer, panel GRAM = front buffer.
class DisplayHAL {
public:
  bool begin(uint8_t rotation, uint8_t brightness);
  lgfx::LGFX_Sprite& canvas() { return _canvas; }
  void present();                              // push full frame
  void presentRect(int x, int y, int w, int h); // push partial region (x,w aligned to 4)
  void setRotation(uint8_t r);                 // 0..3, recreates canvas
  uint8_t rotation() const { return _rot; }
  void setBrightness(uint8_t b);               // 0..255
  uint8_t brightness() const { return _bright; }
  int width()  const { return _w; }
  int height() const { return _h; }

private:
  void qspiInit();
  void lcdWriteReg(uint32_t cmd, const void* data, uint8_t len);
  void lcdSetWindow(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey);
  void beginPixels();
  void pushChunk(const uint16_t* px, size_t count);
  void endPixels();
  void applyRotation();

  lgfx::LGFX_Sprite _canvas;
  uint16_t* _bounce = nullptr;   // internal-RAM staging for partial pushes
  uint16_t* _xpose = nullptr;    // PSRAM staging for landscape transpose
  int _w = 320, _h = 480;
  uint8_t _rot = 0;
  uint8_t _bright = 200;
  bool _firstChunk = true;
};

extern DisplayHAL Gfx;

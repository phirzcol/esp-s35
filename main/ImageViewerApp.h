#pragma once
#include "AppBase.h"

// JPEG/BMP viewer: browses images on SD, fullscreen display, swipe prev/next.
class ImageViewerApp : public AppBase {
public:
  const char* name() const override { return "Images"; }
  bool wantsFullscreen() const override { return _mode == VIEW; }
  bool wantsCanvasClear() const override { return false; }   // decode once, keep frame
  void onEnter() override;
  void onExit() override;
  bool onBack() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;

  // FileManager handoff
  void playPath(const char* fullPath);

private:
  enum Mode { LIST, VIEW };
  struct Img { char name[64]; };
  static const int MAX_IMGS = 128;
  static const int ROW_H = 34;

  Mode _mode = LIST;
  char _dir[128] = "/";
  Img* _imgs = nullptr;
  int _count = 0;
  int _current = -1;
  int _scroll = 0;
  bool _needRender = false;
  bool _renderOk = false;
  char _err[32] = "";

  bool _pendingPlay = false;
  char _pendingPath[224] = "";
  bool _wasPressed = false;
  bool _dragging = false;
  int _pressX = 0, _pressY = 0, _scrollStart = 0;

  void scanDir();
  void show(int idx);
  bool renderImage(lgfx::LGFX_Sprite& g, const char* path);
  bool renderBmp(lgfx::LGFX_Sprite& g, const uint8_t* buf, size_t len);
  void drawList(lgfx::LGFX_Sprite& g);
};

extern ImageViewerApp ImageViewer;

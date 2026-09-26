#pragma once
#include "AppBase.h"

// NES emulator app: retro-go nofrendo core, 60fps emu task on core 0,
// palette blit + touch controller overlay on the UI core.
class NesApp : public AppBase {
public:
  const char* name() const override { return "NES"; }
  bool wantsFullscreen() const override { return _mode == PLAY; }
  bool inhibitScreensaver() const override { return _mode == PLAY; }
  void onEnter() override;
  void onExit() override;
  bool onBack() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;

  // FileManager handoff
  void playPath(const char* fullPath);

private:
  enum Mode { LIST, PLAY };
  struct Rom { char name[64]; };
  static const int MAX_ROMS = 64;
  static const int ROW_H = 34;

  Mode _mode = LIST;
  Rom* _roms = nullptr;
  int _count = 0;
  int _scroll = 0;
  char _dir[96] = "/roms";
  bool _pendingPlay = false;
  char _pendingPath[224] = "";

  bool _wasPressed = false;
  bool _dragging = false;
  int _pressX = 0, _pressY = 0, _scrollStart = 0;

  void scanDir();
  bool startRom(const char* path);
  void stopEmu();
  void drawList(lgfx::LGFX_Sprite& g);
  void drawPlay(lgfx::LGFX_Sprite& g);
  void updateButtons();
};

extern NesApp Nes;

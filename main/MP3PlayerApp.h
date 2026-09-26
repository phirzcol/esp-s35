#pragma once
#include "AppBase.h"

// MP3/audio player: playlist from a directory, transport controls, spectrum visualizer.
class MP3PlayerApp : public AppBase {
public:
  const char* name() const override { return "MP3 Player"; }
  void onEnter() override;
  void onExit() override;
  bool onBack() override;
  void update(uint32_t dtMs) override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;
  bool hasSettings() const override { return true; }
  void onSettings() override;   // toggles visualizer
  bool wantsFullscreen() const override { return _mode == VIS; }
  bool inhibitScreensaver() const override;

  // Called by FileManager before pushing this app
  void playPath(const char* fullPath);

private:
  enum Mode { LIST, PLAYER, VIS };
  struct Track { char name[64]; bool isDir; };
  static const int MAX_TRACKS = 128;
  static const int ROW_H = 34;

  Mode _mode = LIST;
  char _dir[160] = "/";
  Track* _tracks = nullptr;    // PSRAM
  int _count = 0;
  int _current = -1;
  int _scroll = 0;
  char _curPath[256] = "";     // currently playing full path (for resume)

  // resume state (Preferences "mp3")
  char _resumePath[256] = "";
  uint32_t _resumePos = 0;
  uint32_t _lastResumeSave = 0;

  bool _wasPressed = false;
  bool _dragging = false;
  int _pressX = 0, _pressY = 0, _scrollStart = 0;
  bool _pendingPlay = false;
  char _pendingPath[224] = "";

  // seek
  bool _seekDrag = false;
  uint32_t _seekTarget = 0;
  uint32_t _lastTouchMs = 0;

  // radial visualizer state (buffers in PSRAM, allocated on first VIS entry)
  float* _ringHist = nullptr;    // RINGS x POINTS
  float* _smoothBins = nullptr;
  float* _cosT = nullptr;
  float* _sinT = nullptr;
  float _hueOff = 0;
  uint32_t _lastPropagate = 0;

  void scanDir();
  void enterDir(const char* nm);
  void upDir();
  void playIndex(int idx);
  void nextTrack(int dir);
  void saveResume();
  void loadResume();
  void enterVis();
  void exitVis();
  void drawList(lgfx::LGFX_Sprite& g);
  void drawPlayer(lgfx::LGFX_Sprite& g);
  void drawVis(lgfx::LGFX_Sprite& g);
};

extern MP3PlayerApp MP3Player;

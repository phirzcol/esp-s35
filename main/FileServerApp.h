#pragma once
#include "AppBase.h"

// Web file server for the SD card. The HTTP server only runs while this app
// is in the foreground — closing the app closes the server (transfer safety).
class FileServerApp : public AppBase {
public:
  const char* name() const override { return "File Server"; }
  void onEnter() override;
  void onExit() override;
  void update(uint32_t dtMs) override;
  void draw(lgfx::LGFX_Sprite& g) override;
  bool inhibitScreensaver() const override { return _running; }

private:
  bool _running = false;
  char _lastAction[64] = "";
  uint32_t _hits = 0;
};

extern FileServerApp FileServer;

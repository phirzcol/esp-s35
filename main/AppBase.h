#pragma once
#include "UIWidgets.h"

// Base class every app implements. Apps draw into Gfx.canvas() within the content
// area (below status bar, above nav bar) unless wantsFullscreen() is true.
class AppBase {
public:
  virtual ~AppBase() {}
  virtual const char* name() const = 0;
  virtual void onEnter() {}                 // became foreground
  virtual void onExit() {}                  // leaving foreground; release resources here
  virtual void update(uint32_t dtMs) {}     // logic tick
  virtual void draw(lgfx::LGFX_Sprite& g) = 0;
  virtual void handleTouch(int x, int y, bool pressed) {}
  // Return true to consume the Back press (e.g. sub-page navigation).
  virtual bool onBack() { return false; }
  // True prevents the system screensaver from dimming (e.g. visualizer running).
  virtual bool inhibitScreensaver() const { return false; }
  // Games can hide the chrome; a small floating home dot is drawn instead.
  virtual bool wantsFullscreen() const { return false; }
  // Return false to keep the previous canvas contents (e.g. decoded image).
  virtual bool wantsCanvasClear() const { return true; }
  // Apps with in-app settings return true and react to onSettings().
  virtual bool hasSettings() const { return false; }
  virtual void onSettings() {}
  // Content area helper
  static UIRect contentArea();
};

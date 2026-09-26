#pragma once
#include "AppBase.h"

// SD card browser: scrollable listing, text viewer, rename/delete.
class FileManagerApp : public AppBase {
public:
  const char* name() const override { return "Files"; }
  void onEnter() override;
  void onExit() override;
  bool onBack() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;

private:
  enum Mode { BROWSE, FILEMENU, CONFIRM_DELETE, RENAME, VIEW_TEXT, NO_CARD };
  struct Entry { char name[48]; uint32_t size; bool isDir; };

  static const int MAX_ENTRIES = 200;
  static const int ROW_H = 36;

  Mode _mode = NO_CARD;
  char _path[192] = "/";
  char _diag[64] = "";           // last loadDir result, shown in header
  Entry* _entries = nullptr;      // PSRAM
  int _count = 0;
  int _scroll = 0;                // px
  int _sel = -1;

  // touch/drag state
  bool _wasPressed = false;
  bool _dragging = false;
  int _pressY = 0, _pressX = 0, _scrollStart = 0;

  // text viewer
  char* _text = nullptr;          // PSRAM
  int32_t* _lineOff = nullptr;    // PSRAM line start offsets
  int _lineCount = 0;
  int _viewScroll = 0;

  void loadDir();
  void enterDir(const char* nm);
  void upDir();
  void openFile(int idx);
  void openTextViewer(const char* fullPath);
  void closeTextViewer();
  void fullPathOf(int idx, char* out, size_t n) const;
  int  listTop() const;
  int  visibleRows() const;
  void drawBrowse(lgfx::LGFX_Sprite& g);
  void drawOverlayMenu(lgfx::LGFX_Sprite& g);
  void drawConfirm(lgfx::LGFX_Sprite& g);
  void drawViewer(lgfx::LGFX_Sprite& g);
};

extern FileManagerApp FileMan;

#pragma once
#include "AppBase.h"

// Notes/Wordpad: .txt files in /notes on SD. List -> editor with inline keyboard.
class NotesApp : public AppBase {
public:
  const char* name() const override { return "Notes"; }
  void onEnter() override;
  void onExit() override;
  bool onBack() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;

private:
  enum Mode { LIST, EDIT, NAMING };
  struct Note { char name[48]; };
  static const int MAX_NOTES = 64;
  static const int ROW_H = 36;
  static const size_t TEXT_MAX = 8192;

  Mode _mode = LIST;
  Note* _notes = nullptr;
  int _count = 0;
  int _scroll = 0;
  char* _text = nullptr;        // PSRAM edit buffer
  char _curName[48] = "";
  bool _dirty = false;
  bool _shift = false;

  bool _wasPressed = false;
  bool _dragging = false;
  int _pressX = 0, _pressY = 0, _scrollStart = 0;

  void scanNotes();
  void openNote(const char* nm);
  void saveNote();
  void newNote();
  void editorKey(char k);
  void drawList(lgfx::LGFX_Sprite& g);
  void drawEditor(lgfx::LGFX_Sprite& g);
  bool editorTouch(int x, int y);
};

extern NotesApp Notes;

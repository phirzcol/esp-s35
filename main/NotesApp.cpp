#include "NotesApp.h"
#include "SDCardHAL.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include <SD_MMC.h>
#include <dirent.h>

NotesApp Notes;

static const char* NROWS_L[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
static const char* NROWS_U[4] = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

// Editor layout: text pane on top, keyboard pinned to the bottom half
static int kbTop() { return Gfx.height() - Theme::NAV_H - 210; }

void NotesApp::onEnter() {
  if (!_notes) _notes = (Note*)ps_malloc(sizeof(Note) * MAX_NOTES);
  if (!_text) _text = (char*)ps_malloc(TEXT_MAX);
  _mode = LIST;
  if (!SDCard.mount() || !_notes || !_text) {
    Notify.post("No SD card");
    _count = 0;
    return;
  }
  SD_MMC.mkdir("/notes");
  scanNotes();
}

void NotesApp::onExit() {
  if (_dirty) saveNote();
}

bool NotesApp::onBack() {
  if (_mode == EDIT) {
    if (_dirty) saveNote();
    _mode = LIST;
    scanNotes();
    return true;
  }
  return false;
}

void NotesApp::scanNotes() {
  _count = 0;
  _scroll = 0;
  DIR* d = opendir("/sdcard/notes");
  if (!d) return;
  struct dirent* de;
  while ((de = readdir(d)) && _count < MAX_NOTES) {
    if (de->d_type == DT_DIR || de->d_name[0] == '.') continue;
    strlcpy(_notes[_count].name, de->d_name, sizeof(_notes[_count].name));
    _count++;
  }
  closedir(d);
}

void NotesApp::openNote(const char* nm) {
  strlcpy(_curName, nm, sizeof(_curName));
  _text[0] = 0;
  char p[96];
  snprintf(p, sizeof(p), "/notes/%s", nm);
  File f = SD_MMC.open(p);
  if (f) {
    size_t got = f.read((uint8_t*)_text, TEXT_MAX - 1);
    _text[got] = 0;
    f.close();
  }
  _dirty = false;
  _mode = EDIT;
}

void NotesApp::saveNote() {
  if (!_curName[0]) return;
  char p[96];
  snprintf(p, sizeof(p), "/notes/%s", _curName);
  File f = SD_MMC.open(p, FILE_WRITE);
  if (f) {
    f.write((const uint8_t*)_text, strlen(_text));
    f.close();
    Notify.post("Note saved");
    _dirty = false;
  } else {
    Notify.post("Save failed");
  }
}

void NotesApp::newNote() {
  Kbd.open("Note name", "");
  _mode = NAMING;
}

void NotesApp::editorKey(char k) {
  size_t len = strlen(_text);
  if (k == '\b') {
    if (len) { _text[len - 1] = 0; _dirty = true; }
    return;
  }
  if (len < TEXT_MAX - 1) {
    _text[len] = k;
    _text[len + 1] = 0;
    _dirty = true;
    if (_shift && k != ' ') _shift = false;
  }
}

void NotesApp::drawList(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  UIRect nb = {c.x + c.w - 110, c.y + 2, 104, 30};
  UIDraw::button(g, nb, "+ New note", false, Theme::ACCENT);
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(Theme::ACCENT);
  g.drawString("/notes", c.x + 8, c.y + 17);

  int top = c.y + 38;
  int listH = c.y + c.h - top;
  g.setClipRect(0, top, c.w, listH);
  for (int i = 0; i < _count; i++) {
    int y = top + i * ROW_H - _scroll;
    if (y + ROW_H < top || y > top + listH) continue;
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::TEXT);
    g.drawString(_notes[i].name, 14, y + ROW_H / 2);
    g.drawFastHLine(0, y + ROW_H - 1, c.w, Theme::PANEL);
  }
  if (_count == 0) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("No notes yet - tap New note", c.w / 2, top + listH / 2);
  }
  g.clearClipRect();
}

void NotesApp::drawEditor(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  int paneBottom = kbTop() - 4;

  // Header: name + save button
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(_dirty ? Theme::WARN : Theme::TEXT_DIM);
  char hdr[64];
  snprintf(hdr, sizeof(hdr), "%s%s", _curName, _dirty ? " *" : "");
  g.drawString(hdr, c.x + 6, c.y + 14);
  UIRect sb = {c.x + c.w - 70, c.y + 2, 64, 26};
  UIDraw::button(g, sb, "Save", false, _dirty ? Theme::GOOD : Theme::PANEL_HI);

  // Text pane: wrapped, tail-anchored so the cursor is always visible
  int paneTop = c.y + 32;
  int lineH = g.fontHeight() + 2;
  int maxLines = (paneBottom - paneTop) / lineH;
  int maxW = c.w - 16;

  // wrap from the start, keep only the last maxLines
  static const int MAXL = 64;
  int32_t starts[MAXL];
  int nLines = 0;
  int32_t pos = 0;
  while (_text[pos]) {
    if (nLines < MAXL) starts[nLines] = pos;
    else { memmove(starts, starts + 1, sizeof(int32_t) * (MAXL - 1)); starts[MAXL - 1] = pos; }
    if (nLines < MAXL) nLines++;
    int w = 0;
    int32_t ls = pos;
    while (_text[pos]) {
      char ch = _text[pos];
      if (ch == '\n') { pos++; break; }
      char s[2] = {ch, 0};
      w += g.textWidth(s);
      if (w > maxW && pos > ls) break;
      pos++;
    }
  }
  int first = nLines > maxLines ? nLines - maxLines : 0;
  g.setClipRect(0, paneTop, c.w, paneBottom - paneTop);
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::TEXT);
  int yy = paneTop;
  for (int i = first; i < nLines; i++) {
    int32_t s = starts[i];
    int32_t e = (i + 1 < nLines) ? starts[i + 1] : (int32_t)strlen(_text);
    char save = _text[e];
    _text[e] = 0;
    // hide trailing newline glyph
    int sl = strlen(_text + s);
    if (sl && _text[s + sl - 1] == '\n') _text[s + sl - 1] = 0;
    g.drawString(_text + s, c.x + 8, yy);
    if (sl && save != 0 && _text[s + sl - 1] == 0 && e > s) _text[s + sl - 1] = '\n';
    _text[e] = save;
    yy += lineH;
  }
  // cursor
  int cw = 0;
  if (nLines) {
    int32_t s = starts[nLines - 1];
    char save = _text[strlen(_text)];
    (void)save;
    cw = g.textWidth(_text + s);
  }
  if ((millis() / 400) & 1) g.fillRect(c.x + 8 + cw, yy - lineH, 2, lineH - 2, Theme::ACCENT);
  g.clearClipRect();

  // Inline keyboard
  int W = c.w;
  int top = kbTop();
  g.fillRect(0, top, W, Gfx.height() - Theme::NAV_H - top, Theme::PANEL);
  const char** rows = _shift ? NROWS_U : NROWS_L;
  int rowH = 38;
  for (int r = 0; r < 4; r++) {
    int n = strlen(rows[r]);
    int kw = W / n;
    int xoff = (W - kw * n) / 2;
    for (int i = 0; i < n; i++) {
      int bx = xoff + i * kw;
      g.drawRoundRect(bx + 1, top + r * rowH + 1, kw - 2, rowH - 2, 3, Theme::PANEL_HI);
      g.setTextDatum(lgfx::middle_center);
      char s[2] = {rows[r][i], 0};
      g.setTextColor(Theme::TEXT);
      g.drawString(s, bx + kw / 2, top + r * rowH + rowH / 2);
    }
  }
  // bottom row: shift, space, backspace, enter
  int by = top + 4 * rowH;
  const char* labels[4] = {"^", "space", "<-", "NL"};
  int widths[4] = {W / 6, W / 2, W / 6, W - W / 6 - W / 2 - W / 6};
  int bx = 0;
  for (int i = 0; i < 4; i++) {
    uint16_t bg = (i == 0 && _shift) ? Theme::ACCENT : Theme::PANEL_HI;
    g.fillRoundRect(bx + 2, by + 2, widths[i] - 4, 34, 4, bg);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT);
    g.drawString(labels[i], bx + widths[i] / 2, by + 19);
    bx += widths[i];
  }
}

bool NotesApp::editorTouch(int x, int y) {
  UIRect c = contentArea();
  UIRect sb = {c.x + c.w - 70, c.y + 2, 64, 26};
  if (sb.contains(x, y)) { saveNote(); return true; }

  int top = kbTop();
  if (y < top) return false;
  const char** rows = _shift ? NROWS_U : NROWS_L;
  int W = c.w;
  int rowH = 38;
  for (int r = 0; r < 4; r++) {
    if (y >= top + r * rowH && y < top + (r + 1) * rowH) {
      int n = strlen(rows[r]);
      int kw = W / n;
      int xoff = (W - kw * n) / 2;
      int idx = (x - xoff) / kw;
      if (idx >= 0 && idx < n) editorKey(rows[r][idx]);
      return true;
    }
  }
  int by = top + 4 * rowH;
  if (y >= by) {
    int widths[4] = {W / 6, W / 2, W / 6, W - W / 6 - W / 2 - W / 6};
    int bx = 0;
    for (int i = 0; i < 4; i++) {
      if (x >= bx && x < bx + widths[i]) {
        if (i == 0) _shift = !_shift;
        else if (i == 1) editorKey(' ');
        else if (i == 2) editorKey('\b');
        else editorKey('\n');
        return true;
      }
      bx += widths[i];
    }
  }
  return true;
}

void NotesApp::draw(lgfx::LGFX_Sprite& g) {
  if (_mode == NAMING && Kbd.done()) {
    if (!Kbd.cancelled() && strlen(Kbd.text()) > 0) {
      char nm[48];
      strlcpy(nm, Kbd.text(), sizeof(nm) - 4);
      if (!strstr(nm, ".txt")) strlcat(nm, ".txt", sizeof(nm));
      strlcpy(_curName, nm, sizeof(_curName));
      _text[0] = 0;
      _dirty = true;
      _mode = EDIT;
    } else {
      _mode = LIST;
    }
    Kbd.close();
  }
  if (_mode == EDIT) drawEditor(g);
  else drawList(g);
}

void NotesApp::handleTouch(int x, int y, bool pressed) {
  bool tap = false;
  if (pressed && !_wasPressed) {
    _pressX = x; _pressY = y;
    _scrollStart = _scroll;
    _dragging = false;
  } else if (pressed && _wasPressed) {
    if (_mode == LIST && abs(y - _pressY) > 8) _dragging = true;
    if (_dragging) {
      UIRect c = contentArea();
      int listH = c.h - 38;
      int totalH = _count * ROW_H;
      _scroll = _scrollStart + (_pressY - y);
      int maxS = totalH > listH ? totalH - listH : 0;
      if (_scroll < 0) _scroll = 0;
      if (_scroll > maxS) _scroll = maxS;
    }
  } else if (!pressed && _wasPressed) {
    if (!_dragging) tap = true;
  }
  _wasPressed = pressed;
  if (!tap) return;
  x = _pressX; y = _pressY;

  if (_mode == EDIT) { editorTouch(x, y); return; }
  if (_mode != LIST) return;

  UIRect c = contentArea();
  UIRect nb = {c.x + c.w - 110, c.y + 2, 104, 30};
  if (nb.contains(x, y)) { newNote(); return; }
  int top = c.y + 38;
  if (y < top) return;
  int idx = (y - top + _scroll) / ROW_H;
  if (idx >= 0 && idx < _count) openNote(_notes[idx].name);
}

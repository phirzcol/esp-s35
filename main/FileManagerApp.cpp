#include "FileManagerApp.h"
#include "SDCardHAL.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include "MP3PlayerApp.h"
#include "NesApp.h"
#include "ImageViewerApp.h"
#include "AppManager.h"
#include <SD_MMC.h>
#include <dirent.h>
#include <sys/stat.h>

FileManagerApp FileMan;

static const size_t TEXT_MAX = 64 * 1024;
static const int MAX_LINES = 4000;

static bool hasExt(const char* nm, const char* ext) {
  const char* dot = strrchr(nm, '.');
  return dot && strcasecmp(dot + 1, ext) == 0;
}
static bool isTextFile(const char* nm) {
  return hasExt(nm, "txt") || hasExt(nm, "log") || hasExt(nm, "csv") ||
         hasExt(nm, "json") || hasExt(nm, "md") || hasExt(nm, "ini") || hasExt(nm, "cfg");
}
static bool isAudioFile(const char* nm) {
  return hasExt(nm, "mp3") || hasExt(nm, "wav") || hasExt(nm, "flac") || hasExt(nm, "aac");
}
static bool isImageFile(const char* nm) {
  return hasExt(nm, "jpg") || hasExt(nm, "jpeg") || hasExt(nm, "bmp") || hasExt(nm, "png");
}

void FileManagerApp::onEnter() {
  if (!_entries) _entries = (Entry*)ps_malloc(sizeof(Entry) * MAX_ENTRIES);
  strcpy(_path, "/");
  _scroll = 0;
  _sel = -1;
  if (!SDCard.mount() || !_entries) {
    _mode = NO_CARD;
    return;
  }
  _mode = BROWSE;
  loadDir();
}

void FileManagerApp::onExit() {
  closeTextViewer();
  // Card stays mounted for other apps (MP3/images); memory for entries kept.
}

bool FileManagerApp::onBack() {
  if (_mode == VIEW_TEXT) { closeTextViewer(); _mode = BROWSE; return true; }
  if (_mode == FILEMENU || _mode == CONFIRM_DELETE) { _mode = BROWSE; return true; }
  if (_mode == BROWSE && strcmp(_path, "/") != 0) { upDir(); return true; }
  return false;
}

void FileManagerApp::loadDir() {
  _count = 0;
  _scroll = 0;
  // POSIX enumeration on the VFS mount — immune to Arduino FS iterator quirks
  char vfs[256];
  snprintf(vfs, sizeof(vfs), "/sdcard%s", strcmp(_path, "/") == 0 ? "" : _path);
  errno = 0;
  DIR* d = opendir(vfs);
  if (!d) {
    snprintf(_diag, sizeof(_diag), "opendir err %d", errno);
    Serial.printf("FM: opendir('%s') failed, errno=%d\n", vfs, errno);
    // reveal hidden bytes in the final path segment
    const char* seg = strrchr(_path, '/');
    if (seg) {
      Serial.print("FM: name bytes:");
      for (const char* p = seg + 1; *p; p++) Serial.printf(" %02X", (uint8_t)*p);
      Serial.println();
    }
    return;
  }
  struct dirent* de;
  int raw = 0;
  while ((de = readdir(d)) && _count < MAX_ENTRIES) {
    raw++;
    if (de->d_name[0] == '.') continue;
    Entry& e = _entries[_count];
    strlcpy(e.name, de->d_name, sizeof(e.name));
    e.isDir = (de->d_type == DT_DIR);
    e.size = 0;
    if (!e.isDir) {
      char fp[320];
      snprintf(fp, sizeof(fp), "%s/%s", vfs, de->d_name);
      struct stat st;
      if (stat(fp, &st) == 0) e.size = (uint32_t)st.st_size;
    }
    _count++;
  }
  closedir(d);
  snprintf(_diag, sizeof(_diag), "%d shown / %d raw", _count, raw);
  Serial.printf("FM: '%s' -> %d shown, %d raw\n", vfs, _count, raw);
  // dirs first, then alpha (insertion sort, small N)
  for (int i = 1; i < _count; i++) {
    Entry key = _entries[i];
    int j = i - 1;
    while (j >= 0) {
      bool after = (_entries[j].isDir == key.isDir)
                   ? (strcasecmp(_entries[j].name, key.name) > 0)
                   : (!_entries[j].isDir && key.isDir);
      if (!after) break;
      _entries[j + 1] = _entries[j];
      j--;
    }
    _entries[j + 1] = key;
  }
}

void FileManagerApp::fullPathOf(int idx, char* out, size_t n) const {
  if (strcmp(_path, "/") == 0) snprintf(out, n, "/%s", _entries[idx].name);
  else snprintf(out, n, "%s/%s", _path, _entries[idx].name);
}

void FileManagerApp::enterDir(const char* nm) {
  size_t len = strlen(_path);
  if (strcmp(_path, "/") == 0) snprintf(_path, sizeof(_path), "/%s", nm);
  else if (len + strlen(nm) + 2 < sizeof(_path)) {
    _path[len] = '/';
    strlcpy(_path + len + 1, nm, sizeof(_path) - len - 1);
  }
  loadDir();
}

void FileManagerApp::upDir() {
  char* slash = strrchr(_path, '/');
  if (slash == _path) _path[1] = 0;
  else *slash = 0;
  loadDir();
}

void FileManagerApp::openTextViewer(const char* fullPath) {
  closeTextViewer();
  File f = SD_MMC.open(fullPath);
  if (!f) return;
  size_t sz = f.size();
  if (sz > TEXT_MAX) sz = TEXT_MAX;
  _text = (char*)ps_malloc(sz + 1);
  _lineOff = (int32_t*)ps_malloc(sizeof(int32_t) * MAX_LINES);
  if (!_text || !_lineOff) { f.close(); closeTextViewer(); return; }
  size_t got = f.read((uint8_t*)_text, sz);
  f.close();
  _text[got] = 0;

  // Pre-wrap into display lines using canvas font metrics
  lgfx::LGFX_Sprite& g = Gfx.canvas();
  int maxW = Gfx.width() - 16;
  _lineCount = 0;
  int32_t pos = 0;
  while (_text[pos] && _lineCount < MAX_LINES) {
    _lineOff[_lineCount++] = pos;
    int w = 0;
    int32_t lineStart = pos;
    while (_text[pos]) {
      char c = _text[pos];
      if (c == '\n') { pos++; break; }
      if (c == '\r') { _text[pos] = ' '; }
      char s[2] = {c, 0};
      w += g.textWidth(s);
      if (w > maxW && pos > lineStart) break;
      pos++;
    }
  }
  _viewScroll = 0;
  _mode = VIEW_TEXT;
}

void FileManagerApp::closeTextViewer() {
  if (_text) { free(_text); _text = nullptr; }
  if (_lineOff) { free(_lineOff); _lineOff = nullptr; }
  _lineCount = 0;
}

void FileManagerApp::openFile(int idx) {
  char full[256];
  fullPathOf(idx, full, sizeof(full));
  const char* nm = _entries[idx].name;
  if (isTextFile(nm)) { openTextViewer(full); return; }
  if (isAudioFile(nm)) {
    MP3Player.playPath(full);
    Apps.push(&MP3Player);
    return;
  }
  if (hasExt(nm, "nes")) {
    Nes.playPath(full);
    Apps.push(&Nes);
    return;
  }
  if (isImageFile(nm)) {
    ImageViewer.playPath(full);
    Apps.push(&ImageViewer);
    return;
  }
  Notify.post("No viewer for this file type");
}

int FileManagerApp::listTop() const { return Theme::STATUS_H + 26; }
int FileManagerApp::visibleRows() const {
  return (Gfx.height() - Theme::NAV_H - listTop()) / ROW_H;
}

void FileManagerApp::drawBrowse(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  // Header: path + card space
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::ACCENT);
  g.drawString(_path, c.x + 6, c.y + 4);
  g.setTextDatum(lgfx::top_right);
  g.setTextColor(Theme::TEXT_DIM);
  char sp[32];
  snprintf(sp, sizeof(sp), "%llu/%llu MB", SDCard.usedMB(), SDCard.totalMB());
  g.drawString(sp, c.x + c.w - 6, c.y + 4);

  int top = listTop();
  int listH = Gfx.height() - Theme::NAV_H - top;
  g.setClipRect(0, top, c.w, listH);

  for (int i = 0; i < _count; i++) {
    int y = top + i * ROW_H - _scroll;
    if (y + ROW_H < top || y > top + listH) continue;
    if (i == _sel) g.fillRect(0, y, c.w, ROW_H, Theme::PANEL_HI);
    Entry& e = _entries[i];
    // icon
    int ix = 10, iy = y + ROW_H / 2;
    if (e.isDir) {
      g.fillRoundRect(ix, iy - 7, 20, 14, 2, Theme::WARN);
      g.fillRect(ix, iy - 10, 9, 4, Theme::WARN);
    } else {
      uint16_t fc = isAudioFile(e.name) ? Theme::GOOD
                  : isImageFile(e.name) ? Theme::ACCENT
                  : isTextFile(e.name)  ? Theme::TEXT : Theme::TEXT_DIM;
      g.drawRoundRect(ix + 2, iy - 9, 16, 19, 2, fc);
      g.drawFastHLine(ix + 5, iy - 3, 10, fc);
      g.drawFastHLine(ix + 5, iy + 1, 10, fc);
    }
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::TEXT);
    g.drawString(e.name, 38, iy);
    if (!e.isDir) {
      char szs[16];
      if (e.size >= 1048576) snprintf(szs, sizeof(szs), "%.1fM", e.size / 1048576.0f);
      else if (e.size >= 1024) snprintf(szs, sizeof(szs), "%uK", e.size / 1024);
      else snprintf(szs, sizeof(szs), "%uB", (unsigned)e.size);
      g.setTextDatum(lgfx::middle_right);
      g.setTextColor(Theme::TEXT_DIM);
      g.drawString(szs, c.w - 8, iy);
    }
    g.drawFastHLine(0, y + ROW_H - 1, c.w, Theme::PANEL);
  }
  if (_count == 0) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("(empty)", c.w / 2, top + listH / 2 - 12);
    g.setTextColor(Theme::WARN);
    g.drawString(_diag, c.w / 2, top + listH / 2 + 12);
  }
  // scrollbar
  int totalH = _count * ROW_H;
  if (totalH > listH) {
    int barH = listH * listH / totalH;
    if (barH < 16) barH = 16;
    int barY = top + (listH - barH) * _scroll / (totalH - listH);
    g.fillRoundRect(c.w - 4, barY, 3, barH, 1, Theme::PANEL_HI);
  }
  g.clearClipRect();
}

void FileManagerApp::drawOverlayMenu(lgfx::LGFX_Sprite& g) {
  int W = Gfx.width();
  int mw = W - 60, mh = 4 * 46 + 40, mx = 30, my = (Gfx.height() - mh) / 2;
  g.fillRoundRect(mx, my, mw, mh, 8, Theme::PANEL);
  g.drawRoundRect(mx, my, mw, mh, 8, Theme::ACCENT);
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::TEXT);
  g.drawString(_sel >= 0 ? _entries[_sel].name : "", mx + mw / 2, my + 8);
  const char* items[4] = {"Open", "Rename", "Delete", "Cancel"};
  for (int i = 0; i < 4; i++) {
    UIRect r = {mx + 12, my + 32 + i * 46, mw - 24, 38};
    UIDraw::button(g, r, items[i], false, i == 2 ? Theme::BAD : Theme::PANEL_HI);
  }
}

void FileManagerApp::drawConfirm(lgfx::LGFX_Sprite& g) {
  int W = Gfx.width();
  int mw = W - 60, mh = 130, mx = 30, my = (Gfx.height() - mh) / 2;
  g.fillRoundRect(mx, my, mw, mh, 8, Theme::PANEL);
  g.drawRoundRect(mx, my, mw, mh, 8, Theme::BAD);
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::TEXT);
  g.drawString("Delete this file?", mx + mw / 2, my + 12);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString(_sel >= 0 ? _entries[_sel].name : "", mx + mw / 2, my + 32);
  UIRect ry = {mx + 12, my + mh - 50, (mw - 36) / 2, 38};
  UIRect rn = {mx + 24 + (mw - 36) / 2, my + mh - 50, (mw - 36) / 2, 38};
  UIDraw::button(g, ry, "Delete", false, Theme::BAD);
  UIDraw::button(g, rn, "Cancel");
}

void FileManagerApp::drawViewer(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  int lineH = g.fontHeight() + 2;
  int top = c.y + 4;
  int visH = c.h - 8;
  g.setClipRect(0, c.y, c.w, c.h);
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::TEXT);
  int first = _viewScroll / lineH;
  for (int i = first; i < _lineCount && (i - first) * lineH < visH + lineH; i++) {
    int32_t s = _lineOff[i];
    int32_t e2 = (i + 1 < _lineCount) ? _lineOff[i + 1] : (int32_t)strlen(_text);
    char save = _text[e2];
    _text[e2] = 0;
    // strip trailing newline for display
    g.drawString(_text + s, c.x + 8, top + i * lineH - _viewScroll);
    _text[e2] = save;
  }
  g.clearClipRect();
}

void FileManagerApp::draw(lgfx::LGFX_Sprite& g) {
  if (_mode == NO_CARD) {
    UIRect c = contentArea();
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::WARN);
    g.drawString("No SD card detected", c.x + c.w / 2, c.y + c.h / 2 - 14);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("Insert a card and reopen Files", c.x + c.w / 2, c.y + c.h / 2 + 10);
    return;
  }
  if (_mode == VIEW_TEXT) { drawViewer(g); return; }
  drawBrowse(g);
  if (_mode == FILEMENU) drawOverlayMenu(g);
  if (_mode == CONFIRM_DELETE) drawConfirm(g);

  // Keyboard rename result
  if (_mode == RENAME && Kbd.done()) {
    if (!Kbd.cancelled() && _sel >= 0 && strlen(Kbd.text()) > 0) {
      char from[256], to[256];
      fullPathOf(_sel, from, sizeof(from));
      if (strcmp(_path, "/") == 0) snprintf(to, sizeof(to), "/%s", Kbd.text());
      else snprintf(to, sizeof(to), "%s/%s", _path, Kbd.text());
      if (SD_MMC.rename(from, to)) Notify.post("Renamed");
      else Notify.post("Rename failed");
      loadDir();
    }
    Kbd.close();
    _mode = BROWSE;
  }
}

void FileManagerApp::handleTouch(int x, int y, bool pressed) {
  bool tap = false;
  if (pressed && !_wasPressed) {
    _pressX = x; _pressY = y;
    _scrollStart = (_mode == VIEW_TEXT) ? _viewScroll : _scroll;
    _dragging = false;
  } else if (pressed && _wasPressed) {
    if (abs(y - _pressY) > 8) _dragging = true;
    if (_dragging) {
      int listH, totalH;
      if (_mode == VIEW_TEXT) {
        UIRect c = contentArea();
        listH = c.h - 8;
        int lineH = Gfx.canvas().fontHeight() + 2;
        totalH = _lineCount * lineH;
        _viewScroll = _scrollStart + (_pressY - y);
        int maxS = totalH > listH ? totalH - listH : 0;
        if (_viewScroll < 0) _viewScroll = 0;
        if (_viewScroll > maxS) _viewScroll = maxS;
      } else if (_mode == BROWSE) {
        listH = Gfx.height() - Theme::NAV_H - listTop();
        totalH = _count * ROW_H;
        _scroll = _scrollStart + (_pressY - y);
        int maxS = totalH > listH ? totalH - listH : 0;
        if (_scroll < 0) _scroll = 0;
        if (_scroll > maxS) _scroll = maxS;
      }
    }
  } else if (!pressed && _wasPressed) {
    if (!_dragging) tap = true;
  }
  _wasPressed = pressed;
  if (!tap) return;
  x = _pressX; y = _pressY;

  switch (_mode) {
    case NO_CARD:
      if (SDCard.mount()) { _mode = BROWSE; loadDir(); }
      break;

    case BROWSE: {
      int top = listTop();
      if (y < top) break;
      int idx = (y - top + _scroll) / ROW_H;
      if (idx < 0 || idx >= _count) break;
      if (_entries[idx].isDir) enterDir(_entries[idx].name);
      else { _sel = idx; _mode = FILEMENU; }
      break;
    }

    case FILEMENU: {
      int W = Gfx.width();
      int mw = W - 60, mh = 4 * 46 + 40, mx = 30, my = (Gfx.height() - mh) / 2;
      for (int i = 0; i < 4; i++) {
        UIRect r = {mx + 12, my + 32 + i * 46, mw - 24, 38};
        if (!r.contains(x, y)) continue;
        if (i == 0) { _mode = BROWSE; openFile(_sel); }
        else if (i == 1) { _mode = RENAME; Kbd.open("New name", _entries[_sel].name); }
        else if (i == 2) { _mode = CONFIRM_DELETE; }
        else { _mode = BROWSE; }
        return;
      }
      _mode = BROWSE;   // tap outside closes
      break;
    }

    case CONFIRM_DELETE: {
      int W = Gfx.width();
      int mw = W - 60, mh = 130, mx = 30, my = (Gfx.height() - mh) / 2;
      UIRect ry = {mx + 12, my + mh - 50, (mw - 36) / 2, 38};
      UIRect rn = {mx + 24 + (mw - 36) / 2, my + mh - 50, (mw - 36) / 2, 38};
      if (ry.contains(x, y) && _sel >= 0) {
        char full[256];
        fullPathOf(_sel, full, sizeof(full));
        if (SD_MMC.remove(full)) Notify.post("Deleted");
        else Notify.post("Delete failed");
        loadDir();
        _mode = BROWSE;
      } else if (rn.contains(x, y)) {
        _mode = BROWSE;
      }
      break;
    }

    case RENAME:
    case VIEW_TEXT:
      break;
  }
}

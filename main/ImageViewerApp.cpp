#include "ImageViewerApp.h"
#include "SDCardHAL.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include <SD_MMC.h>
#include <dirent.h>
#include <TJpg_Decoder.h>

ImageViewerApp ImageViewer;

static bool isJpg(const char* nm) {
  const char* dot = strrchr(nm, '.');
  return dot && (!strcasecmp(dot + 1, "jpg") || !strcasecmp(dot + 1, "jpeg"));
}
static bool isBmp(const char* nm) {
  const char* dot = strrchr(nm, '.');
  return dot && !strcasecmp(dot + 1, "bmp");
}

// TJpg decode callback: block into the canvas via pushImage
static lgfx::LGFX_Sprite* s_jpgTarget = nullptr;
static int s_offX = 0, s_offY = 0;
static bool jpgOut(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* data) {
  if (s_jpgTarget) s_jpgTarget->pushImage(s_offX + x, s_offY + y, w, h, (lgfx::rgb565_t*)data);
  return true;
}

void ImageViewerApp::playPath(const char* fullPath) {
  strlcpy(_pendingPath, fullPath, sizeof(_pendingPath));
  _pendingPlay = true;
}

void ImageViewerApp::onEnter() {
  if (!_imgs) _imgs = (Img*)ps_malloc(sizeof(Img) * MAX_IMGS);
  if (!SDCard.mount() || !_imgs) {
    Notify.post("No SD card");
    _count = 0;
    _mode = LIST;
    return;
  }
  if (_pendingPlay) {
    _pendingPlay = false;
    char* slash = strrchr(_pendingPath, '/');
    if (slash && slash != _pendingPath) {
      size_t n = slash - _pendingPath;
      memcpy(_dir, _pendingPath, n);
      _dir[n] = 0;
    } else strcpy(_dir, "/");
    scanDir();
    const char* base = slash ? slash + 1 : _pendingPath;
    _current = -1;
    for (int i = 0; i < _count; i++)
      if (!strcmp(_imgs[i].name, base)) { _current = i; break; }
    if (_current >= 0) { show(_current); return; }
  }
  // Prefer /photos or /images
  const char* dirs[3] = {"/photos", "/images", "/"};
  for (int d = 0; d < 3; d++) {
    strlcpy(_dir, dirs[d], sizeof(_dir));
    scanDir();
    if (_count) break;
  }
  _mode = LIST;
}

void ImageViewerApp::onExit() { _mode = LIST; }

bool ImageViewerApp::onBack() {
  if (_mode == VIEW) { _mode = LIST; return true; }
  return false;
}

void ImageViewerApp::scanDir() {
  _count = 0;
  _scroll = 0;
  char vfs[160];
  snprintf(vfs, sizeof(vfs), "/sdcard%s", strcmp(_dir, "/") == 0 ? "" : _dir);
  DIR* d = opendir(vfs);
  if (!d) return;
  struct dirent* de;
  while ((de = readdir(d)) && _count < MAX_IMGS) {
    if (de->d_type == DT_DIR || de->d_name[0] == '.') continue;
    if (!isJpg(de->d_name) && !isBmp(de->d_name)) continue;
    strlcpy(_imgs[_count].name, de->d_name, sizeof(_imgs[_count].name));
    _count++;
  }
  closedir(d);
}

void ImageViewerApp::show(int idx) {
  if (idx < 0 || idx >= _count) return;
  _current = idx;
  _needRender = true;
  _mode = VIEW;
}

bool ImageViewerApp::renderBmp(lgfx::LGFX_Sprite& g, const uint8_t* buf, size_t len) {
  if (len < 54 || buf[0] != 'B' || buf[1] != 'M') return false;
  uint32_t dataOff = buf[10] | (buf[11] << 8) | (buf[12] << 16) | (buf[13] << 24);
  int32_t w = buf[18] | (buf[19] << 8) | (buf[20] << 16) | (buf[21] << 24);
  int32_t h = buf[22] | (buf[23] << 8) | (buf[24] << 16) | (buf[25] << 24);
  uint16_t bpp = buf[28] | (buf[29] << 8);
  uint32_t comp = buf[30] | (buf[31] << 8) | (buf[32] << 16) | (buf[33] << 24);
  if (bpp != 24 || comp != 0 || w <= 0) return false;
  bool topDown = h < 0;
  if (topDown) h = -h;

  int W = g.width(), H = g.height();
  int step = 1;
  while (w / step > W || h / step > H) step++;
  int dw = w / step, dh = h / step;
  int ox = (W - dw) / 2, oy = (H - dh) / 2;
  size_t rowBytes = ((size_t)w * 3 + 3) & ~3;

  for (int dy = 0; dy < dh; dy++) {
    int sy = dy * step;
    int srcRow = topDown ? sy : (h - 1 - sy);
    const uint8_t* row = buf + dataOff + (size_t)srcRow * rowBytes;
    if (buf + len < row + rowBytes) break;
    for (int dx = 0; dx < dw; dx++) {
      const uint8_t* p = row + (size_t)dx * step * 3;
      g.drawPixel(ox + dx, oy + dy, g.color565(p[2], p[1], p[0]));
    }
  }
  return true;
}

bool ImageViewerApp::renderImage(lgfx::LGFX_Sprite& g, const char* path) {
  char vfs[256];
  snprintf(vfs, sizeof(vfs), "/sdcard%s", path);
  FILE* f = fopen(vfs, "rb");
  if (!f) { strlcpy(_err, "open failed", sizeof(_err)); return false; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 6 * 1024 * 1024) { fclose(f); strlcpy(_err, "file too big", sizeof(_err)); return false; }
  uint8_t* buf = (uint8_t*)ps_malloc(sz);
  if (!buf) { fclose(f); strlcpy(_err, "out of memory", sizeof(_err)); return false; }
  size_t got = fread(buf, 1, sz, f);
  fclose(f);

  g.fillScreen(TFT_BLACK);
  bool ok = false;
  if (isBmp(path)) {
    ok = renderBmp(g, buf, got);
    if (!ok) strlcpy(_err, "unsupported BMP", sizeof(_err));
  } else {
    uint16_t w = 0, h = 0;
    if (TJpgDec.getJpgSize(&w, &h, buf, got) == 0 && w && h) {
      uint8_t scale = 1;
      while ((w / scale > g.width() || h / scale > g.height()) && scale < 8) scale <<= 1;
      TJpgDec.setJpgScale(scale);
      TJpgDec.setSwapBytes(false);
      TJpgDec.setCallback(jpgOut);
      s_jpgTarget = &g;
      s_offX = (g.width() - w / scale) / 2;
      s_offY = (g.height() - h / scale) / 2;
      ok = (TJpgDec.drawJpg(0, 0, buf, got) == 0);
      s_jpgTarget = nullptr;
      if (!ok) strlcpy(_err, "JPEG decode failed", sizeof(_err));
    } else {
      strlcpy(_err, "bad JPEG header", sizeof(_err));
    }
  }
  free(buf);
  return ok;
}

void ImageViewerApp::drawList(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::ACCENT);
  char hdr[96];
  snprintf(hdr, sizeof(hdr), "%s  (%d images)", _dir, _count);
  g.drawString(hdr, c.x + 6, c.y + 4);

  int top = c.y + 24;
  int listH = c.y + c.h - top;
  g.setClipRect(0, top, c.w, listH);
  for (int i = 0; i < _count; i++) {
    int y = top + i * ROW_H - _scroll;
    if (y + ROW_H < top || y > top + listH) continue;
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::TEXT);
    g.drawString(_imgs[i].name, 14, y + ROW_H / 2);
    g.drawFastHLine(0, y + ROW_H - 1, c.w, Theme::PANEL);
  }
  if (_count == 0) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("No JPG/BMP files found", c.w / 2, top + listH / 2);
  }
  g.clearClipRect();
}

void ImageViewerApp::draw(lgfx::LGFX_Sprite& g) {
  if (_mode == LIST) { drawList(g); return; }

  if (_needRender) {
    _needRender = false;
    char full[256];
    if (!strcmp(_dir, "/")) snprintf(full, sizeof(full), "/%s", _imgs[_current].name);
    else snprintf(full, sizeof(full), "%s/%s", _dir, _imgs[_current].name);
    _renderOk = renderImage(g, full);
  } else if (!_renderOk) {
    g.fillScreen(TFT_BLACK);
  }
  // In VIEW mode the canvas keeps the decoded image between frames (we skip
  // the usual background clear because fullscreen apps own the whole canvas).
  if (!_renderOk) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::BAD);
    g.drawString(_err, g.width() / 2, g.height() / 2);
  }
  // filename + position, bottom
  g.setTextDatum(lgfx::bottom_center);
  g.setTextColor(Theme::TEXT_DIM);
  char cap[96];
  snprintf(cap, sizeof(cap), "%s  (%d/%d)", _imgs[_current].name, _current + 1, _count);
  g.drawString(cap, g.width() / 2, g.height() - 4);
}

void ImageViewerApp::handleTouch(int x, int y, bool pressed) {
  bool tap = false;
  if (pressed && !_wasPressed) {
    _pressX = x; _pressY = y;
    _scrollStart = _scroll;
    _dragging = false;
  } else if (pressed && _wasPressed) {
    if (_mode == LIST && abs(y - _pressY) > 8) _dragging = true;
    if (_dragging) {
      UIRect c = contentArea();
      int listH = c.h - 24;
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

  if (_mode == VIEW) {
    // left third = prev, right third = next
    if (x < Gfx.width() / 3 && _current > 0) show(_current - 1);
    else if (x > 2 * Gfx.width() / 3 && _current < _count - 1) show(_current + 1);
    return;
  }

  UIRect c = contentArea();
  int top = c.y + 24;
  if (y < top) return;
  int idx = (y - top + _scroll) / ROW_H;
  if (idx >= 0 && idx < _count) show(idx);
}

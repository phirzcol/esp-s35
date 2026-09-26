#include "NesApp.h"
#include "DisplayHAL.h"
#include "TouchHAL.h"
#include "SDCardHAL.h"
#include "SystemTask.h"
#include "AppManager.h"
#include "AudioHAL.h"
#include <dirent.h>
#include <SD_MMC.h>

extern "C" {
#include "src/nofrendo/nofrendo.h"
}

NesApp Nes;

// ---- emulator task state ----
static TaskHandle_t s_emuTask = nullptr;
static nes_t* s_nes = nullptr;
static uint8_t* s_vidbuf = nullptr;        // NES_SCREEN_PITCH x 240, internal RAM
static uint16_t s_pal565[256];             // panel byte order
static volatile uint32_t s_buttons = 0;
static volatile bool s_emuRun = false;
static volatile bool s_frameReady = false;
static volatile bool s_audioOk = false;
static volatile uint32_t s_emuFrames = 0;

// nofrendo calls this at the end of each rendered frame
extern "C" void nesBlitCallback(void*) {
  s_frameReady = true;
}

static void emuTask(void*) {
  const TickType_t period = pdMS_TO_TICKS(16);   // fallback pacing when no audio
  TickType_t last = xTaskGetTickCount();
  while (s_emuRun) {
    input_update(0, (int)s_buttons);
    // nes_reset() nulls the vidbuf, so re-attach every frame (retro-go does the same)
    nes_setvidbuf(s_vidbuf);
    bool drawFrame = !s_frameReady;
    nes_emulate(drawFrame);
    s_emuFrames++;
    if (s_audioOk && s_nes && s_nes->apu && s_nes->apu->buffer) {
      // Blocking I2S write paces emulation at exactly the sample rate
      AudioSys.rawWrite(s_nes->apu->buffer, s_nes->apu->samples_per_frame);
    } else {
      vTaskDelayUntil(&last, period);
    }
  }
  s_emuTask = nullptr;
  vTaskDelete(nullptr);
}

// ---- touch controller layout (portrait 320x480, image on top) ----
// dpad bottom-left, A/B bottom-right, start/select center
struct PadZone { UIRect r; uint32_t bits; const char* label; };

static const int IMG_X = 32, IMG_Y = 34;   // 256x240 centered horizontally

static PadZone zones[8];
static void buildZones() {
  int W = Gfx.width(), H = Gfx.height();
  int cy = H - 105;             // control row center
  int dx = 88, dy = cy;         // dpad center
  int bs = 58;                  // dpad button size
  zones[0] = {{dx - bs / 2, dy - bs - bs / 2, bs, bs}, NES_PAD_UP, "^"};
  zones[1] = {{dx - bs / 2, dy + bs / 2, bs, bs}, NES_PAD_DOWN, "v"};
  zones[2] = {{dx - bs - bs / 2, dy - bs / 2, bs, bs}, NES_PAD_LEFT, "<"};
  zones[3] = {{dx + bs / 2, dy - bs / 2, bs, bs}, NES_PAD_RIGHT, ">"};
  int ax = W - 62;
  zones[4] = {{ax - 29, cy - 88, 58, 58}, NES_PAD_A, "A"};
  zones[5] = {{ax - 29, cy - 18, 58, 58}, NES_PAD_B, "B"};   // stacked under A
  zones[6] = {{W / 2 - 60, H - 30, 55, 24}, NES_PAD_SELECT, "SEL"};
  zones[7] = {{W / 2 + 5, H - 30, 55, 24}, NES_PAD_START, "STA"};
}

void NesApp::updateButtons() {
  MultiTouch mt = Touch.readMulti();
  uint32_t b = 0;
  for (int i = 0; i < mt.count; i++)
    for (auto& z : zones)
      if (z.r.contains(mt.x[i], mt.y[i])) b |= z.bits;
  s_buttons = b;
}

// ---- lifecycle ----

bool NesApp::startRom(const char* path) {
  if (!s_vidbuf) {
    s_vidbuf = (uint8_t*)heap_caps_malloc(NES_SCREEN_PITCH * NES_SCREEN_HEIGHT,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_vidbuf)   // internal RAM taken (e.g. audio engine); PSRAM is slower but works
      s_vidbuf = (uint8_t*)ps_malloc(NES_SCREEN_PITCH * NES_SCREEN_HEIGHT);
    if (!s_vidbuf) { Notify.post("NES: out of memory"); return false; }
  }
  if (!s_nes) {
    s_nes = nes_init(SYS_DETECT, 48000, false, nullptr);   // 48k matches codec default config
    if (!s_nes) { Notify.post("NES: init failed"); return false; }
  }
  char vfs[256];
  snprintf(vfs, sizeof(vfs), "/sdcard%s", path);
  int ret = nes_loadfile(vfs);
  if (ret < 0) {
    Notify.post(ret == -2 ? "Unsupported mapper" : "ROM load failed");
    return false;
  }
  // Panel-byte-order palette (canvas buffer is pushed raw)
  uint16_t* pal = (uint16_t*)nofrendo_buildpalette(NES_PALETTE_PVM, 16);
  for (int i = 0; i < 256; i++) s_pal565[i] = (pal[i] >> 8) | (pal[i] << 8);
  free(pal);

  memset(s_vidbuf, 0, NES_SCREEN_PITCH * NES_SCREEN_HEIGHT);
  nes_setvidbuf(s_vidbuf);
  s_nes->blit_func = (void (*)(uint8*))nesBlitCallback;
  nes_reset(true);

  s_buttons = 0;
  s_frameReady = false;
  s_audioOk = AudioSys.rawStart(48000);
  s_emuRun = true;
  buildZones();
  // Core 0 with the other background work; prio above touch poller
  xTaskCreatePinnedToCore(emuTask, "nesTask", 8192, nullptr, 4, &s_emuTask, 0);
  _mode = PLAY;
  return true;
}

void NesApp::stopEmu() {
  if (s_emuRun) {
    s_emuRun = false;
    while (s_emuTask) vTaskDelay(1);   // task self-deletes
  }
  AudioSys.rawStop();
  s_audioOk = false;
  if (s_nes) { nes_shutdown(); s_nes = nullptr; }
  if (s_vidbuf) { free(s_vidbuf); s_vidbuf = nullptr; }
  _mode = LIST;
}

void NesApp::playPath(const char* fullPath) {
  strlcpy(_pendingPath, fullPath, sizeof(_pendingPath));
  _pendingPlay = true;
}

void NesApp::onEnter() {
  if (!_roms) _roms = (Rom*)ps_malloc(sizeof(Rom) * MAX_ROMS);
  AudioSys.stop();   // free codec/CPU while emulating
  if (!SDCard.mount() || !_roms) {
    Notify.post("No SD card");
    _count = 0;
    _mode = LIST;
    return;
  }
  if (_pendingPlay) {
    _pendingPlay = false;
    if (startRom(_pendingPath)) return;
  }
  _mode = LIST;
  scanDir();
}

void NesApp::onExit() { stopEmu(); }

bool NesApp::onBack() {
  if (_mode == PLAY) { stopEmu(); scanDir(); return true; }
  return false;
}

void NesApp::scanDir() {
  _count = 0;
  _scroll = 0;
  // Prefer /roms, fall back to /nes then root
  const char* dirs[3] = {"/roms", "/nes", "/"};
  for (int d = 0; d < 3 && _count == 0; d++) {
    strlcpy(_dir, dirs[d], sizeof(_dir));
    char vfs[128];
    snprintf(vfs, sizeof(vfs), "/sdcard%s", strcmp(_dir, "/") == 0 ? "" : _dir);
    DIR* dp = opendir(vfs);
    if (!dp) continue;
    struct dirent* de;
    while ((de = readdir(dp)) && _count < MAX_ROMS) {
      if (de->d_type == DT_DIR || de->d_name[0] == '.') continue;
      const char* dot = strrchr(de->d_name, '.');
      if (!dot || strcasecmp(dot + 1, "nes")) continue;
      strlcpy(_roms[_count].name, de->d_name, sizeof(_roms[_count].name));
      _count++;
    }
    closedir(dp);
  }
}

// ---- drawing ----

void NesApp::drawList(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::ACCENT);
  char hdr[96];
  snprintf(hdr, sizeof(hdr), "%s  (%d ROMs)", _dir, _count);
  g.drawString(hdr, c.x + 6, c.y + 4);

  int top = c.y + 24;
  int listH = c.y + c.h - top;
  g.setClipRect(0, top, c.w, listH);
  for (int i = 0; i < _count; i++) {
    int y = top + i * ROW_H - _scroll;
    if (y + ROW_H < top || y > top + listH) continue;
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::TEXT);
    g.drawString(_roms[i].name, 14, y + ROW_H / 2);
    g.drawFastHLine(0, y + ROW_H - 1, c.w, Theme::PANEL);
  }
  if (_count == 0) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("No .nes files found", c.w / 2, top + listH / 2 - 12);
    g.drawString("Put ROMs in /roms on the SD card", c.w / 2, top + listH / 2 + 12);
  }
  g.clearClipRect();
}

void NesApp::drawPlay(lgfx::LGFX_Sprite& g) {
  updateButtons();

  // Palette blit: 8bpp indexed -> panel-order RGB565 directly into the canvas
  if (s_vidbuf) {
    uint16_t* dst0 = (uint16_t*)g.getBuffer();
    int W = g.width();
    for (int y = 0; y < NES_SCREEN_HEIGHT; y++) {
      const uint8_t* src = s_vidbuf + (size_t)y * NES_SCREEN_PITCH + NES_SCREEN_OVERDRAW;
      uint16_t* dst = dst0 + (size_t)(IMG_Y + y) * W + IMG_X;
      for (int x = 0; x < NES_SCREEN_WIDTH; x++) dst[x] = s_pal565[src[x]];
    }
    s_frameReady = false;   // let the emu render the next one
  }

  // Controller overlay
  uint32_t b = s_buttons;
  for (auto& z : zones) {
    bool on = b & z.bits;
    g.drawRoundRect(z.r.x, z.r.y, z.r.w, z.r.h, 8, on ? Theme::ACCENT : Theme::PANEL_HI);
    if (on) g.drawRoundRect(z.r.x + 1, z.r.y + 1, z.r.w - 2, z.r.h - 2, 7, Theme::ACCENT);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(on ? Theme::ACCENT : Theme::TEXT_DIM);
    g.drawString(z.label, z.r.x + z.r.w / 2, z.r.y + z.r.h / 2);
  }
}

void NesApp::draw(lgfx::LGFX_Sprite& g) {
  if (_mode == PLAY) drawPlay(g);
  else drawList(g);
}

void NesApp::handleTouch(int x, int y, bool pressed) {
  if (_mode == PLAY) return;   // buttons read via readMulti() in drawPlay

  bool tap = false;
  if (pressed && !_wasPressed) {
    _pressX = x; _pressY = y;
    _scrollStart = _scroll;
    _dragging = false;
  } else if (pressed && _wasPressed) {
    if (abs(y - _pressY) > 8) _dragging = true;
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

  UIRect c = contentArea();
  int top = c.y + 24;
  if (_pressY < top) return;
  int idx = (_pressY - top + _scroll) / ROW_H;
  if (idx < 0 || idx >= _count) return;
  char full[256];
  if (!strcmp(_dir, "/")) snprintf(full, sizeof(full), "/%s", _roms[idx].name);
  else snprintf(full, sizeof(full), "%s/%s", _dir, _roms[idx].name);
  startRom(full);
}

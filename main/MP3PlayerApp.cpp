#include "MP3PlayerApp.h"
#include "AudioHAL.h"
#include "SDCardHAL.h"
#include "SettingsStore.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include <SD_MMC.h>
#include <arduinoFFT.h>
#include <dirent.h>
#include <Preferences.h>

MP3PlayerApp MP3Player;

// Radial shockwave visualizer (ported from panellanmp3vissd)
static const int FFT_N = 512;
static const int VIS_RINGS = 16;
static const int VIS_POINTS = 144;
static const int VIS_SPACING = 8;
static float s_vReal[FFT_N];
static float s_vImag[FFT_N];
static ArduinoFFT<float> s_fft(s_vReal, s_vImag, FFT_N, 44100.0f);
static int16_t s_waveBuf[FFT_N];

static uint16_t hsv565(lgfx::LGFX_Sprite& g, int hue, int sat, int val) {
  float h = hue % 360; if (h < 0) h += 360;
  float s = sat / 255.0f, v = val / 255.0f;
  int i = (int)(h / 60.0f);
  float f = (h / 60.0f) - i;
  float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  float r = 0, gg = 0, b = 0;
  switch (i % 6) {
    case 0: r = v; gg = t; b = p; break;
    case 1: r = q; gg = v; b = p; break;
    case 2: r = p; gg = v; b = t; break;
    case 3: r = p; gg = q; b = v; break;
    case 4: r = t; gg = p; b = v; break;
    case 5: r = v; gg = p; b = q; break;
  }
  return g.color565((uint8_t)(r * 255), (uint8_t)(gg * 255), (uint8_t)(b * 255));
}

static bool isAudio(const char* nm) {
  const char* dot = strrchr(nm, '.');
  if (!dot) return false;
  return !strcasecmp(dot + 1, "mp3") || !strcasecmp(dot + 1, "wav") ||
         !strcasecmp(dot + 1, "flac") || !strcasecmp(dot + 1, "aac") ||
         !strcasecmp(dot + 1, "m4a") || !strcasecmp(dot + 1, "ogg");
}

void MP3PlayerApp::playPath(const char* fullPath) {
  strlcpy(_pendingPath, fullPath, sizeof(_pendingPath));
  _pendingPlay = true;
}

void MP3PlayerApp::saveResume() {
  if (!_curPath[0]) return;
  Preferences p;
  p.begin("mp3", false);
  p.putString("rpath", _curPath);
  p.putUInt("rpos", AudioSys.currentSec());
  p.end();
}

void MP3PlayerApp::loadResume() {
  Preferences p;
  p.begin("mp3", true);
  String rp = p.getString("rpath", "");
  _resumePos = p.getUInt("rpos", 0);
  p.end();
  strlcpy(_resumePath, rp.c_str(), sizeof(_resumePath));
}

void MP3PlayerApp::onEnter() {
  if (!_tracks) _tracks = (Track*)ps_malloc(sizeof(Track) * MAX_TRACKS);
  AudioSys.begin();
  AudioSys.setSpectrum(true);
  _lastTouchMs = millis();
  loadResume();
  if (!SDCard.mount() || !_tracks) {
    Notify.post("No SD card");
    _count = 0;
    return;
  }

  if (_pendingPlay) {
    // Directory of the requested file becomes the playlist
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
      if (!strcmp(_tracks[i].name, base)) { _current = i; break; }
    if (_current >= 0) playIndex(_current);
    _mode = PLAYER;
    _pendingPlay = false;
    return;
  }

  // Prefer /music when present
  if (!AudioSys.isActive()) {
    File m = SD_MMC.open("/music");
    bool haveMusic = m && m.isDirectory();
    if (m) m.close();
    strcpy(_dir, haveMusic ? "/music" : "/");
    scanDir();
    _mode = LIST;
  } else {
    _mode = PLAYER;   // returning to an active session
    scanDir();
  }
}

bool MP3PlayerApp::onBack() {
  if (_mode == VIS) { exitVis(); return true; }
  if (_mode == PLAYER) { _mode = LIST; return true; }
  if (_mode == LIST && strcmp(_dir, "/") != 0) { upDir(); return true; }
  return false;
}

void MP3PlayerApp::onExit() {
  if (_mode == VIS) exitVis();
  if (AudioSys.isActive()) saveResume();
  AudioSys.setSpectrum(false);   // no FFT load while the player UI is hidden
}

bool MP3PlayerApp::inhibitScreensaver() const {
  return _mode == VIS && AudioSys.isActive() && !AudioSys.isPaused();
}

void MP3PlayerApp::enterVis() {
  if (!_ringHist) {
    _ringHist = (float*)ps_calloc(VIS_RINGS * VIS_POINTS, sizeof(float));
    _smoothBins = (float*)ps_calloc(65, sizeof(float));
    _cosT = (float*)ps_malloc(VIS_POINTS * sizeof(float));
    _sinT = (float*)ps_malloc(VIS_POINTS * sizeof(float));
    if (_cosT && _sinT)
      for (int p = 0; p < VIS_POINTS; p++) {
        float a = p * 2.0f * PI / VIS_POINTS;
        _cosT[p] = cosf(a);
        _sinT[p] = sinf(a);
      }
  }
  if (_ringHist) memset(_ringHist, 0, VIS_RINGS * VIS_POINTS * sizeof(float));
  AudioSys.setCapture(true);
  _mode = VIS;
}

void MP3PlayerApp::exitVis() {
  AudioSys.setCapture(false);
  _mode = PLAYER;
  _lastTouchMs = millis();
}

void MP3PlayerApp::onSettings() {
  Settings.visualizerOn = !Settings.visualizerOn;
  Settings.save();
  Notify.post(Settings.visualizerOn ? "Visualizer on" : "Visualizer off");
}

void MP3PlayerApp::scanDir() {
  _count = 0;
  _scroll = 0;
  char vfs[224];
  snprintf(vfs, sizeof(vfs), "/sdcard%s", strcmp(_dir, "/") == 0 ? "" : _dir);
  DIR* d = opendir(vfs);
  if (!d) return;
  struct dirent* de;
  while ((de = readdir(d)) && _count < MAX_TRACKS) {
    if (de->d_name[0] == '.') continue;
    bool dirEntry = (de->d_type == DT_DIR);
    if (!dirEntry && !isAudio(de->d_name)) continue;
    strlcpy(_tracks[_count].name, de->d_name, sizeof(_tracks[_count].name));
    _tracks[_count].isDir = dirEntry;
    _count++;
  }
  closedir(d);
  for (int i = 1; i < _count; i++) {   // dirs first, then alpha
    Track key = _tracks[i];
    int j = i - 1;
    while (j >= 0) {
      bool after = (_tracks[j].isDir == key.isDir)
                   ? (strcasecmp(_tracks[j].name, key.name) > 0)
                   : (!_tracks[j].isDir && key.isDir);
      if (!after) break;
      _tracks[j + 1] = _tracks[j];
      j--;
    }
    _tracks[j + 1] = key;
  }
}

void MP3PlayerApp::enterDir(const char* nm) {
  size_t len = strlen(_dir);
  if (strcmp(_dir, "/") == 0) snprintf(_dir, sizeof(_dir), "/%s", nm);
  else if (len + strlen(nm) + 2 < sizeof(_dir)) {
    _dir[len] = '/';
    strlcpy(_dir + len + 1, nm, sizeof(_dir) - len - 1);
  }
  scanDir();
}

void MP3PlayerApp::upDir() {
  char* slash = strrchr(_dir, '/');
  if (slash == _dir) _dir[1] = 0;
  else *slash = 0;
  scanDir();
}

void MP3PlayerApp::playIndex(int idx) {
  if (idx < 0 || idx >= _count || _tracks[idx].isDir) return;
  _current = idx;
  char full[256];
  if (!strcmp(_dir, "/")) snprintf(full, sizeof(full), "/%s", _tracks[idx].name);
  else snprintf(full, sizeof(full), "%s/%s", _dir, _tracks[idx].name);
  strlcpy(_curPath, full, sizeof(_curPath));
  AudioSys.playFile(full);
  saveResume();
  _mode = PLAYER;
}

void MP3PlayerApp::nextTrack(int dir) {
  if (_count == 0) return;
  int idx = _current;
  for (int step = 0; step < _count; step++) {
    idx += dir;
    if (idx < 0) idx = _count - 1;
    if (idx >= _count) idx = 0;
    if (!_tracks[idx].isDir) { playIndex(idx); return; }
  }
}

void MP3PlayerApp::update(uint32_t) {
  if (AudioSys.consumeEof()) nextTrack(+1);   // auto-advance

  // Periodic resume checkpoint for audiobooks
  if (AudioSys.isActive() && !AudioSys.isPaused() && millis() - _lastResumeSave > 30000) {
    _lastResumeSave = millis();
    saveResume();
  }

  // Visualizer doubles as screensaver: idle in player view while music plays
  if (_mode == PLAYER && Settings.visualizerOn && Settings.screensaverOn &&
      AudioSys.isActive() && !AudioSys.isPaused() &&
      millis() - _lastTouchMs > (uint32_t)Settings.screensaverSec * 1000) {
    enterVis();
  }
}

void MP3PlayerApp::drawList(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::ACCENT);
  char hdr[96];
  snprintf(hdr, sizeof(hdr), "%s  (%d)", _dir, _count);
  g.drawString(hdr, c.x + 6, c.y + 4);

  int top = c.y + 24;
  // Pinned resume row (audiobooks): last played file + position
  if (_resumePath[0]) {
    UIRect rr = {c.x + 4, top, c.w - 8, 38};
    g.fillRoundRect(rr.x, rr.y, rr.w, rr.h, 6, Theme::PANEL_HI);
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::GOOD);
    const char* base = strrchr(_resumePath, '/');
    char lbl[80];
    snprintf(lbl, sizeof(lbl), "Resume %s  %lu:%02lu", base ? base + 1 : _resumePath,
             (unsigned long)_resumePos / 60, (unsigned long)_resumePos % 60);
    g.drawString(lbl, rr.x + 10, rr.y + rr.h / 2);
    top += 44;
  }

  int listH = c.y + c.h - top;
  g.setClipRect(0, top, c.w, listH);
  for (int i = 0; i < _count; i++) {
    int y = top + i * ROW_H - _scroll;
    if (y + ROW_H < top || y > top + listH) continue;
    if (i == _current) g.fillRect(0, y, c.w, ROW_H, Theme::PANEL_HI);
    int iy = y + ROW_H / 2;
    if (_tracks[i].isDir) {
      g.fillRoundRect(10, iy - 7, 20, 14, 2, Theme::WARN);
      g.fillRect(10, iy - 10, 9, 4, Theme::WARN);
    } else {
      g.fillCircle(14, iy + 5, 4, Theme::GOOD);
      g.drawFastVLine(18, iy - 7, 12, Theme::GOOD);
    }
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(_tracks[i].isDir ? Theme::TEXT : (i == _current ? Theme::ACCENT : Theme::TEXT));
    g.drawString(_tracks[i].name, 36, iy);
    g.drawFastHLine(0, y + ROW_H - 1, c.w, Theme::PANEL);
  }
  if (_count == 0) {
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString("No audio files found", c.w / 2, top + listH / 2);
  }
  int totalH = _count * ROW_H;
  if (totalH > listH) {
    int barH = max(16, listH * listH / totalH);
    int barY = top + (listH - barH) * _scroll / (totalH - listH);
    g.fillRoundRect(c.w - 4, barY, 3, barH, 1, Theme::PANEL_HI);
  }
  g.clearClipRect();
}

// Player view layout helpers
static UIRect btnPrev()  { return { Gfx.width()/2 - 100, Gfx.height() - Theme::NAV_H - 70, 56, 48 }; }
static UIRect btnPlay()  { return { Gfx.width()/2 - 28,  Gfx.height() - Theme::NAV_H - 70, 56, 48 }; }
static UIRect btnNext()  { return { Gfx.width()/2 + 44,  Gfx.height() - Theme::NAV_H - 70, 56, 48 }; }
static UIRect volRect()  { return { 20, Gfx.height() - Theme::NAV_H - 130, Gfx.width() - 40, 44 }; }
static UIRect seekRect() {
  // generous touch band around the progress bar
  UIRect c = AppBase::contentArea();
  return { 20, c.y + 38, Gfx.width() - 40, 26 };
}

void MP3PlayerApp::drawPlayer(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();

  // Title
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::TEXT);
  g.drawString(AudioSys.title(), c.w / 2, c.y + 8);

  // Time + progress (shows seek target while dragging)
  uint32_t dur = AudioSys.durationSec();
  uint32_t cur = _seekDrag ? _seekTarget : AudioSys.currentSec();
  char ts[24];
  snprintf(ts, sizeof(ts), "%lu:%02lu / %lu:%02lu",
           (unsigned long)cur / 60, (unsigned long)cur % 60,
           (unsigned long)dur / 60, (unsigned long)dur % 60);
  g.setTextColor(_seekDrag ? Theme::ACCENT : Theme::TEXT_DIM);
  g.drawString(ts, c.w / 2, c.y + 28);
  int px = 20, pw = c.w - 40, py = c.y + 48;
  g.fillRoundRect(px, py, pw, 6, 3, Theme::PANEL_HI);
  if (dur > 0) {
    int fx = (int)((int64_t)pw * cur / dur);
    g.fillRoundRect(px, py, fx, 6, 3, Theme::ACCENT);
    g.fillCircle(px + fx, py + 3, _seekDrag ? 8 : 5, Theme::ACCENT);
  }

  // Visualizer
  if (Settings.visualizerOn) {
    uint8_t bars[AudioHAL::BANDS], peaks[AudioHAL::BANDS];
    AudioSys.getSpectrum(bars, peaks);
    int vx = 20, vw = c.w - 40;
    int vy = py + 16;
    int vh = volRect().y - vy - 10;
    int bw = vw / AudioHAL::BANDS;
    for (int i = 0; i < AudioHAL::BANDS; i++) {
      int h = (int)bars[i] * vh / 100;
      int x0 = vx + i * bw;
      // gradient green->red by height
      uint16_t col = h > vh * 3 / 4 ? Theme::BAD : (h > vh / 2 ? Theme::WARN : Theme::GOOD);
      g.fillRect(x0 + 2, vy + vh - h, bw - 4, h, col);
      int ph = (int)peaks[i] * vh / 100;
      g.fillRect(x0 + 2, vy + vh - ph - 2, bw - 4, 2, Theme::TEXT);
    }
  }

  // Volume
  UIRect vr = volRect();
  UIDraw::slider(g, vr, Settings.volume, 0, 100, "Volume");

  // Transport
  UIRect rp = btnPrev(), rl = btnPlay(), rn = btnNext();
  UIDraw::button(g, rp, "", false, Theme::PANEL);
  UIDraw::button(g, rl, "", false, Theme::PANEL_HI);
  UIDraw::button(g, rn, "", false, Theme::PANEL);
  // glyphs
  int cyy = rp.y + rp.h / 2;
  g.fillTriangle(rp.x + 36, cyy - 10, rp.x + 36, cyy + 10, rp.x + 18, cyy, Theme::TEXT);
  g.fillRect(rp.x + 14, cyy - 10, 3, 20, Theme::TEXT);
  if (AudioSys.isActive() && !AudioSys.isPaused()) {
    g.fillRect(rl.x + 18, cyy - 10, 7, 20, Theme::TEXT);
    g.fillRect(rl.x + 31, cyy - 10, 7, 20, Theme::TEXT);
  } else {
    g.fillTriangle(rl.x + 20, cyy - 11, rl.x + 20, cyy + 11, rl.x + 40, cyy, Theme::TEXT);
  }
  g.fillTriangle(rn.x + 20, cyy - 10, rn.x + 20, cyy + 10, rn.x + 38, cyy, Theme::TEXT);
  g.fillRect(rn.x + 39, cyy - 10, 3, 20, Theme::TEXT);
}

void MP3PlayerApp::drawVis(lgfx::LGFX_Sprite& g) {
  if (!_ringHist || !_cosT) return;

  // FFT the latest samples (UI core; audio decode unaffected on core 0)
  AudioSys.getWave(s_waveBuf, FFT_N);
  for (int i = 0; i < FFT_N; i++) {
    s_vReal[i] = (float)s_waveBuf[i];
    s_vImag[i] = 0;
  }
  s_fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  s_fft.compute(FFTDirection::Forward);
  s_fft.complexToMagnitude();

  // Live ring from spectrum, mapped around the circle (mirror at 180)
  float sens = Settings.visSens / 50.0f;
  if (sens < 0.1f) sens = 0.1f;
  float live[VIS_POINTS];
  for (int p = 0; p < VIS_POINTS; p++) {
    int angle = (p * 360) / VIS_POINTS;
    int bin = (angle <= 180) ? map(angle, 0, 180, 2, 58) : map(angle, 181, 360, 58, 2);
    float raw = (s_vReal[bin] * sens) / 1500.0f;
    if (raw < 0) raw = 0;
    raw = 50.0f * raw / (raw + 25.0f);   // soft knee instead of hard clip at 50
    _smoothBins[bin] = _smoothBins[bin] * 0.6f + raw * 0.4f;
    live[p] = _smoothBins[bin];
  }

  // Propagate rings outward
  if (millis() - _lastPropagate > 30) {
    for (int r = VIS_RINGS - 1; r > 0; r--)
      memcpy(&_ringHist[r * VIS_POINTS], &_ringHist[(r - 1) * VIS_POINTS], VIS_POINTS * sizeof(float));
    _lastPropagate = millis();
  }
  memcpy(&_ringHist[0], live, VIS_POINTS * sizeof(float));

  int cx = Gfx.width() / 2, cy2 = Gfx.height() / 2;
  _hueOff += 2.5f;
  if (_hueOff >= 360) _hueOff -= 360;
  const int fadeStep = 255 / VIS_RINGS;
  const int hueStep = 360 / VIS_RINGS;

  for (int r = VIS_RINGS - 1; r >= 0; r--) {
    float ampF = Settings.visAmp / 50.0f;
    if (ampF < 0.1f) ampF = 0.1f;
    float baseR = 25 + r * VIS_SPACING;
    uint16_t col;
    if (r == 0) col = TFT_WHITE;
    else {
      int bright = 255 - r * fadeStep;
      if (bright < 0) bright = 0;
      col = hsv565(g, ((int)(_hueOff) + (VIS_RINGS - r) * hueStep) % 360, 255, bright);
    }
    int firstX = 0, firstY = 0, prevX = 0, prevY = 0;
    float* ring = &_ringHist[r * VIS_POINTS];
    for (int p = 0; p < VIS_POINTS; p++) {
      float R = baseR + ring[p] * ampF;
      int x = cx + (int)(R * _cosT[p]);
      int y = cy2 + (int)(R * _sinT[p]);
      if (p == 0) { firstX = x; firstY = y; }
      else g.drawLine(prevX, prevY, x, y, col);
      prevX = x; prevY = y;
    }
    g.drawLine(prevX, prevY, firstX, firstY, col);
    if (r == 0) g.drawCircle(cx, cy2, 24, TFT_MAROON);
  }

  // Track name, unobtrusive
  g.setTextDatum(lgfx::bottom_center);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString(AudioSys.title(), cx, Gfx.height() - 6);
}

void MP3PlayerApp::draw(lgfx::LGFX_Sprite& g) {
  if (_mode == LIST) drawList(g);
  else if (_mode == PLAYER) drawPlayer(g);
  else drawVis(g);
}

void MP3PlayerApp::handleTouch(int x, int y, bool pressed) {
  if (pressed) _lastTouchMs = millis();

  if (_mode == VIS) {
    // any tap exits back to player controls
    if (pressed && !_wasPressed) exitVis();
    _wasPressed = pressed;
    return;
  }

  bool tap = false;
  if (pressed && !_wasPressed) {
    _pressX = x; _pressY = y;
    _scrollStart = _scroll;
    _dragging = false;
    if (_mode == PLAYER && seekRect().contains(x, y) && AudioSys.durationSec() > 0) {
      _seekDrag = true;
      UIRect sr = seekRect();
      _seekTarget = (uint32_t)UIDraw::sliderHit(sr, x, 0, 0, (int)AudioSys.durationSec());
    }
  } else if (pressed && _wasPressed) {
    if (_seekDrag) {
      UIRect sr = seekRect();
      _seekTarget = (uint32_t)UIDraw::sliderHit(sr, x, 0, 0, (int)AudioSys.durationSec());
    } else {
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
      // live volume drag in player view
      if (_mode == PLAYER) {
        UIRect vr = volRect();
        if (vr.contains(_pressX, _pressY)) {
          Settings.volume = (uint8_t)UIDraw::sliderHit(vr, x, Settings.volume, 0, 100);
          AudioSys.setVolume(Settings.volume);
        }
      }
    }
  } else if (!pressed && _wasPressed) {
    if (_seekDrag) {
      AudioSys.seekTo(_seekTarget);   // commit on release
      _seekDrag = false;
    } else if (!_dragging) tap = true;
    if (_mode == PLAYER && volRect().contains(_pressX, _pressY)) Settings.save();
  }
  _wasPressed = pressed;
  if (!tap) return;
  x = _pressX; y = _pressY;

  if (_mode == LIST) {
    UIRect c = contentArea();
    int top = c.y + 24;
    if (_resumePath[0]) {
      UIRect rr = {c.x + 4, top, c.w - 8, 38};
      if (rr.contains(x, y)) {
        // playlist = the resumed file's directory
        char* slash = strrchr(_resumePath, '/');
        if (slash && slash != _resumePath) {
          size_t n = slash - _resumePath;
          memcpy(_dir, _resumePath, n);
          _dir[n] = 0;
        } else strcpy(_dir, "/");
        scanDir();
        const char* base = slash ? slash + 1 : _resumePath;
        _current = -1;
        for (int i = 0; i < _count; i++)
          if (!_tracks[i].isDir && !strcmp(_tracks[i].name, base)) { _current = i; break; }
        strlcpy(_curPath, _resumePath, sizeof(_curPath));
        AudioSys.playFile(_resumePath, _resumePos);
        _mode = PLAYER;
        return;
      }
      top += 44;
    }
    if (y < top) return;
    int idx = (y - top + _scroll) / ROW_H;
    if (idx < 0 || idx >= _count) return;
    if (_tracks[idx].isDir) enterDir(_tracks[idx].name);
    else playIndex(idx);
    return;
  }

  // PLAYER
  if (btnPlay().contains(x, y)) {
    if (AudioSys.isActive()) AudioSys.togglePause();
    else if (_current >= 0) playIndex(_current);
  } else if (btnPrev().contains(x, y)) {
    nextTrack(-1);
  } else if (btnNext().contains(x, y)) {
    nextTrack(+1);
  } else if (Settings.visualizerOn && y > seekRect().y + seekRect().h && y < volRect().y) {
    enterVis();   // tap the spectrum area for the fullscreen shockwave
  }
}

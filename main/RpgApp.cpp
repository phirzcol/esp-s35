#include "RpgApp.h"
#include "DisplayHAL.h"
#include "TouchHAL.h"
#include "SDCardHAL.h"
#include "SystemTask.h"
#include <SD_MMC.h>
#include <math.h>

// Game data/logic ported from H:\game\game (level map, tiles, enemies, player)
#include "src/rpg/level1.h"
#include "src/rpg/enemy1.h"
#include "src/rpg/player_base.h"

RpgApp Rpg;

// ---- world constants (original design) ----
static const int VIEW_W = 320, VIEW_H = 240;      // playfield on top of the canvas
static const int MAP_W = 30 * 32, MAP_H = 24 * 32;
static const int WIZ_X = 144, WIZ_Y = 104;        // wizard screen anchor
static const int ENEMY_N = 2;

static PlayerCharacter s_party;
static LevelEnemy s_monsters[ENEMY_N];
static bool s_spawned = false;

// ---- control layout (panel below the 320x240 world) ----
static const int JOY_CX = 80, JOY_CY = 396, JOY_R = 54, JOY_KNOB = 24;
static const int BTN_A_X = 240, BTN_A_Y = 362, BTN_R = 28;
static const int BTN_B_X = 276, BTN_B_Y = 438;
static const float WALK_SPEED = 3.0f;   // px per frame at full stick
static const float RUN_MULT = 1.8f;     // B held

// ---- collision (original corner-check against tile IDs >= 5) ----
static bool terrainBlocked(int pWorldX, int pWorldY) {
  int cx[4] = {pWorldX + 1, pWorldX + 30, pWorldX + 1, pWorldX + 30};
  int cy[4] = {pWorldY + 1, pWorldY + 1, pWorldY + 30, pWorldY + 30};
  for (int c = 0; c < 4; c++) {
    int col = cx[c] / 32, row = cy[c] / 32;
    if (col >= 0 && col < 30 && row >= 0 && row < 24) {
      if ((pgm_read_byte(&(level1_map[row][col])) & 0x1F) >= 5) return true;
    }
  }
  return false;
}

void RpgApp::newGame() {
  memcpy_P(&s_party, &archmage_profile, sizeof(PlayerCharacter));
  _camX = 224; _camY = 144;
  _lastCamX = _camX; _lastCamY = _camY;
  _spriteId = 2;
  s_monsters[0].spawn(10, 11, enemy_slime_frame1);
  s_monsters[1].spawn(17, 13, enemy_skeleton_frame1);
  s_spawned = true;
}

// SD save replaces the original RAMStateBuffer (survives power-off)
bool RpgApp::saveGame() {
  if (!SDCard.mount()) return false;
  File f = SD_MMC.open("/rpg_save.bin", FILE_WRITE);
  if (!f) return false;
  f.write((uint8_t*)&_camX, sizeof(_camX));
  f.write((uint8_t*)&_camY, sizeof(_camY));
  f.write((uint8_t*)&s_party.currentHP, sizeof(s_party.currentHP));
  f.write((uint8_t*)&s_party.currentMP, sizeof(s_party.currentMP));
  f.write((uint8_t*)&s_party.playerLevel, sizeof(s_party.playerLevel));
  f.write((uint8_t*)&s_party.currentEXP, sizeof(s_party.currentEXP));
  f.close();
  return true;
}

bool RpgApp::loadGame() {
  if (!SDCard.mount()) return false;
  File f = SD_MMC.open("/rpg_save.bin");
  if (!f) return false;
  bool ok = f.read((uint8_t*)&_camX, sizeof(_camX)) == sizeof(_camX);
  ok &= f.read((uint8_t*)&_camY, sizeof(_camY)) == sizeof(_camY);
  ok &= f.read((uint8_t*)&s_party.currentHP, sizeof(s_party.currentHP)) == sizeof(s_party.currentHP);
  ok &= f.read((uint8_t*)&s_party.currentMP, sizeof(s_party.currentMP)) == sizeof(s_party.currentMP);
  ok &= f.read((uint8_t*)&s_party.playerLevel, sizeof(s_party.playerLevel)) == sizeof(s_party.playerLevel);
  ok &= f.read((uint8_t*)&s_party.currentEXP, sizeof(s_party.currentEXP)) == sizeof(s_party.currentEXP);
  f.close();
  _lastCamX = _camX;
  _lastCamY = _camY;
  return ok;
}

void RpgApp::onEnter() {
  if (!s_spawned) newGame();
  _st = EXPLORE;
  _fingerDown = false;
  Gfx.canvas().setSwapBytes(true);   // asset arrays are byte-pair RGB565
}

void RpgApp::onExit() {
  Gfx.canvas().setSwapBytes(false);
  saveGame();
}

bool RpgApp::onBack() {
  if (_st == STATUS) { _st = PAUSE; return true; }
  if (_st == PAUSE) { _st = EXPLORE; return true; }
  if (_st == EXPLORE) { _st = PAUSE; return true; }   // Back = pause first
  return false;
}

// ---- movement: original per-axis sweeping collision ----
void RpgApp::moveCamera(int targetCamX, int targetCamY) {
  int stepX = (targetCamX > _camX) ? 1 : -1;
  while (_camX != targetCamX) {
    int test = _camX + stepX;
    int pX = test + WIZ_X, pY = _camY + WIZ_Y;
    bool blocked = terrainBlocked(pX, pY);
    for (int i = 0; i < ENEMY_N && !blocked; i++)
      if (checkHitboxCollision(pX, pY, s_monsters[i].mapX, s_monsters[i].mapY, 0)) blocked = true;
    if (blocked) break;
    _camX = test;
  }
  int stepY = (targetCamY > _camY) ? 1 : -1;
  while (_camY != targetCamY) {
    int test = _camY + stepY;
    int pX = _camX + WIZ_X, pY = test + WIZ_Y;
    bool blocked = terrainBlocked(pX, pY);
    for (int i = 0; i < ENEMY_N && !blocked; i++)
      if (checkHitboxCollision(pX, pY, s_monsters[i].mapX, s_monsters[i].mapY, 0)) blocked = true;
    if (blocked) break;
    _camY = test;
  }

  // facing + walk detection (original thresholds)
  int dX = _camX - _lastCamX, dY = _camY - _lastCamY;
  if (abs(dX) > 1 || abs(dY) > 1) {
    _walking = true;
    if (abs(dX) > abs(dY)) _spriteId = (dX > 0) ? 4 : 3;
    else                   _spriteId = (dY > 0) ? 2 : 1;
  }
  _lastCamX = _camX;
  _lastCamY = _camY;

  _camX = constrain(_camX, 0, MAP_W - VIEW_W);
  _camY = constrain(_camY, 0, MAP_H - VIEW_H);
}

// Joystick + buttons via multi-touch so stick and button work simultaneously
void RpgApp::readPad() {
  MultiTouch mt = Touch.readMulti();
  _joyActive = false;
  _btnA = _btnB = false;
  float jx = 0, jy = 0;
  for (int i = 0; i < mt.count; i++) {
    int dxA = mt.x[i] - BTN_A_X, dyA = mt.y[i] - BTN_A_Y;
    int dxB = mt.x[i] - BTN_B_X, dyB = mt.y[i] - BTN_B_Y;
    if (dxA * dxA + dyA * dyA < (BTN_R + 8) * (BTN_R + 8)) { _btnA = true; continue; }
    if (dxB * dxB + dyB * dyB < (BTN_R + 8) * (BTN_R + 8)) { _btnB = true; continue; }
    int dx = mt.x[i] - JOY_CX, dy = mt.y[i] - JOY_CY;
    if (dx * dx + dy * dy < (JOY_R + 40) * (JOY_R + 40)) {
      float len = sqrtf((float)(dx * dx + dy * dy));
      if (len > JOY_R) { dx = (int)(dx * JOY_R / len); dy = (int)(dy * JOY_R / len); }
      jx = (float)dx / JOY_R;
      jy = (float)dy / JOY_R;
      _joyActive = true;
    }
  }
  _joyDX = jx;
  _joyDY = jy;
}

void RpgApp::update(uint32_t) {
  if (_st != EXPLORE) return;

  readPad();
  _walking = false;
  float mag = sqrtf(_joyDX * _joyDX + _joyDY * _joyDY);
  if (_joyActive && mag > 0.15f) {   // deadzone; release snaps to center = stop
    float speed = WALK_SPEED * (_btnB ? RUN_MULT : 1.0f);
    int tx = _camX + (int)lroundf(_joyDX * speed);
    int ty = _camY + (int)lroundf(_joyDY * speed);
    moveCamera(tx, ty);
    _walking = true;
    // facing straight from the stick (per-frame deltas are too small for the
    // original >1px threshold at walk speed)
    if (fabsf(_joyDX) > fabsf(_joyDY)) _spriteId = (_joyDX > 0) ? 4 : 3;
    else                               _spriteId = (_joyDY > 0) ? 2 : 1;
  }

  int pX = _camX + WIZ_X, pY = _camY + WIZ_Y;
  for (int i = 0; i < ENEMY_N; i++)
    s_monsters[i].updatePatrol(s_monsters, ENEMY_N, i, pX, pY);
  if (_walking) _animFrame++;
  else _animFrame = 0;
}

// ---- rendering (original tile blitter, retargeted to our canvas) ----
void RpgApp::drawWorld(lgfx::LGFX_Sprite& g) {
  g.fillRect(0, 0, VIEW_W, VIEW_H, TFT_BLACK);
  int startCol = _camX / 32, startRow = _camY / 32;
  int offX = -(_camX % 32), offY = -(_camY % 32);

  for (int col = 0; col < 11; col++) {
    int tCol = startCol + col;
    if (tCol >= 30) break;
    int drawX = offX + col * 32;
    for (int row = 0; row < 9; row++) {
      int tRow = startRow + row;
      if (tRow >= 24) break;
      int drawY = offY + row * 32;
      if (drawY >= VIEW_H) break;

      uint8_t raw = pgm_read_byte(&(level1_map[tRow][tCol]));
      uint8_t id = raw & 0x1F;
      bool flipX = raw & 0x80, flipY = raw & 0x40;
      const uint8_t* tile = level_sprites[id];

      if (!flipX && !flipY) {
        g.pushImage(drawX, drawY, 32, 32, (const uint16_t*)tile);
      } else {
        // same color pipeline as pushImage (mirror via negative zoom), so
        // flipped shore tiles match the rest of the pond
        g.pushImageRotateZoom(drawX + 16.0f, drawY + 16.0f, 15.5f, 15.5f, 0.0f,
                              flipX ? -1.0f : 1.0f, flipY ? -1.0f : 1.0f,
                              32, 32, (const uint16_t*)tile);
      }
    }
  }

  // enemies
  for (int i = 0; i < ENEMY_N; i++) {
    int ex = s_monsters[i].mapX - _camX, ey = s_monsters[i].mapY - _camY;
    if (ex >= -32 && ex <= VIEW_W && ey >= -32 && ey <= VIEW_H)
      g.pushImage(ex, ey, 32, 32, (const uint16_t*)s_monsters[i].sprite, (uint16_t)0xFFFF);
  }

  // wizard with procedural walk bob/sway (original)
  int bobY = 0, swayX = 0;
  if (_walking) {
    if ((_animFrame / 6) % 2 == 0) bobY = 2;
    swayX = ((_animFrame / 12) % 2 == 0) ? -1 : 1;
  }
  g.pushImage(WIZ_X + swayX, WIZ_Y + bobY, 32, 32,
              (const uint16_t*)level_sprites[_spriteId], (uint16_t)0xFFFF);
}

void RpgApp::drawPanel(lgfx::LGFX_Sprite& g) {
  int W = g.width(), H = g.height();
  int py = VIEW_H;
  g.fillRect(0, py, W, H - py, Theme::BG);
  g.drawFastHLine(0, py, W, Theme::ACCENT);

  // Compact party strip
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::ACCENT);
  g.drawString(s_party.charName, 10, py + 8);
  g.setTextColor(Theme::TEXT_DIM);
  char lv[20];
  snprintf(lv, sizeof(lv), "Lv %u", s_party.playerLevel);
  g.drawString(lv, 10, py + 26);

  int bx = 78, bw = 152;
  int hpW = s_party.maxHP > 0 ? (int)((int32_t)bw * s_party.currentHP / s_party.maxHP) : 0;
  int mpW = s_party.maxMP > 0 ? (int)((int32_t)bw * s_party.currentMP / s_party.maxMP) : 0;
  g.fillRoundRect(bx, py + 8, bw, 12, 3, Theme::PANEL);
  g.fillRoundRect(bx, py + 8, hpW, 12, 3, Theme::BAD);
  g.fillRoundRect(bx, py + 26, bw, 12, 3, Theme::PANEL);
  g.fillRoundRect(bx, py + 26, mpW, 12, 3, Theme::ACCENT);

  UIRect mb = {W - 78, py + 6, 70, 34};
  UIDraw::button(g, mb, "Menu");

  // Ball joystick: base ring + spring-back knob
  g.fillCircle(JOY_CX, JOY_CY, JOY_R + 4, Theme::PANEL);
  g.drawCircle(JOY_CX, JOY_CY, JOY_R + 4, Theme::PANEL_HI);
  g.drawCircle(JOY_CX, JOY_CY, JOY_R / 2, Theme::PANEL_HI);
  int kx = JOY_CX + (int)(_joyDX * JOY_R);
  int ky = JOY_CY + (int)(_joyDY * JOY_R);
  g.fillCircle(kx, ky, JOY_KNOB, _joyActive ? Theme::ACCENT : Theme::PANEL_HI);
  g.fillCircle(kx - 6, ky - 6, 6, Theme::TEXT_DIM);   // highlight = ball look

  // A / B buttons
  g.fillCircle(BTN_A_X, BTN_A_Y, BTN_R, _btnA ? Theme::ACCENT : Theme::PANEL);
  g.drawCircle(BTN_A_X, BTN_A_Y, BTN_R, Theme::PANEL_HI);
  g.fillCircle(BTN_B_X, BTN_B_Y, BTN_R, _btnB ? Theme::GOOD : Theme::PANEL);
  g.drawCircle(BTN_B_X, BTN_B_Y, BTN_R, Theme::PANEL_HI);
  g.setTextDatum(lgfx::middle_center);
  g.setTextColor(Theme::TEXT);
  g.drawString("A", BTN_A_X, BTN_A_Y);
  g.drawString("B", BTN_B_X, BTN_B_Y);
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString("action", BTN_A_X, BTN_A_Y + BTN_R + 4);
  g.drawString("run", BTN_B_X, BTN_B_Y + BTN_R + 4);
}

void RpgApp::drawPause(lgfx::LGFX_Sprite& g) {
  int W = g.width(), H = g.height();
  g.fillRect(30, 60, W - 60, H - 200, Theme::PANEL);
  g.drawRect(30, 60, W - 60, H - 200, Theme::ACCENT);
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::ACCENT);
  g.drawString("- PAUSED -", W / 2, 74);
  const char* items[4] = {"Party status", "Save game", "Load game", "Continue"};
  for (int i = 0; i < 4; i++) {
    UIRect r = {50, 104 + i * 46, W - 100, 38};
    UIDraw::button(g, r, items[i]);
  }
}

void RpgApp::drawStatus(lgfx::LGFX_Sprite& g) {
  int W = g.width();
  g.fillScreen(Theme::BG);
  g.setTextDatum(lgfx::top_center);
  g.setTextColor(Theme::ACCENT);
  g.setTextSize(2);
  g.drawString(s_party.charName, W / 2, 16);
  g.setTextSize(1);

  g.setTextDatum(lgfx::top_left);
  int y = 56;
  g.setTextColor(Theme::TEXT_DIM);
  // bio, word-wrapped
  const char* p = s_party.bioFlavorText;
  while (*p) {
    char line[52];
    int o = 0, lastSp = -1;
    while (*p && o < 48) {
      if (*p == ' ') lastSp = o;
      line[o++] = *p++;
    }
    if (*p && lastSp > 0) { p -= (o - lastSp - 1); o = lastSp; }
    line[o] = 0;
    g.drawString(line, 16, y);
    y += 16;
  }
  y += 10;
  g.setTextColor(Theme::TEXT);
  char s[40];
  snprintf(s, sizeof(s), "STR %u  AGI %u  VIT %u", s_party.strength, s_party.agility, s_party.vitality);
  g.drawString(s, 16, y); y += 20;
  snprintf(s, sizeof(s), "INT %u  LCK %u  Block %u", s_party.intelligence, s_party.luck, s_party.blockRating);
  g.drawString(s, 16, y); y += 28;
  g.setTextColor(Theme::WARN);
  g.drawString("Abilities:", 16, y); y += 18;
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString(s_party.spell1, 28, y); y += 16;
  g.drawString(s_party.spell2, 28, y); y += 16;
  g.drawString(s_party.spell3, 28, y); y += 16;
  g.setTextDatum(lgfx::bottom_center);
  g.drawString("Back returns to menu", W / 2, g.height() - 12);
}

void RpgApp::draw(lgfx::LGFX_Sprite& g) {
  if (_st == STATUS) { drawStatus(g); return; }
  drawWorld(g);
  drawPanel(g);
  if (_st == PAUSE) drawPause(g);
}

void RpgApp::handleTouch(int x, int y, bool pressed) {
  bool tap = pressed && !_wasPressed;
  _wasPressed = pressed;

  if (_st == PAUSE) {
    if (!tap) return;
    int W = Gfx.width();
    for (int i = 0; i < 4; i++) {
      UIRect r = {50, 104 + i * 46, W - 100, 38};
      if (!r.contains(x, y)) continue;
      switch (i) {
        case 0: _st = STATUS; break;
        case 1: Notify.post(saveGame() ? "Game saved" : "Save failed"); break;
        case 2: Notify.post(loadGame() ? "Game loaded" : "No save found"); _st = EXPLORE; break;
        case 3: _st = EXPLORE; break;
      }
      return;
    }
    return;
  }
  if (_st == STATUS) {
    if (tap) _st = PAUSE;
    return;
  }

  // EXPLORE: movement/buttons handled by readPad() in update(); here only Menu
  int W = Gfx.width();
  UIRect mb = {W - 78, VIEW_H + 6, 70, 34};
  if (tap && mb.contains(x, y)) { _st = PAUSE; return; }
}

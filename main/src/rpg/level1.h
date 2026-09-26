#ifndef LEVEL1_H
#define LEVEL1_H
#define SPRITE_LIST_EXISTS

#include <Arduino.h>

// ============================================================================
//               RPG RETRO ENGINE TILE TRANSFORM BITMASK LEGEND
// ============================================================================
// Each cell in the level1_map matrix uses a single 8-bit byte. 
// The byte is split to store BOTH the sprite graphics ID and its spatial transform:
//
//   [Bit 7] [Bit 6] [Bit 5]   [Bit 4] [Bit 3] [Bit 2] [Bit 1] [Bit 0]
//   |-- FLIP X ----|-- FLIP Y ----|-- ROTATE 90 -|------------ GRAPHIC ASSET ID --------------|
//
// --- THE LOWER 5 BITS: GRAPHIC ASSET ID (0 to 31) ---
//   ID 0: Grass Tile
//   ID 1: Wizard Back
//   ID 2: Wizard Front
//   ID 3: Wizard Left
//   ID 4: Wizard Right
//   ID 5: Deep Water Center Core (watersolid)
//   ID 6: Shoreline Edge Straight (waterside)
//   ID 7: Shoreline Inner Corner  (watercorner)
//
// --- THE UPPER 3 BITS: TRANSFORM TOGGLES ---
//   Bit 5 (0x20) -> Rotate Tile 90 Degrees Clockwise
//   Bit 6 (0x40) -> Flip Tile Vertically (Mirror Y Axis)
//   Bit 7 (0x80) -> Flip Tile Horizontally (Mirror X Axis)
// ============================================================================

// 1. INGEST THE INDIVIDUAL SPRITE RAW GRAPHIC TILES USED FOR THIS LEVEL
#include "floorn.h" // Grass Asset   (ID: 0) -> my_sprite2_32x32
#include "backn.h"  // Wizard Back   (ID: 1) -> my_sprite3_32x32
#include "frontn.h" // Wizard Front  (ID: 2) -> my_sprite4_32x32
#include "leftn.h"  // Wizard Left   (ID: 3) -> my_sprite5_32x32
#include "rightn.h" // Wizard Right  (ID: 4) -> my_sprite6_32x32

// INGEST YOUR SEPARATE INDEPENDENT WATER FILES
#include "watersolid.h"   // ID 5 -> water_solid
#include "waterside.h"    // ID 6 -> water_side_edge
#include "watercorner.h"  // ID 7 -> water_corner
#include "watertop.h"     // <--- ADD THIS: ID 8 -> watertop

// 2. THE TILE REGISTER INDEX MAP LEGEND
const uint8_t* level_sprites[] = {
    my_sprite2_32x32, // [0] Grass Tile
    my_sprite3_32x32, // [1] Wizard Back
    my_sprite4_32x32, // [2] Wizard Front
    my_sprite5_32x32, // [3] Wizard Left
    my_sprite6_32x32, // [4] Wizard Right
    watersolid,       // [5] Deep Water Solid
    waterside,        // [6] Straight Side Shoreline Edge
    watercorner,      // [7] Shoreline Corner
    watertop          // [8] <--- REGISTER THIS: Straight Top Shoreline Edge
};

// 3. THE 3X3 SCREEN TILEMAP GRID MAP MATRIX (30 columns wide by 24 rows deep)
// Uses base IDs and Bitmask math values to build a custom test pond!
const uint8_t level1_map[24][30] PROGMEM = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  // Row 11-13: A pristine rectangular test pond using your new top/bottom shore strategy!
  // 7 = Top-Left Corner, 8 = Top Shore, 135 (7+128 FlipX) = Top-Right Corner
  {0,0,0,0,0,0,0,0,0,0,0,0,7,  8,  8,  135,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  // 6 = Left Shore, 5 = Deep Water Core, 134 (6+128 FlipX) = Right Shore
  {0,0,0,0,0,0,0,0,0,0,0,0,6,  5,  5,  134,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  // 71 (7+64 FlipY) = Bottom-Left Corner, 72 (8+64 FlipY) = Bottom Shore, 199 (7+128+64) = Bottom-Right Corner
  {0,0,0,0,0,0,0,0,0,0,0,0,71, 72, 72, 199,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}  
};

// --- SOLID MATRIX ENVIRONMENTAL WALKABILITY FILTER ---
// Takes real-world map pixel spaces, isolates the cell, strips the transform bitmask, 
// and returns true if the tile is open ground.
inline bool isTileWalkable(int worldPixelX, int worldPixelY) {
    int col = worldPixelX / 32;
    int row = worldPixelY / 32;

    // Hard boundary map containment fences
    if (col < 0 || col >= 30 || row < 0 || row >= 24) return false;

    // 1. Pull raw byte value including transformations
    uint8_t rawTileValue = pgm_read_byte(&(level1_map[row][col]));

    // 2. APPLY THE BITMASK: Strip the top 3 transform bits to isolate the pure Asset ID
    uint8_t cleanAssetID = rawTileValue & 0x1F;

     // IDs 0-4 are passable floors. IDs 5 through 8 are un-swimmable water surfaces!
    if (cleanAssetID >= 5) {
        return false; 
    }
    return true;  
}

// UNIVERSAL 2D BOX OVERLAP DETECTOR
inline bool checkHitboxCollision(int x1, int y1, int x2, int y2, int padding) {
    if (x1 + 32 - padding <= x2 + padding) return false; 
    if (x1 + padding >= x2 + 32 - padding) return false; 
    if (y1 + 32 - padding <= y2 + padding) return false; 
    if (y1 + padding >= y2 + 32 - padding) return false; 
    return true; 
}

#endif

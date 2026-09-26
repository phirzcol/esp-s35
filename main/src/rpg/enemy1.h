#ifndef ENEMY1_H
#define ENEMY1_H

#include <Arduino.h>

// 1. INGEST EACH MONSTER FRAME AS A SEPARATE LIGHTWEIGHT FILE
#include "slime.h"      // Pulls in enemy_slime_frame1
#include "slime2.h"     // Pulls in enemy_slime_frame2
#include "skeleton.h"   // Pulls in enemy_skeleton_frame1 (Frame 1)
#include "skeleton2.h"  // Pulls in skeleton2             (Frame 2)

// Link external collision check from level1.h dynamically
bool checkHitboxCollision(int x1, int y1, int x2, int y2, int padding);

// 2. REUSABLE ENEMY BEHAVIOR CLASS
class LevelEnemy {
public:
    int mapX, mapY;         // Position on the 3x3 screen map grid (in pixels)
    int homeX, homeY;       // Fixed patrol anchor point
    int targetX, targetY;   // Active target path square
    int moveTimer;
    int animTimer;          // Isolated clock counter specifically for frame swapping
    const uint8_t* sprite;  // Direct tracking pointer to its bitmap data
    const uint8_t* baseID;  // Anchor reference to remember who I am (Slime vs Skeleton)

    void spawn(int gridX, int gridY, const uint8_t* enemySprite) {
        homeX = gridX * 32;
        homeY = gridY * 32;
        mapX = homeX;
        mapY = homeY;
        targetX = homeX;
        targetY = homeY;
        moveTimer = random(0, 60);
        animTimer = 0;
        sprite = enemySprite;
        baseID = enemySprite; // Lock in starting graphic identity permanently
    }

    // RANDOM WANDERING PATROL ENGINE WITH SAFE AI GENERATION GATES
    void updatePatrol(LevelEnemy* pool, int totalEnemies, int myIndex, int playerWorldX, int playerWorldY) {
        if (mapX == targetX && mapY == targetY) {
            moveTimer++;
            animTimer = 0; 
            sprite = baseID; // Maintain core graphic identity
            
            if (moveTimer > 90) { 
                moveTimer = 0;
                
                // --- PROACTIVE PATH ROUTE PICKER ---
                // Keep rolling random directions until we find a target tile that is completely open!
                bool validPathFound = false;
                int safetyAttempts = 0;
                
                int tentativeTargetX = mapX;
                int tentativeTargetY = mapY;

                while (!validPathFound && safetyAttempts < 10) {
                    safetyAttempts++;
                    
                    int gridOffsetX = random(-2, 3) * 32;
                    int gridOffsetY = random(-2, 3) * 32;
                    
                    if (abs(gridOffsetX) >= abs(gridOffsetY)) {
                        gridOffsetY = 0; 
                    } else {
                        gridOffsetX = 0; 
                    }
                    
                    tentativeTargetX = homeX + gridOffsetX;
                    tentativeTargetY = homeY + gridOffsetY;

                    // Clamping to level matrix boundaries
                    if (tentativeTargetX < 0) tentativeTargetX = 0;
                    if (tentativeTargetX > (30 * 32) - 32) tentativeTargetX = (30 * 32) - 32;
                    if (tentativeTargetY < 0) tentativeTargetY = 0;
                    if (tentativeTargetY > (24 * 32) - 32) tentativeTargetY = (24 * 32) - 32;

                    // Validate Tile Passability via level1_map
                    extern const uint8_t level1_map[24][30] PROGMEM;
                    int tCol = tentativeTargetX / 32;
                    int tRow = tentativeTargetY / 32;
                    uint8_t rawTile = pgm_read_byte(&(level1_map[tRow][tCol]));
                    uint8_t assetID = rawTile & 0x1F;

                    // If the rolled tile lands on water (ID >= 5) or on the player, reject it and re-roll!
                    if (assetID < 5 && !checkHitboxCollision(tentativeTargetX, tentativeTargetY, playerWorldX, playerWorldY, 0)) {
                        validPathFound = true;
                    }
                }

                // Lock in the verified safe path destination
                targetX = tentativeTargetX;
                targetY = tentativeTargetY;
            }
        } else {
            // 1. STEP ENGINE (Strict Cardinal Separation)
            int nextMapX = mapX;
            int nextMapY = mapY;

            if (mapX != targetX) {
                if (mapX < targetX) nextMapX += 1;
                else                nextMapX -= 1;
            } else if (mapY != targetY) {
                if (mapY < targetY) nextMapY += 1;
                else                nextMapY -= 1;
            }

            // 2. DETECT INTERSECTIONS USING ABSOLUTE BOUNDING SHELLS (0 Padding Buffer)
            bool isBlocked = false;

            if (checkHitboxCollision(nextMapX, nextMapY, playerWorldX, playerWorldY, 0)) {
                isBlocked = true;
            }

            for (int i = 0; i < totalEnemies; i++) {
                if (i == myIndex) continue; 
                if (checkHitboxCollision(nextMapX, nextMapY, pool[i].mapX, pool[i].mapY, 0)) {
                    isBlocked = true;
                    break;
                }
            }

            // --- BULLETPROOF PROACTIVE OUTSIDE BUMPER GUARD ---
            extern const uint8_t level1_map[24][30] PROGMEM;
            // Expand check points 1 pixel OUTSIDE the 32x32 bounding shell to catch sharp visual corners early
            int cornersX[4] = { nextMapX - 1, nextMapX + 32, nextMapX - 1, nextMapX + 32 };
            int cornersY[4] = { nextMapY - 1, nextMapY - 1, nextMapY + 32, nextMapY + 32 };

            for (int c = 0; c < 4; c++) {
                int tCol = cornersX[c] / 32;
                int tRow = cornersY[c] / 32;
                if (tCol >= 0 && tCol < 30 && tRow >= 0 && tRow < 24) {
                    uint8_t rawTile = pgm_read_byte(&(level1_map[tRow][tCol]));
                    uint8_t assetID = rawTile & 0x1F;
                    if (assetID >= 5) {
                        isBlocked = true;
                        break;
                    }
                }
            }

            // 3. EXECUTE POSITION COORDS UPDATE OR FORCE CARDINAL BOUNCE REFLEX
            if (!isBlocked) {
                mapX = nextMapX;
                mapY = nextMapY;

                animTimer++; 
                bool useFrame1 = ((animTimer / 12) % 2 == 0);
                if (baseID == enemy_skeleton_frame1) {
                    sprite = useFrame1 ? enemy_skeleton_frame1 : skeleton2;
                } else {
                    sprite = useFrame1 ? enemy_slime_frame1 : enemy_slime_frame2;
                }
            } else {
                // --- AI CARDINAL REFLEX DETOUR ENGAGED ---
                int dirX = nextMapX - mapX;
                int dirY = nextMapY - mapY;

                mapX = (mapX / 32) * 32;
                mapY = (mapY / 32) * 32;

                int bounceX = (dirX != 0) ? (-dirX * 32) : 0;
                int bounceY = (dirY != 0) ? (-dirY * 32) : 0;

                targetX = mapX + bounceX;
                targetY = mapY + bounceY;

                if (targetX < 0) targetX = 0;
                if (targetX > (30 * 32) - 32) targetX = (30 * 32) - 32;
                if (targetY < 0) targetY = 0;
                if (targetY > (24 * 32) - 32) targetY = (24 * 32) - 32;

                moveTimer = 0; // Force immediate walk execution away from wall
                
                Serial.print("[AI REFLEX] Monster "); Serial.print(myIndex);
                Serial.print(" BUMPED AND TURNED. New Target: ("); 
                Serial.print(targetX); Serial.print(", "); Serial.print(targetY); Serial.println(")");
            } 
        } 
    } 
}; 

#endif

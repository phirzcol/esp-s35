#ifndef PLAYER_BASE_H
#define PLAYER_BASE_H

#include <Arduino.h>

// UNIVERSAL STRUCT SPECIFICATION FOR PLAYER PARTY MEMBERS
struct PlayerCharacter {
    const char* charName;      // Display Name (e.g., "Archmage")
    const char* bioFlavorText; // The unique narrative summary text from your asset panel
    
    // --- THE CORE ATTRIBUTES (FROM YOUR COMPANION STATUS UI) ---
    uint8_t strength;          // STR (Physical muscle and unarmed scaling)
    uint8_t agility;           // AGI (Speed, evasion, initiative)
    uint8_t vitality;          // VIT (Base physical health mitigation multiplier)
    uint8_t intelligence;      // INT (Spell power scaling resource pool factor)
    uint8_t luck;              // LCK (Critical hits and rare loot table drops)

    // --- GAMEPLAY COMBAT STATS REGISTER ---
    int16_t currentHP;
    int16_t maxHP;
    int16_t currentMP;
    int16_t maxMP;
    uint8_t playerLevel;
    uint32_t currentEXP;
    
    // --- SKILLS & ABILITIES INVENTORY REGISTER ---
    const char* basicAttack;       // Set to: "Unarmed Attack"
    const char* magicBasicAttack;  // Set to: "Magic Unarmed Attack"
    uint8_t blockRating;           // Set to: 3
    
    // THE 3 SPECIFIC ARCANE ABILITIES
    const char* spell1;            // "Spell Blast"
    const char* spell2;            // "Overcharge"
    const char* spell3;            // "Meteor"
};

// --- DATA-DRIVEN ARCHMAGE MASTER BLUEPRINT PROFILE ---
const PlayerCharacter archmage_profile PROGMEM = {
    "Archmage",
    "A master of the fundamental forces, she weaves devastating spells with effortless "
    "grace. Her knowledge is vast, her power unmatched, and her curiosity endless.",
    
    // ATTRIBUTES (DIRECT FROM YOUR BLUE SPRITE CARD SHEET)
    3,   // STR
    8,   // AGI
    7,   // VIT
    20,  // INT
    10,  // LCK

    // STARTING EXPEDITION METRICS (Perfect for early-game balance testing)
    85,  // Current HP
    85,  // Max HP
    180, // Current MP
    180, // Max MP
    1,   // Player Level
    0,   // Starting Experience Points (EXP)

    // FIXED COMBAT ABILITY STRINGS
    "Unarmed Attack",
    "Magic Unarmed Attack",
    3,   // Block Rating 3

    // UNIQUE SKILL/SKILLCASTING SELECTIONS
    "Spell Blast",
    "Overcharge",
    "Meteor"
};

#endif

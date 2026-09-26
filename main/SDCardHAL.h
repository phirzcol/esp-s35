#pragma once
#include <Arduino.h>

// Lazy-mount SDMMC card (4-bit bus, falls back to 1-bit). Mount on first use.
class SDCardHAL {
public:
  bool mount();                 // idempotent; true if card ready
  void unmount();
  bool mounted() const { return _mounted; }
  uint64_t totalMB() const;
  uint64_t usedMB() const;
private:
  bool _mounted = false;
  bool _pinsSet = false;
};

extern SDCardHAL SDCard;

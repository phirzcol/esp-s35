#include "SDCardHAL.h"
#include "HW_Pins.h"
#include <SD_MMC.h>

SDCardHAL SDCard;

bool SDCardHAL::mount() {
  if (_mounted) return true;
  if (!_pinsSet) {
    if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3)) {
      Serial.println("SD: setPins failed");
      return false;
    }
    _pinsSet = true;
  }
  // 4-bit first, then 1-bit fallback
  if (!SD_MMC.begin("/sdcard", false)) {
    if (!SD_MMC.begin("/sdcard", true)) {
      Serial.println("SD: mount failed");
      return false;
    }
    Serial.println("SD: mounted (1-bit)");
  } else {
    Serial.println("SD: mounted (4-bit)");
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    return false;
  }
  _mounted = true;
  return true;
}

void SDCardHAL::unmount() {
  if (!_mounted) return;
  SD_MMC.end();
  _mounted = false;
}

uint64_t SDCardHAL::totalMB() const { return _mounted ? SD_MMC.totalBytes() / (1024ULL * 1024) : 0; }
uint64_t SDCardHAL::usedMB()  const { return _mounted ? SD_MMC.usedBytes()  / (1024ULL * 1024) : 0; }

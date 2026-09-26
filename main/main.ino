// ES3C35P Multi-App Framework — core build
// Board: Hosyond 3.5" ESP32-S3 Display (ES3C35P), ESP32-S3-WROOM-1-N16R8
// Arduino IDE settings: board "ESP32S3 Dev Module", Flash 16MB, PSRAM "OPI PSRAM",
// Partition scheme with large APP (e.g. "16M Flash (3MB APP/9.9MB FATFS)" or custom).
//
// Architecture:
//  - Core 1 (Arduino loop): touch input, app logic, LovyanGFX rendering into a
//    PSRAM back buffer, QSPI push to the ST77922 panel (double buffered).
//  - Core 0 (sysTask): battery sampling, WiFi state machine, NTP.
//  - Peripherals start OFF (amp disabled, radios down) until an app needs them.

#include <Arduino.h>
#include "HW_Pins.h"
#include "SettingsStore.h"
#include "DisplayHAL.h"
#include "TouchHAL.h"
#include "SystemTask.h"
#include "AppManager.h"
#include "LauncherApp.h"

static uint32_t s_lastFrame = 0;
static uint32_t s_lastActivity = 0;
static bool s_dimmed = false;
static const uint32_t FRAME_MS = 33;   // ~30 fps

void setup() {
  Serial.begin(115200);

  Settings.begin();

  // Vendor-verified bring-up order: display bus -> panel init -> backlight,
  // then touch (shared I2C), then background services. Audio/SD stay off.
  if (!Gfx.begin(Settings.rotation, Settings.brightness)) {
    Serial.println("FATAL: display init failed (PSRAM enabled?)");
  }
  if (!Touch.begin(Settings.rotation)) {
    Serial.println("WARN: touch controller not responding");
  } else {
    Touch.startTask();   // acquisition on core 0, UI reads snapshots
  }

  systemTaskStart();
  Apps.begin(&Launcher);

  s_lastActivity = millis();
  Serial.printf("Core ready. Heap %u KB, PSRAM %u KB free\n",
                ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
}

void loop() {
  uint32_t now = millis();
  uint32_t dt = now - s_lastFrame;
  if (dt < FRAME_MS) {
    delay(FRAME_MS - dt);
    now = millis();
    dt = now - s_lastFrame;
  }
  s_lastFrame = now;

  TouchPoint tp = Touch.read();
  if (tp.pressed) {
    s_lastActivity = now;
    if (s_dimmed) {
      // Wake: restore brightness, swallow this touch
      Gfx.setBrightness(Settings.brightness);
      s_dimmed = false;
      return;
    }
  }

  // Screensaver: dim backlight after idle timeout (apps may inhibit, e.g. visualizer)
  bool inhibit = Apps.current() && Apps.current()->inhibitScreensaver();
  if (inhibit) s_lastActivity = now;
  if (Settings.screensaverOn && !s_dimmed &&
      now - s_lastActivity > (uint32_t)Settings.screensaverSec * 1000) {
    Gfx.setBrightness(10);
    s_dimmed = true;
  }

  Apps.handleTouch(tp.x, tp.y, tp.pressed);
  Apps.tick(dt);
}

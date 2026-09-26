# ES3C35P Multi-App Framework

Custom firmware for the **Hosyond 3.5" ESP32-S3 Touchscreen Module (ES3C35P)** —
a launcher-based multi-application environment built on LovyanGFX with a shared
UI framework, dual-core task layout, and careful shared-bus management.

Sketch: [main/main.ino](main/main.ino) (Arduino IDE / arduino-cli)

---

## Hardware (verified against vendor demo code + lcdwiki)

| Subsystem | Part | Pins / Notes |
|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 | 240MHz dual core, 16MB flash, 8MB OPI PSRAM |
| Display | **ST77922** 320x480 IPS, **QSPI @ 80MHz** | CS=10, SCLK=12, D0=11, D1=13, D2=14, D3=9, TE=42, BL=41 (PWM **30kHz** — see Known Fixes) |
| Touch | **ST77922 in-cell** (not FT6336U!) | I2C addr **0x55**, SDA=38, SCL=39, RST=48, INT=47, 100kHz, 16-bit registers, multi-point |
| Audio | **ES8311 codec** (not MAX98357A) + PA | MCLK=17, BCLK=18, LRCK=21, DOUT=15, DIN=16, amp enable=IO1 (**active LOW**) |
| SD card | SDMMC 4-bit | CLK=5, CMD=4, D0=6, D1=7, D2=2, D3=3 |
| Battery | ADC on **IO8** (not 34) | reading x2 for divider; ETA6096 charger |
| RGB LED | WS2812 on IO40 | held off |
| Buttons | BOOT=IO0 | |

The vendor demo package (`source/3.5inch ESP32-S3 Display/`) is the authoritative
reference; the marketing/prompt specs (ST7796 SPI, FT6336U, MAX98357A) are wrong
for this board.

## Building

Arduino IDE board settings: **ESP32S3 Dev Module**, Flash Size **16MB**, PSRAM
**OPI PSRAM**, Partition **16M Flash (3MB APP/9.9MB FATFS)**.

Or arduino-cli:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" --libraries "E:\arduino\libraries" d:\s335\main
```

Required libraries (sketchbook `E:\arduino\libraries`):
- LovyanGFX (1.2.x) — all drawing
- ESP32-audioI2S (4.x, schreibfaul1) — MP3/AAC/FLAC decode; its ES8311 example driver is vendored into the sketch
- TJpg_Decoder — JPEG viewer
- arduinoFFT — radial visualizer

Vendored sources in the sketch:
- [main/es8311.cpp](main/es8311.cpp) — ES8311 codec driver (from ESP32-audioI2S examples, adapted to shared Wire bus)
- [main/src/nofrendo](main/src/nofrendo) — retro-go's nofrendo NES core (GPL-2, see COPYING; include paths adapted)

## Architecture

```
Core 1 (UI)                     Core 0 (hardware/services)
─────────────                   ──────────────────────────
loop() @ ~30fps                 touchTask   — in-cell touch poll (INT-gated I2C)
  touch snapshot read           sysTask     — battery ADC, WiFi state, NTP
  app update/draw               audioTask   — MP3 decode engine (ESP32-audioI2S)
  LovyanGFX canvas (PSRAM)      nesTask     — NES emulation @60fps (audio-clocked)
  QSPI push (ST77922)           rssFetch/wxFetch — transient HTTP tasks
```

- **Display pipeline**: all drawing goes to a full-screen `LGFX_Sprite` in PSRAM
  (back buffer); `present()` pushes it over QSPI (front buffer = panel GRAM).
  Flicker-free; landscape support exists via software transpose but rotation is
  disabled in settings (panel has no hardware row/col exchange).
- **Shared I2C bus** (touch + audio codec): all transactions wrapped in
  `i2cLock()/i2cUnlock()`; bus kept at 100kHz (touch controller requirement).
- **Audio gain staging**: decoder pinned at −3dB digital headroom (absorbs MP3
  overshoot on hot masters); user volume = ES8311 attenuation only, capped at
  0dB. Amp powers on only during playback with mute-before-switch de-pop.
- **Raw PCM path**: emulators use their own I2S1 channel (MP3 engine owns I2S0);
  blocking writes clock the NES at exactly 60fps. Pins hand back on exit.
- **App framework**: apps subclass `AppBase` (draw/handleTouch/update, optional
  fullscreen, screensaver inhibit, canvas-clear opt-out, in-app settings gear).
  `AppManager` provides the stack navigation + chrome (status bar: clock,
  notifications, BT/WiFi, battery; nav bar: Back/Home/gear).

## Apps

| App | Notes |
|---|---|
| Settings | Display (brightness, screensaver), WiFi (scan + keyboard password), Bluetooth toggle, Audio (volume, visualizer sens/amp), About |
| Files | SD browser (POSIX enumeration), text viewer, rename/delete, opens mp3/nes/images in their apps |
| Notes | /notes on SD, inline QWERTY editor, auto-save |
| MP3 Player | Directory browsing, transport, seek-drag, **resume** (30s checkpoints — audiobooks), 15-band spectrum + fullscreen radial shockwave visualizer (doubles as screensaver) |
| Pong / Breakout | Touch-drag paddles, fullscreen; Breakout has levels + persisted high score w/ name entry |
| NES | retro-go nofrendo core, ~70 mappers, APU audio, multi-touch controller overlay, ROMs from /roms (or Files app) |
| RSS | RSS+Atom, feeds persisted, headlines → article; **double-tap = fetch full story** from the article link |
| Weather | wttr.in current + 3-day; gear = city, long-press = units |
| File Server | HTTP file manager for the SD card at `http://<ip>/` — **runs only while the app is open** |
| Images | JPEG (TJpg, auto-scale) + 24-bit BMP; tap sides for prev/next |
| Calendar | *not yet implemented* |

## SD card layout (suggested)

```
/music/...        MP3/WAV/FLAC/AAC (subfolders fine — folder = playlist)
/roms/*.nes       NES ROMs
/photos/*.jpg     images (also /images)
/notes/*.txt      created by the Notes app
```

## Known fixes & gotchas (hard-won)

- **Backlight PWM must be ≥30kHz** — the vendor's 5kHz couples into the audio
  amp as a 3–8kHz hiss whenever brightness < 100%.
- **In-cell touch protocol**: must drain the full `7 × maxPoints` buffer each
  read or the controller stalls after the first touch; 100kHz I2C only.
- **`nes_reset()` nulls the emulator's video buffer** — re-attach every frame.
- **Arduino FS directory iteration is unreliable on SD_MMC subfolders** — use
  POSIX `opendir/readdir/stat` on `/sdcard/...`.
- **`DISPLAY` is a macro in Arduino.h** — don't use it as an identifier.
- Panel requires x/width aligned to 4px for partial window pushes.
- WiFi modem sleep disabled during operation (power-save bursts click the amp).
- Rotation is intentionally disabled (in-cell panel: no hardware landscape;
  stored rotation NVS is ignored and forced to 0).

## Credits / licenses

- ST77922 init sequence + QSPI protocol: vendor demo (QD electronic), guidance-only firmware
- nofrendo NES core: Matthew Conte et al., ducalex/retro-go fork — **GPL-2** ([COPYING](main/src/nofrendo/COPYING))
- ES8311 driver: ESP32-audioI2S examples (schreibfaul1)
- Weather/file-server concepts: prior projects in `D:\done code\esp32-rss-reader`

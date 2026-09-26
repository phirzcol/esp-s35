#include "AudioHAL.h"
#include "HW_Pins.h"
#include "TouchHAL.h"     // i2cLock/i2cUnlock
#include "SettingsStore.h"
#include <Audio.h>
#include <SD_MMC.h>
#include "es8311.h"
#include <driver/i2s_std.h>

AudioHAL AudioSys;

static Audio* s_audio = nullptr;      // created lazily in audio task
static ES8311 s_codec;

enum CmdType : uint8_t { CMD_PLAY, CMD_PAUSE, CMD_STOP, CMD_VOL, CMD_SEEK };
struct Cmd { CmdType type; uint8_t val; uint32_t u32; char path[192]; };
static QueueHandle_t s_cmdQ = nullptr;

// Codec volume staging: the library runs at a FIXED -3dB (headroom for MP3
// decode overshoot on hot masters); loudness is ES8311 attenuation only,
// capped at 0dB so the codec never adds gain (reg 191 = 0dB, 0.5dB/step).
static volatile uint8_t s_codecVolPct = 60;   // user percent
static volatile bool s_codecVolDirty = true;
static bool s_ampOn = false;
static uint32_t s_lastActiveMs = 0;
static volatile bool s_wantSpectrum = false;
static const uint32_t AMP_IDLE_OFF_MS = 4000;

static uint8_t codecVolFromPct(uint8_t v) {
  if (v == 0) return 0;                       // hard mute
  float dB = -50.0f * (1.0f - v / 100.0f);    // 0dB at 100%, -50dB at ~0%
  int reg = 191 + (int)lroundf(2.0f * dB);
  if (reg < 13) reg = 13;
  return (uint8_t)lroundf(reg / 2.55f);       // ES8311::setVolume() maps *2.55 -> reg
}

// ---- raw PCM path (emulators) ----
static i2s_chan_handle_t s_rawTx = nullptr;
static int16_t* s_rawStereo = nullptr;
static const int RAW_CHUNK = 1024;            // mono samples per write chunk
static bool s_codecReady = false;

bool AudioHAL::codecInit() {
  if (s_codecReady) return true;
  i2cLock();
  bool ok = s_codec.begin(PIN_TP_SDA, PIN_TP_SCL, 100000);  // shared bus, keep 100kHz
  if (ok) {
    s_codec.setBitsPerSample(16);
    s_codec.setVolume(codecVolFromPct(s_codecVolPct));
  }
  i2cUnlock();
  if (!ok) Serial.println("WARN: ES8311 codec not found");
  s_codecReady = ok;
  return ok;
}

bool AudioHAL::rawStart(uint32_t sampleRate) {
  if (s_rawTx) return true;
  stop();                                   // MP3 engine off (no-op if never started)
  if (!codecInit()) return false;
  if (!s_rawStereo) {
    s_rawStereo = (int16_t*)heap_caps_malloc(RAW_CHUNK * 2 * sizeof(int16_t),
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_rawStereo) return false;
  }

  i2s_chan_config_t ccfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  ccfg.dma_desc_num = 8;
  ccfg.dma_frame_num = 512;
  if (i2s_new_channel(&ccfg, &s_rawTx, nullptr) != ESP_OK) return false;

  i2s_std_config_t scfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = (gpio_num_t)PIN_I2S_MCLK,
          .bclk = (gpio_num_t)PIN_I2S_BCLK,
          .ws = (gpio_num_t)PIN_I2S_LRCK,
          .dout = (gpio_num_t)PIN_I2S_DOUT,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {0, 0, 0},
      },
  };
  scfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(s_rawTx, &scfg) != ESP_OK ||
      i2s_channel_enable(s_rawTx) != ESP_OK) {
    i2s_del_channel(s_rawTx);
    s_rawTx = nullptr;
    return false;
  }

  ampEnable(true);
  return true;
}

void AudioHAL::rawWrite(const int16_t* mono, int n) {
  if (!s_rawTx) return;
  while (n > 0) {
    int chunk = n > RAW_CHUNK ? RAW_CHUNK : n;
    for (int i = 0; i < chunk; i++) {
      s_rawStereo[i * 2] = mono[i];
      s_rawStereo[i * 2 + 1] = mono[i];
    }
    size_t written = 0;
    i2s_channel_write(s_rawTx, s_rawStereo, chunk * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    mono += chunk;
    n -= chunk;
  }
}

void AudioHAL::rawStop() {
  if (!s_rawTx) return;
  ampEnable(false);
  i2s_channel_disable(s_rawTx);
  i2s_del_channel(s_rawTx);
  s_rawTx = nullptr;
  // Hand the pins back to the MP3 engine if it exists
  if (s_audio) s_audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_MCLK);
}

// Raw sample ring for the radial visualizer (written from audio task)
static const int WAVE_N = 1024;
static int16_t s_wave[WAVE_N];
static volatile int s_waveIdx = 0;
static volatile bool s_capture = false;

// Weak hook from ESP32-audioI2S: post-volume samples on their way to I2S
void audio_process_i2s(int32_t* outBuff, int16_t validSamples, bool* continueI2S) {
  if (!s_capture) return;
  int idx = s_waveIdx;
  for (int i = 0; i < validSamples; i += 2) {   // left channel only
    s_wave[idx] = (int16_t)(outBuff[i] >> 16);
    idx = (idx + 1) & (WAVE_N - 1);
  }
  s_waveIdx = idx;
}

void AudioHAL::ampEnable(bool on) {
  if (on == s_ampOn) return;
  if (!on) {
    // mute codec before killing the amp to avoid the switch-off click
    i2cLock();
    s_codec.setVolume(0);
    i2cUnlock();
    vTaskDelay(pdMS_TO_TICKS(30));
    digitalWrite(PIN_AMP_EN, HIGH);
  } else {
    digitalWrite(PIN_AMP_EN, LOW);
    vTaskDelay(pdMS_TO_TICKS(30));
    i2cLock();
    s_codec.setVolume(codecVolFromPct(s_codecVolPct));   // restore immediately (raw path has no task)
    i2cUnlock();
  }
  s_ampOn = on;
}

void AudioHAL::lazyInit() {
  if (_hwReady) return;
  s_audio = new Audio();

  Audio::audio_info_callback = [](Audio::msg_t m) {
    AudioHAL* a = &AudioSys;
    switch (m.e) {
      case Audio::evt_eof:
        a->_eof = true;
        break;
      case Audio::evt_streamtitle:
      case Audio::evt_id3data:
        if (m.msg && strlen(m.msg) > 3) strlcpy(a->_title, m.msg, sizeof(a->_title));
        break;
      case Audio::evt_spectrum: {
        int n = m.vec1.size() < BANDS ? m.vec1.size() : BANDS;
        for (int i = 0; i < n; i++) {
          a->_bars[i] = (uint8_t)min<uint32_t>(m.vec1[i], 100);
          if (i < (int)m.vec2.size()) a->_peaks[i] = (uint8_t)min<uint32_t>(m.vec2[i], 100);
        }
        break;
      }
      default: break;
    }
  };

  s_audio->settings.DMA_DESC_NUM = 32;   // ~190ms buffer: rides out SD card stalls
  s_audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_MCLK);
  s_audio->settings.SPECTRUM = s_wantSpectrum;
  s_audio->forceMono(true);   // ES8311 drives one speaker; proper L+R downmix avoids one-channel pops
  // Fixed digital level: curve tops out at -3dB, volume pinned at max step
  s_audio->setVolumeCurve([](float t) { return -60.0f + 57.0f * t; });
  s_audio->setVolume(21);

  codecInit();

  _hwReady = true;
}

void AudioHAL::taskLoop() {
  Cmd cmd;
  for (;;) {
    while (s_cmdQ && xQueueReceive(s_cmdQ, &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case CMD_PLAY: {
          lazyInit();
          const char* base = strrchr(cmd.path, '/');
          strlcpy(_title, base ? base + 1 : cmd.path, sizeof(_title));
          ampEnable(true);
          s_audio->connecttoFS(SD_MMC, cmd.path, cmd.u32 > 0 ? (int32_t)cmd.u32 : -1);
          _active = true;
          _paused = false;
          _eof = false;
          break;
        }
        case CMD_PAUSE:
          if (s_audio && _active) {
            if (!_paused) {
              ampEnable(false);          // mutes codec first
              s_audio->pauseResume();
              _paused = true;
            } else {
              s_audio->pauseResume();
              ampEnable(true);
              _paused = false;
            }
          }
          break;
        case CMD_STOP:
          if (s_audio) s_audio->stopSong();
          _active = false;
          _paused = false;
          ampEnable(false);
          break;
        case CMD_VOL:
          s_codecVolPct = cmd.val;
          s_codecVolDirty = true;
          break;
        case CMD_SEEK:
          if (s_audio && _active) s_audio->setAudioPlayTime(cmd.u32);
          break;
      }
    }

    if (s_audio) {
      s_audio->loop();

      if (_active) {
        s_lastActiveMs = millis();
        _curSec = s_audio->getAudioCurrentTime();
        _durSec = s_audio->getAudioFileDuration();
        if (!s_audio->isRunning() && !_paused && _eof) {
          _active = false;   // amp stays on: auto-advance restarts within the idle grace
        }
      } else if (s_ampOn && millis() - s_lastActiveMs > AMP_IDLE_OFF_MS) {
        ampEnable(false);    // nothing followed; power down quietly
      }
    }

    // Rate-limited codec volume writes (shared I2C bus); serves MP3 and raw paths
    static uint32_t lastVolWrite = 0;
    if (s_codecVolDirty && s_ampOn && s_codecReady && millis() - lastVolWrite > 50) {
      s_codecVolDirty = false;
      lastVolWrite = millis();
      i2cLock();
      s_codec.setVolume(codecVolFromPct(s_codecVolPct));
      i2cUnlock();
    }
    vTaskDelay(1);
  }
}

void AudioHAL::taskEntry(void* self) { ((AudioHAL*)self)->taskLoop(); }

void AudioHAL::begin() {
  if (s_cmdQ) return;
  s_codecVolPct = Settings.volume;
  s_cmdQ = xQueueCreate(8, sizeof(Cmd));
  // Audio decode on core 0 with the other hardware services
  xTaskCreatePinnedToCore(taskEntry, "audioTask", 12288, this, 2, nullptr, 0);
}

void AudioHAL::playFile(const char* path, uint32_t startSec) {
  if (!s_cmdQ) return;
  Cmd c = {};
  c.type = CMD_PLAY;
  c.u32 = startSec;
  strlcpy(c.path, path, sizeof(c.path));
  xQueueSend(s_cmdQ, &c, 0);
}
void AudioHAL::togglePause() { if (!s_cmdQ) return; Cmd c = {}; c.type = CMD_PAUSE; xQueueSend(s_cmdQ, &c, 0); }
void AudioHAL::stop()        { if (!s_cmdQ) return; Cmd c = {}; c.type = CMD_STOP;  xQueueSend(s_cmdQ, &c, 0); }
void AudioHAL::setVolume(uint8_t pct) {
  if (!s_cmdQ) { s_codecVolPct = pct; return; }
  Cmd c = {};
  c.type = CMD_VOL;
  c.val = pct;
  xQueueSend(s_cmdQ, &c, 0);
}

void AudioHAL::setSpectrum(bool on) {
  s_wantSpectrum = on;
  if (s_audio) s_audio->settings.SPECTRUM = on;
}

void AudioHAL::seekTo(uint32_t sec) {
  if (!s_cmdQ) return;
  Cmd c = {};
  c.type = CMD_SEEK;
  c.u32 = sec;
  xQueueSend(s_cmdQ, &c, 0);
}

void AudioHAL::setCapture(bool on) { s_capture = on; }

int AudioHAL::getWave(int16_t* dst, int n) {
  if (n > WAVE_N) n = WAVE_N;
  int idx = (s_waveIdx - n) & (WAVE_N - 1);
  for (int i = 0; i < n; i++) {
    dst[i] = s_wave[idx];
    idx = (idx + 1) & (WAVE_N - 1);
  }
  return n;
}

bool AudioHAL::consumeEof() {
  if (!_eof) return false;
  _eof = false;
  return true;
}

void AudioHAL::getSpectrum(uint8_t* bars, uint8_t* peaks) {
  for (int i = 0; i < BANDS; i++) {
    bars[i] = _bars[i];
    peaks[i] = _peaks[i];
  }
}

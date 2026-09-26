#pragma once
#include <Arduino.h>

// Audio playback engine: ESP32-audioI2S decoder task on core 0 + ES8311 codec.
// UI core posts commands via queue; status/spectrum read back via snapshots.
class AudioHAL {
public:
  void begin();                        // create task+queue (hardware stays off)
  void playFile(const char* path, uint32_t startSec = 0);   // optional resume offset
  void togglePause();
  void stop();
  void setVolume(uint8_t pct);         // 0..100 -> ES8311 codec attenuation
  void seekTo(uint32_t sec);
  void setSpectrum(bool on);           // built-in FFT only while player UI visible

  // Raw PCM path for emulators (own I2S channel; mutually exclusive with MP3).
  // rawWrite blocks until DMA space frees -> paces the caller at the sample rate.
  bool rawStart(uint32_t sampleRate);
  void rawWrite(const int16_t* mono, int n);
  void rawStop();

  // Raw mono samples for the radial visualizer (capture must be enabled)
  void setCapture(bool on);
  int  getWave(int16_t* dst, int n);   // copies latest n samples, returns copied

  bool isActive()  const { return _active; }    // engine has a track loaded
  bool isPaused()  const { return _paused; }
  uint32_t currentSec() const { return _curSec; }
  uint32_t durationSec() const { return _durSec; }
  const char* title() const { return _title; }  // stream/ID3 title or filename
  bool consumeEof();                   // true once per finished track

  static const int BANDS = 15;
  void getSpectrum(uint8_t* bars, uint8_t* peaks); // 0..100 each

private:
  static void taskEntry(void*);
  void taskLoop();
  void lazyInit();
  bool codecInit();
  void ampEnable(bool on);

  volatile bool _active = false;
  volatile bool _paused = false;
  volatile bool _eof = false;
  volatile uint32_t _curSec = 0, _durSec = 0;
  char _title[96] = "";
  volatile uint8_t _bars[BANDS] = {0};
  volatile uint8_t _peaks[BANDS] = {0};
  bool _hwReady = false;
};

extern AudioHAL AudioSys;

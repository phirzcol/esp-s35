#include "SettingsStore.h"
#include <Preferences.h>

SettingsStore Settings;
static Preferences prefs;

void SettingsStore::begin() {
  prefs.begin("syscfg", false);
  brightness     = prefs.getUChar("bright", 200);
  rotation       = 0;   // rotation feature removed; ignore any stored value
  screensaverOn  = prefs.getBool("ssOn", true);
  screensaverSec = prefs.getUShort("ssSec", 60);
  volume         = prefs.getUChar("vol", 60);
  visualizerOn   = prefs.getBool("visOn", true);
  visSens        = prefs.getUChar("visSens", 50);
  visAmp         = prefs.getUChar("visAmp", 50);
  wifiOn         = prefs.getBool("wifiOn", true);
  wifiSsid       = prefs.getString("ssid", "");
  wifiPass       = prefs.getString("pass", "");
  btOn           = prefs.getBool("btOn", false);
  utcOffsetMin   = prefs.getInt("utcOff", 0);
  weatherCity    = prefs.getString("wxCity", "");
  weatherMetric  = prefs.getBool("wxMetric", false);
}

void SettingsStore::save() {
  prefs.putUChar("bright", brightness);
  prefs.putBool("ssOn", screensaverOn);
  prefs.putUShort("ssSec", screensaverSec);
  prefs.putUChar("vol", volume);
  prefs.putBool("visOn", visualizerOn);
  prefs.putUChar("visSens", visSens);
  prefs.putUChar("visAmp", visAmp);
  prefs.putBool("wifiOn", wifiOn);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putBool("btOn", btOn);
  prefs.putInt("utcOff", utcOffsetMin);
  prefs.putString("wxCity", weatherCity);
  prefs.putBool("wxMetric", weatherMetric);
}

#pragma once
// ES3C35P (3.5" ESP32-S3 Display) pin map — verified against lcdwiki.com/3.5inch_ESP32-S3_Display
// and vendor demo package (QD electronic). Do NOT reuse these pins for external peripherals.

// LCD: ST77922, QSPI @ 80MHz, 320x480 RGB565
#define PIN_LCD_CS      10
#define PIN_LCD_SCLK    12
#define PIN_LCD_D0      11
#define PIN_LCD_D1      13
#define PIN_LCD_D2      14
#define PIN_LCD_D3      9
#define PIN_LCD_TE      42   // tear effect (unused for now)
#define PIN_LCD_BL      41   // backlight, PWM, high = on

// Touch: FT6336U, I2C addr 0x38 (bus shared with ES8311 codec)
#define PIN_TP_SDA      38
#define PIN_TP_SCL      39
#define PIN_TP_RST      48
#define PIN_TP_INT      47

// Audio: ES8311 codec on shared I2C; I2S below. Amp enable is ACTIVE LOW.
#define PIN_I2S_MCLK    17
#define PIN_I2S_BCLK    18
#define PIN_I2S_DOUT    15   // to speaker
#define PIN_I2S_LRCK    21
#define PIN_I2S_DIN     16   // from mic
#define PIN_AMP_EN      1    // low = amp on

// MicroSD: SDMMC 4-bit
#define PIN_SD_CLK      5
#define PIN_SD_CMD      4
#define PIN_SD_D0       6
#define PIN_SD_D1       7
#define PIN_SD_D2       2
#define PIN_SD_D3       3

// Misc
#define PIN_BAT_ADC     8    // battery voltage via divider (x2)
#define PIN_RGB_LED     40   // WS2812, leave idle = off
#define PIN_BOOT_BTN    0

#define LCD_NATIVE_W    320
#define LCD_NATIVE_H    480

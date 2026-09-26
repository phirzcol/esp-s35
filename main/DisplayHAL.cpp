// ST77922 QSPI driver. Init sequence and QSPI transaction pattern adapted from the
// vendor demo (QD electronic, Example_01_Simple_test). Provided for guidance only —
// see vendor firmware disclaimer in the original demo package.
#include "DisplayHAL.h"
#include "HW_Pins.h"
#include <Arduino.h>
#include "driver/spi_master.h"
#include "hal/gpio_ll.h"

DisplayHAL Gfx;

#define QSPI_PORT       SPI2_HOST
#define QSPI_FREQUENCY  80000000
#define TX_LEN          0x4000  // pixels per transaction chunk

#define LCD_CS_LOW   GPIO.out_w1tc = (1u << PIN_LCD_CS)
#define LCD_CS_HIGH  GPIO.out_w1ts = (1u << PIN_LCD_CS)

static spi_device_handle_t s_qspi;

struct lcd_init_cmd { uint8_t cmd; const uint8_t* data; uint8_t len; uint32_t delay_ms; };

// Vendor ST77922 init table (current revision from Simple_test.ino)
static const uint8_t d_F1[]={0x00}; static const uint8_t d_60[]={0x00,0x00,0x00};
static const uint8_t d_65[]={0x80}; static const uint8_t d_79[]={0x06};
static const uint8_t d_7B[]={0x00,0x08,0x08};
static const uint8_t d_80[]={0x55,0x62,0x2F,0x17,0xF0,0x52,0x70,0xD2,0x52,0x62,0xEA};
static const uint8_t d_81[]={0x26,0x52,0x72,0x27};
static const uint8_t d_84[]={0x92,0x25};
static const uint8_t d_87[]={0x10,0x10,0x58,0x00,0x02,0x3A};
static const uint8_t d_88[]={0x00,0x00,0x2C,0x10,0x04,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x06};
static const uint8_t d_89[]={0x00,0x00,0x00};
static const uint8_t d_8A[]={0x13,0x00,0x2C,0x00,0x00,0x2C,0x10,0x10,0x00,0x3E,0x19};
static const uint8_t d_8B[]={0x15,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x97,0x8E};
static const uint8_t d_8C[]={0x1D,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x50,0x0F,0x01,0xC5,0x12,0x09};
static const uint8_t d_8D[]={0x0C};
static const uint8_t d_8E[]={0x33,0x01,0x0C,0x13,0x01,0x01};
static const uint8_t d_B3[]={0x00,0x30};
static const uint8_t d_71[]={0xD0};
static const uint8_t d_66[]={0x02,0x3F};
static const uint8_t d_BE[]={0x26,0x00,0x9D};
static const uint8_t d_70[]={0x01,0xA0,0x11,0x40,0xE0,0x00,0x11,0x69,0x11,0x00,0x00,0x1A};
static const uint8_t d_90[]={0x04,0x04,0x55,0x74,0x00,0x40,0x43,0x27,0x27};
static const uint8_t d_91[]={0x04,0x04,0x55,0x75,0x00,0x40,0x42,0x27,0x27};
static const uint8_t d_92[]={0x04,0x44,0x55,0xC0,0x06,0x00,0x07,0x05,0x90,0x27};
static const uint8_t d_93[]={0x04,0x43,0x11,0x00,0x00,0x00,0x00,0x05,0x90,0x27};
static const uint8_t d_94[]={0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t d_95[]={0x96,0x16,0x00,0x00,0xFF};
static const uint8_t d_96[]={0x44,0x53,0x03,0x12,0x23,0x24,0x06,0x05,0x94,0x27,0x00,0x44};
static const uint8_t d_97[]={0x44,0x53,0x47,0x56,0x20,0x20,0x02,0x01,0x94,0x27,0x00,0x44};
static const uint8_t d_BA[]={0x55,0x94,0x2D,0x94,0x27};
static const uint8_t d_9A[]={0x40,0x00,0x06,0x00,0x00,0x00,0x00};
static const uint8_t d_9B[]={0x00,0x00,0x06,0x00,0x00,0x00,0x00};
static const uint8_t d_9C[]={0x5C,0x12,0x00,0x00,0x10,0x12,0x00,0x00,0x10,0x02,0x00,0x00,0x00};
static const uint8_t d_9D[]={0x8A,0x51,0x00,0x00,0x00,0x80,0x1E,0x01};
static const uint8_t d_9E[]={0x51,0x00,0x00,0x00,0x80,0x1E,0x01};
static const uint8_t d_B4[]={0x1D,0x1C,0x1E,0x0B,0x14,0x02,0x13,0x09,0x1E,0x00,0x1E,0x10};
static const uint8_t d_B5[]={0x1D,0x1C,0x1E,0x0A,0x15,0x03,0x11,0x08,0x1E,0x01,0x1E,0x12};
static const uint8_t d_B6[]={0x77,0x77,0x00,0x0A,0xFF,0x0A,0xFF};
static const uint8_t d_86[]={0xCD,0x04,0xB1,0x02,0x58,0x12,0x58,0x0C,0x13,0x01,0xA5,0x00,0xA5,0xA5};
static const uint8_t d_B7[]={0x07,0x0A,0x0E,0x06,0x05,0x03,0x2B,0x03,0x03,0x42,0x07,0x10,0x10,0x2E,0x3F,0x0D};
static const uint8_t d_B8[]={0x07,0x0A,0x0D,0x05,0x05,0x02,0x2B,0x02,0x03,0x42,0x06,0x10,0x0F,0x2E,0x3F,0x0D};
static const uint8_t d_B9[]={0x23,0x23};
static const uint8_t d_BF1[]={0x10,0x14,0x14,0x0B,0x0B,0x0B};
static const uint8_t d_F2[]={0x00};
static const uint8_t d_73[]={0x04,0xDA,0x12,0x54,0x47};
static const uint8_t d_77[]={0x6B,0x5B,0xFD,0xC3,0xC5};
static const uint8_t d_7A[]={0x15,0x27};
static const uint8_t d_7B2[]={0x04,0x57};
static const uint8_t d_7E[]={0x01,0x0E};
static const uint8_t d_BF2[]={0x36};
static const uint8_t d_E3[]={0x40,0x40};
static const uint8_t d_F0[]={0x00};
static const uint8_t d_D0[]={0x00};
static const uint8_t d_2A[]={0x00,0x00,0x01,0x3F};
static const uint8_t d_2B[]={0x00,0x00,0x01,0xDF};
static const uint8_t d_3A[]={0x01};
static const uint8_t d_36[]={0x00};
static const uint8_t d_35[]={0x01};

static const lcd_init_cmd st77922_init[] = {
  {0xF1,d_F1,1,0},{0x60,d_60,3,0},{0x65,d_65,1,0},{0x79,d_79,1,0},{0x7B,d_7B,3,0},
  {0x80,d_80,11,0},{0x81,d_81,4,0},{0x84,d_84,2,0},{0x87,d_87,6,0},{0x88,d_88,15,0},
  {0x89,d_89,3,0},{0x8A,d_8A,11,0},{0x8B,d_8B,9,0},{0x8C,d_8C,13,0},{0x8D,d_8D,1,0},
  {0x8E,d_8E,6,0},{0xB3,d_B3,2,0},{0xF1,d_F1,1,0},{0x71,d_71,1,0},{0x66,d_66,2,0},
  {0xBE,d_BE,3,0},{0x70,d_70,12,0},{0x90,d_90,9,0},{0x91,d_91,9,0},{0x92,d_92,10,0},
  {0x93,d_93,10,0},{0x94,d_94,6,0},{0x95,d_95,5,0},{0x96,d_96,12,0},{0x97,d_97,12,0},
  {0xBA,d_BA,5,0},{0x9A,d_9A,7,0},{0x9B,d_9B,7,0},{0x9C,d_9C,13,0},{0x9D,d_9D,8,0},
  {0x9E,d_9E,7,0},{0xB4,d_B4,12,0},{0xB5,d_B5,12,0},{0xB6,d_B6,7,0},{0x86,d_86,14,0},
  {0xB7,d_B7,16,0},{0xB8,d_B8,16,0},{0xB9,d_B9,2,0},{0xBF,d_BF1,6,0},{0xF2,d_F2,1,0},
  {0x73,d_73,5,0},{0x77,d_77,5,0},{0x7A,d_7A,2,0},{0x7B,d_7B2,2,0},{0x7E,d_7E,2,0},
  {0xBF,d_BF2,1,0},{0xE3,d_E3,2,0},{0xF0,d_F0,1,0},{0xD0,d_D0,1,0},
  {0x2A,d_2A,4,0},{0x2B,d_2B,4,0},
  {0x21,nullptr,0,0},{0x11,nullptr,0,120},{0x29,nullptr,0,0},{0x2C,nullptr,0,0},
  {0x3A,d_3A,1,0},{0x36,d_36,1,0},{0x35,d_35,1,20},
};

void DisplayHAL::qspiInit() {
  spi_bus_config_t buscfg = {};
  buscfg.data0_io_num = PIN_LCD_D0;
  buscfg.data1_io_num = PIN_LCD_D1;
  buscfg.sclk_io_num  = PIN_LCD_SCLK;
  buscfg.data2_io_num = PIN_LCD_D2;
  buscfg.data3_io_num = PIN_LCD_D3;
  buscfg.max_transfer_sz = (TX_LEN * 16) + 8;
  buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS | SPICOMMON_BUSFLAG_QUAD;
  ESP_ERROR_CHECK(spi_bus_initialize(QSPI_PORT, &buscfg, SPI_DMA_CH_AUTO));

  spi_device_interface_config_t devcfg = {};
  devcfg.mode = SPI_MODE0;
  devcfg.clock_speed_hz = QSPI_FREQUENCY;
  devcfg.spics_io_num = PIN_LCD_CS;
  devcfg.flags = SPI_DEVICE_HALFDUPLEX;
  devcfg.queue_size = 17;
  ESP_ERROR_CHECK(spi_bus_add_device(QSPI_PORT, &devcfg, &s_qspi));
}

void DisplayHAL::lcdWriteReg(uint32_t cmd, const void* data, uint8_t len) {
  LCD_CS_LOW;
  spi_transaction_ext_t t = {};
  t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
  t.base.cmd = 0x02;
  t.base.addr = cmd << 8;
  t.command_bits = 8;
  t.address_bits = 24;
  if (len) { t.base.tx_buffer = data; t.base.length = 8u * len; }
  spi_device_polling_transmit(s_qspi, (spi_transaction_t*)&t);
  LCD_CS_HIGH;
}

void DisplayHAL::lcdSetWindow(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey) {
  uint8_t xa[4] = {(uint8_t)(sx>>8),(uint8_t)sx,(uint8_t)((ex-1)>>8),(uint8_t)(ex-1)};
  uint8_t ya[4] = {(uint8_t)(sy>>8),(uint8_t)sy,(uint8_t)((ey-1)>>8),(uint8_t)(ey-1)};
  lcdWriteReg(0x2A, xa, 4);
  lcdWriteReg(0x2B, ya, 4);
}

void DisplayHAL::beginPixels() { LCD_CS_LOW; _firstChunk = true; }
void DisplayHAL::endPixels()   { LCD_CS_HIGH; }

void DisplayHAL::pushChunk(const uint16_t* px, size_t count) {
  spi_transaction_ext_t t = {};
  while (count) {
    size_t n = (count > TX_LEN) ? TX_LEN : count;
    if (_firstChunk) {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
      t.base.cmd = 0x32;
      t.base.addr = 0x3C << 8;
      t.command_bits = 8;
      t.address_bits = 24;
      _firstChunk = false;
    } else {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    t.base.tx_buffer = px;
    t.base.length = n * 16;
    spi_device_polling_transmit(s_qspi, (spi_transaction_t*)&t);
    px += n;
    count -= n;
  }
}

void DisplayHAL::applyRotation() {
  // Panel has no hardware row/col exchange: landscape stays portrait in MADCTL
  // and is transposed in software (matches vendor ST77922 Arduino lib).
  static const uint8_t mad[4] = { 0x00, 0x00, 0xC0, 0x00 };
  uint8_t v = mad[_rot & 3];
  lcdWriteReg(0x36, &v, 1);
  if (_rot & 1) { _w = LCD_NATIVE_H; _h = LCD_NATIVE_W; }
  else          { _w = LCD_NATIVE_W; _h = LCD_NATIVE_H; }
}

bool DisplayHAL::begin(uint8_t rotation, uint8_t brightness) {
  pinMode(PIN_LCD_CS, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);

  // Backlight PWM (off until first frame is presented)
  // 30kHz carrier: above the audio band so LED ripple can't couple into the amp
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_LCD_BL, 30000, 10);
  ledcWrite(PIN_LCD_BL, 0);
#else
  ledcSetup(0, 30000, 10);
  ledcAttachPin(PIN_LCD_BL, 0);
  ledcWrite(0, 0);
#endif

  qspiInit();
  for (auto& c : st77922_init) {
    lcdWriteReg(c.cmd, c.data, c.len);
    if (c.delay_ms) delay(c.delay_ms);
  }

  _rot = rotation & 3;
  applyRotation();

  _canvas.setPsram(true);
  _canvas.setColorDepth(16);
  if (!_canvas.createSprite(_w, _h)) return false;
  _canvas.setTextWrap(false);

  _bounce = (uint16_t*)heap_caps_malloc(TX_LEN * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!_bounce) return false;
  _xpose = (uint16_t*)ps_malloc((size_t)LCD_NATIVE_W * LCD_NATIVE_H * sizeof(uint16_t));
  if (!_xpose) return false;

  _canvas.fillScreen(TFT_BLACK);
  present();
  setBrightness(brightness);
  return true;
}

void DisplayHAL::setBrightness(uint8_t b) {
  _bright = b;
  uint32_t duty = ((uint32_t)b * 1023) / 255;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_LCD_BL, duty);
#else
  ledcWrite(0, duty);
#endif
}

void DisplayHAL::setRotation(uint8_t r) {
  r &= 3;
  if (r == _rot) return;
  _rot = r;
  applyRotation();
  _canvas.deleteSprite();
  _canvas.setPsram(true);
  _canvas.setColorDepth(16);
  if (!_canvas.createSprite(_w, _h)) {
    Serial.println("FATAL: canvas realloc failed");
    return;
  }
  _canvas.fillScreen(TFT_BLACK);
  present();
}

void DisplayHAL::present() {
  const uint16_t* src = (const uint16_t*)_canvas.getBuffer();
  const uint16_t* out = src;

  if (_rot & 1) {
    // Tiled transpose landscape canvas -> portrait panel buffer (vendor scheme)
    const int W = _w, H = _h;            // 480 x 320 logical
    const int TS = 32;
    uint16_t* dst = _xpose;
    for (int i0 = 0; i0 < H; i0 += TS) {
      int iMax = (i0 + TS < H) ? i0 + TS : H;
      for (int j0 = 0; j0 < W; j0 += TS) {
        int jMax = (j0 + TS < W) ? j0 + TS : W;
        for (int i = i0; i < iMax; i++) {
          const uint16_t* s = src + (size_t)i * W;
          if (_rot == 1) {
            for (int j = j0; j < jMax; j++) dst[(size_t)j * H + (H - 1 - i)] = s[j];
          } else {
            for (int j = j0; j < jMax; j++) dst[(size_t)(W - 1 - j) * H + i] = s[j];
          }
        }
      }
    }
    out = _xpose;
  }

  lcdSetWindow(0, 0, LCD_NATIVE_W, LCD_NATIVE_H);
  beginPixels();
  pushChunk(out, (size_t)LCD_NATIVE_W * LCD_NATIVE_H);
  endPixels();
}

void DisplayHAL::presentRect(int x, int y, int w, int h) {
  // Landscape needs the transpose path; partial windows only work in portrait.
  if (_rot & 1) { present(); return; }
  // Panel requires x and w aligned to 4 px; expand the rect to satisfy that.
  int x2 = x + w;
  x &= ~3;
  x2 = (x2 + 3) & ~3;
  if (x < 0) x = 0;
  if (x2 > _w) x2 = _w;
  if (y < 0) { h += y; y = 0; }
  if (y + h > _h) h = _h - y;
  w = x2 - x;
  if (w <= 0 || h <= 0) return;

  const uint16_t* buf = (const uint16_t*)_canvas.getBuffer();
  lcdSetWindow(x, y, x + w, y + h);
  beginPixels();
  size_t rowsPerChunk = TX_LEN / (size_t)w;
  if (rowsPerChunk == 0) rowsPerChunk = 1;
  int row = 0;
  while (row < h) {
    size_t rows = ((size_t)(h - row) < rowsPerChunk) ? (size_t)(h - row) : rowsPerChunk;
    uint16_t* dst = _bounce;
    for (size_t r = 0; r < rows; r++) {
      memcpy(dst, buf + (size_t)(y + row + r) * _w + x, (size_t)w * 2);
      dst += w;
    }
    pushChunk(_bounce, rows * (size_t)w);
    row += rows;
  }
  endPixels();
}

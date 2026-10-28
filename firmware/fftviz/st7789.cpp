// st7789.cpp — IDF spi_master ST7789 sürücüsü. Tüm işlemler kuyruklu (interrupt)
// gönderilir; DC pini pre_cb içinde transaction->user ile sürülür. Böylece şerit
// render'ı önceki şeridin DMA'sıyla örtüşür.
#include "st7789.h"
#include "pins.h"
#include <driver/spi_master.h>
#include <soc/gpio_reg.h>
#include <driver/gpio.h>

namespace st7789 {

static constexpr int QDEPTH = 24;
static constexpr int MAX_XFER = 240 * 24 * 2 + 16;

static spi_device_handle_t s_dev = nullptr;
static bool s_bus = false;
static Config s_cfg;
static spi_transaction_t s_tr[QDEPTH];
static uint32_t s_queued = 0, s_done = 0;
static volatile uint32_t s_err = 0, s_xfers = 0;
static int16_t s_ox = 0, s_oy = 0;

// 240x240 panel 240x320 RAM'li kontrolcüde: bazı rotasyonlarda 80 satır/sütun kayar.
static const int16_t kAutoOff[4][2] = {{0, 0}, {0, 0}, {0, 80}, {80, 0}};
// MADCTL: MY=0x80, MX=0x40, MV=0x20; RGB sırası.
static const uint8_t kMadctl[4] = {0x00, 0x60, 0xC0, 0xA0};

static void IRAM_ATTR dc_pre_cb(spi_transaction_t* t) {
  if ((intptr_t)t->user) REG_WRITE(GPIO_OUT_W1TS_REG, 1u << PIN_TFT_DC);
  else REG_WRITE(GPIO_OUT_W1TC_REG, 1u << PIN_TFT_DC);
}

static void reapOne() {
  spi_transaction_t* r = nullptr;
  for (;;) {
    esp_err_t e = spi_device_get_trans_result(s_dev, &r, pdMS_TO_TICKS(500));
    if (e == ESP_OK) break;
    s_err = s_err + 1;  // zaman aşımı: donanım takılmaz, yine de beklemeye devam
  }
  ++s_done;
}

void waitToken(uint32_t token) {
  while ((int32_t)(s_done - token) < 0 && s_done != s_queued) reapOne();
}

void waitIdle() {
  while (s_done != s_queued) reapOne();
}

static spi_transaction_t* slot() {
  if (s_queued - s_done >= (uint32_t)QDEPTH) reapOne();
  spi_transaction_t* t = &s_tr[s_queued % QDEPTH];
  memset(t, 0, sizeof(*t));
  return t;
}

static uint32_t enqueue(spi_transaction_t* t) {
  esp_err_t e = spi_device_queue_trans(s_dev, t, portMAX_DELAY);
  if (e != ESP_OK) { s_err = s_err + 1; return s_queued; }
  return ++s_queued;
}

static void cmd(uint8_t c, const uint8_t* data = nullptr, int n = 0) {
  spi_transaction_t* t = slot();
  t->flags = SPI_TRANS_USE_TXDATA;
  t->length = 8;
  t->tx_data[0] = c;
  t->user = (void*)0;
  enqueue(t);
  if (n > 0) {
    t = slot();
    t->user = (void*)1;
    t->length = 8 * n;
    if (n <= 4) {
      t->flags = SPI_TRANS_USE_TXDATA;
      memcpy(t->tx_data, data, n);
    } else {
      t->tx_buffer = data;  // çağıran waitIdle ile tamamlanmayı bekler
    }
    enqueue(t);
  }
}

static bool addDevice(int mhz) {
  spi_device_interface_config_t dc = {};
  dc.mode = 3;  // CS yok → mode 3 (GMT130)
  dc.clock_speed_hz = mhz * 1000000;
  dc.spics_io_num = -1;
  dc.queue_size = QDEPTH;
  dc.pre_cb = dc_pre_cb;
  dc.flags = SPI_DEVICE_NO_DUMMY;
  return spi_bus_add_device(SPI3_HOST, &dc, &s_dev) == ESP_OK;
}

static void applyRotation() {
  uint8_t r = s_cfg.rot & 3;
  s_ox = s_cfg.offX >= 0 ? s_cfg.offX : kAutoOff[r][0];
  s_oy = s_cfg.offY >= 0 ? s_cfg.offY : kAutoOff[r][1];
  uint8_t m = kMadctl[r];
  cmd(0x36, &m, 1);
  waitIdle();
}

bool begin(const Config& c) {
  s_cfg = c;
  pinMode(PIN_TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_DC, HIGH);
  pinMode(PIN_TFT_RST, OUTPUT);
  ledcAttach(PIN_TFT_BLK, TFT_BL_FREQ, 8);
  ledcWrite(PIN_TFT_BLK, 0);

  if (!s_bus) {
    spi_bus_config_t bc = {};
    bc.mosi_io_num = PIN_TFT_MOSI;
    bc.miso_io_num = -1;
    bc.sclk_io_num = PIN_TFT_SCK;
    bc.quadwp_io_num = -1;
    bc.quadhd_io_num = -1;
    bc.max_transfer_sz = MAX_XFER;
    if (spi_bus_initialize(SPI3_HOST, &bc, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    s_bus = true;
  }
  if (!addDevice(s_cfg.mhz)) return false;
  for (int pin : {PIN_TFT_SCK, PIN_TFT_MOSI, PIN_TFT_DC}) gpio_set_drive_capability((gpio_num_t)pin, (gpio_drive_cap_t)TFT_DRIVE_CAP);

  // Donanım reset
  digitalWrite(PIN_TFT_RST, HIGH); delay(5);
  digitalWrite(PIN_TFT_RST, LOW); delay(20);
  digitalWrite(PIN_TFT_RST, HIGH); delay(150);

  static const uint8_t colmod = 0x55;  // 16 bit RGB565
  cmd(0x01); waitIdle(); delay(150);   // SWRESET
  cmd(0x11); waitIdle(); delay(120);   // SLPOUT
  cmd(0x3A, &colmod, 1); waitIdle(); delay(10);  // COLMOD
  applyRotation();                     // MADCTL
  cmd(0x21);                           // INVON (IPS panel)
  cmd(0x13); waitIdle(); delay(10);    // NORON
  cmd(0x29); waitIdle(); delay(20);    // DISPON
  return true;
}

bool reconfigure(const Config& c) {
  waitIdle();
  bool clk = c.mhz != s_cfg.mhz;
  s_cfg = c;
  if (clk) {
    spi_bus_remove_device(s_dev);
    s_dev = nullptr;
    if (!addDevice(s_cfg.mhz)) { s_err = s_err + 1; return false; }
  }
  applyRotation();
  return true;
}

const Config& config() { return s_cfg; }

void effectiveOffsets(int16_t* ox, int16_t* oy) { *ox = s_ox; *oy = s_oy; }

void setWindowRows(int y0, int y1) {
  uint8_t ca[4] = {(uint8_t)(s_ox >> 8), (uint8_t)s_ox, (uint8_t)((s_ox + 239) >> 8), (uint8_t)(s_ox + 239)};
  int a = s_oy + y0, b = s_oy + y1;
  uint8_t ra[4] = {(uint8_t)(a >> 8), (uint8_t)a, (uint8_t)(b >> 8), (uint8_t)b};
  cmd(0x2A, ca, 4);  // CASET (≤4 bayt → tx_data'ya kopyalanır)
  cmd(0x2B, ra, 4);  // RASET
  cmd(0x2C);         // RAMWR
}

uint32_t pushPixels(const uint16_t* px, int count) {
  spi_transaction_t* t = slot();
  t->user = (void*)1;
  t->length = count * 16;
  t->tx_buffer = px;
  uint32_t tok = enqueue(t);
  s_xfers = s_xfers + 1;
  return tok;
}

static bool s_blConst = false;
void sleep() {
  waitIdle();
  ledcDetach(PIN_TFT_BLK);
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, LOW);
  s_blConst = true;
  cmd(0x10);
  waitIdle();
  delay(120);
}

void wake() {
  cmd(0x11);
  waitIdle();
  delay(120);
}

void backlight(uint8_t v) {
  if (s_blConst) { ledcAttach(PIN_TFT_BLK, TFT_BL_FREQ, 8); s_blConst = false; }
  ledcWrite(PIN_TFT_BLK, v);
}
// Teşhis: PWM olmadan sabit tam parlaklık (5 kHz anahtarlama yok).
void backlightConst() {
  ledcDetach(PIN_TFT_BLK);
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, HIGH);
  s_blConst = true;
}
uint32_t errors() { return s_err; }
uint32_t transfers() { return s_xfers; }

}  // namespace st7789

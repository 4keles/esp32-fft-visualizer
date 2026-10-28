// st7789.h — IDF spi_master üzerinde ST7789 sürücüsü (CS yok, mode 3, DMA kuyruğu).
#pragma once
#include <Arduino.h>

namespace st7789 {

struct Config {
  int mhz = 40;      // SPI saati (10..80)
  uint8_t rot = 0;   // 0..3
  int16_t offX = -1; // -1 = rotasyona göre otomatik
  int16_t offY = -1;
};

bool begin(const Config& c);
// Saat/rotasyon/ofset değişimi (kuyruk boşaltılır, cihaz yeniden eklenir).
bool reconfigure(const Config& c);
const Config& config();
void effectiveOffsets(int16_t* ox, int16_t* oy);

// Pencere: x 0..239, satır y0..y1 (dahil). Ardından RAMWR gönderilir.
void setWindowRows(int y0, int y1);
// Pikselleri (wire-order RGB565, DMA bellekte) kuyruğa ekler; buffer `waitBuffer`
// ile serbest bırakılana kadar değiştirilmemeli. Dönen token buffer'ı izler.
uint32_t pushPixels(const uint16_t* px, int count);
// Token'a kadar tüm işlemlerin bitmesini bekler.
void waitToken(uint32_t token);
void waitIdle();
void sleep();
void wake();

void backlight(uint8_t v);
void backlightConst();
uint32_t errors();
uint32_t transfers();

}  // namespace st7789

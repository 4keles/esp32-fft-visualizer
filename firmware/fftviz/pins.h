// pins.h — pin haritası ve donanım sabitleri (hardware-drivers.md §7). Değerler config.h'den gelir
// (kişisel değişiklik: user_config.h); burada yalnız türlü sabitlere çevrilir.
#pragma once
#include <stdint.h>
#include "config.h"

// ST7789 240x240 (GMT130-V1.0, CS pini yok → SPI mode 3)
constexpr int PIN_TFT_SCK = CFG_PIN_TFT_SCK;    // VSPI SCLK (IO_MUX)
constexpr int PIN_TFT_MOSI = CFG_PIN_TFT_MOSI;  // VSPI MOSI (IO_MUX)
constexpr int PIN_TFT_DC = CFG_PIN_TFT_DC;
constexpr int PIN_TFT_RST = CFG_PIN_TFT_RST;
constexpr int PIN_TFT_BLK = CFG_PIN_TFT_BLK;    // Fixed HIGH by default; optional PWM

// MAX4466 çıkışı → ADC1 (3V3 besleme, bias ≈1,65 V)
constexpr int PIN_MIC = CFG_PIN_MIC;
// ESP32 GPIO → ADC1 kanalı (32→4, 33→5, 34→6, 35→7, 36→0, 37→1, 38→2, 39→3); geçersiz pin -1
constexpr int adc1ChannelOfGpio(int gpio) {
  return gpio >= 32 && gpio <= 35 ? gpio - 28 : (gpio >= 36 && gpio <= 39 ? gpio - 36 : -1);
}
static_assert(adc1ChannelOfGpio(PIN_MIC) >= 0, "CFG_PIN_MIC bir ADC1 pini olmalı (GPIO32..39)");

// WS2812B veri (74HC tampon üzerinden)
constexpr int PIN_LED = CFG_PIN_LED;

// Onboard BOOT (active LOW).
constexpr int PIN_BTN_BOOT = CFG_PIN_BTN_BOOT;  // yalnızca çalışma zamanında

// İsteğe bağlı donanım (kapalıyken hiçbir şey yapılmaz)
constexpr bool ADP_DETECT = CFG_ADP_DETECT != 0;
constexpr int PIN_ADP_OK = CFG_PIN_ADP_OK;      // GPIO35: giriş-only, dış bölücü; HIGH = adaptör
constexpr int PIN_IR = CFG_PIN_IR;              // -1 disables reception

namespace pin_safety {
constexpr bool gpio(int p) {
  return (p >= 0 && p <= 19) || (p >= 21 && p <= 23) ||
         (p >= 25 && p <= 27) || (p >= 32 && p <= 39);
}
constexpr bool external(int p) {
  return gpio(p) && !(p >= 6 && p <= 11) &&
         p != 0 && p != 2 && p != 4 && p != 5 && p != 12 && p != 15;
}
constexpr bool output(int p) { return external(p) && p < 34; }
constexpr int roles[] = {PIN_TFT_SCK, PIN_TFT_MOSI, PIN_TFT_DC, PIN_TFT_RST,
                        PIN_TFT_BLK, PIN_MIC, PIN_LED, PIN_IR,
                        ADP_DETECT ? PIN_ADP_OK : -1, PIN_BTN_BOOT};
constexpr bool unique() {
  for (unsigned i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
    for (unsigned j = i + 1; j < sizeof(roles) / sizeof(roles[0]); ++j)
      if (roles[i] != -1 && roles[i] == roles[j]) return false;
  return true;
}
}
static_assert(PIN_TFT_SCK == 18, "CFG_PIN_TFT_SCK must use VSPI IO_MUX GPIO18");
static_assert(PIN_TFT_MOSI == 23, "CFG_PIN_TFT_MOSI must use VSPI IO_MUX GPIO23");
static_assert(pin_safety::output(PIN_TFT_DC) && PIN_TFT_DC < 32, "CFG_PIN_TFT_DC requires an output below GPIO32");
static_assert(pin_safety::output(PIN_TFT_RST), "CFG_PIN_TFT_RST requires a safe output");
static_assert(pin_safety::output(PIN_TFT_BLK), "CFG_PIN_TFT_BLK requires a safe output");
static_assert(pin_safety::output(PIN_LED), "CFG_PIN_LED requires a safe output");
static_assert(PIN_BTN_BOOT == 0, "CFG_PIN_BTN_BOOT must remain onboard GPIO0");
static_assert(PIN_IR == -1 || pin_safety::external(PIN_IR), "CFG_PIN_IR requires a safe input or -1");
static_assert(!ADP_DETECT || pin_safety::external(PIN_ADP_OK), "CFG_PIN_ADP_OK requires a safe input");
static_assert(pin_safety::unique(), "CFG_PIN role collision: active pins must be unique");

// Ekran varsayılanları
constexpr int TFT_SPI_MHZ_DEFAULT = CFG_TFT_SPI_MHZ;
constexpr uint8_t TFT_ROTATION_DEFAULT = CFG_TFT_ROTATION;
constexpr int TFT_OFFSET_X_DEFAULT = CFG_TFT_OFFSET_X;
constexpr int TFT_OFFSET_Y_DEFAULT = CFG_TFT_OFFSET_Y;
constexpr uint8_t TFT_BACKLIGHT = CFG_TFT_BACKLIGHT;  // 0..255 (yalnız TFT_BL_PWM = true iken)
constexpr bool TFT_BL_PWM = CFG_TFT_BL_PWM != 0;      // false: arka ışık sabit HIGH (PWM gürültüsü yok)
constexpr int TFT_BL_FREQ = CFG_TFT_BL_FREQ;
// SPI SCK/MOSI/DC sürüş gücü (0=5 mA (varsayılan), 1=10, 2=20, 3=40 mA). Düşük değer kenarları yavaşlatır, ADC'ye
// bindirilen ekran gürültüsünü azaltır (40 MHz'de 0 → ≈ -5 dB); ekran bozulursa 2'ye döndürün. CLI: drive [0-3].
constexpr int TFT_DRIVE_CAP = CFG_TFT_DRIVE_CAP;  // kullanıcı görsel onayı (2026-10-10): 0 ile ekran düzgün, ADC gürültüsü ~%36 düşük

// Görev yerleşimi (D5)
constexpr int AUDIO_CORE = 0, AUDIO_PRIO = 10;
constexpr int DISPLAY_CORE = 1, DISPLAY_PRIO = 5;

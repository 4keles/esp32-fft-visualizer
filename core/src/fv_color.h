// fv_color.h — renk yardımcıları ve tema/mod isimleri.
#pragma once
#include "fv_types.h"

namespace fv {

enum Theme : uint8_t { TH_SPECTRUM = 0, TH_EMBER, TH_NEON, TH_FIRE, TH_CHROMA, TH_COUNT };

// Gökkuşağı HSV (FastLED "rainbow" benzeri, eşit algısal dağılım). h,s,v 0..255.
Rgb hsv8(uint8_t h, uint8_t s, uint8_t v);
// Wire-order (byte-swap edilmiş) RGB565: ESP32 DMA buffer'ına doğrudan yazılır.
static inline uint16_t rgb565w(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}
static inline uint16_t rgb565w(Rgb c) { return rgb565w(c.r, c.g, c.b); }
// Wire-order RGB565 → Rgb (simülatör/test için).
Rgb unpack565w(uint16_t w);
static inline Rgb rgb_scale(Rgb c, uint8_t s) { return Rgb{scale8(c.r, s), scale8(c.g, s), scale8(c.b, s)}; }
static inline Rgb rgb_add(Rgb a, Rgb b) {
  return Rgb{satu8(a.r + b.r), satu8(a.g + b.g), satu8(a.b + b.b)};
}
Rgb rgb_lerp(Rgb a, Rgb b, uint8_t t);  // t=0 → a, 255 → b

const char* theme_name(uint8_t th);

}  // namespace fv

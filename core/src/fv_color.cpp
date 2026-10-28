// fv_color.cpp — renk yardımcıları (tam sayı, deterministik).
#include "fv_color.h"
#include "fv_math.h"

namespace fv {

// FastLED hsv2rgb_rainbow benzeri: sarı bölgesi geniş, tonlar algısal olarak eşit aralıklı.
// Fark: V doğrusal ölçeklenir (FastLED video-dim karesi uygulamaz); gamma LED hattında zaten var.
Rgb hsv8(uint8_t h, uint8_t s, uint8_t v) {
  const uint8_t offset = h & 0x1F;                       // bölüm içi konum 0..31
  const uint8_t off8 = (uint8_t)(offset << 3);           // 0..248
  const uint8_t third = scale8(off8, 85);                // 0..82
  const uint8_t twothirds = scale8(off8, 170);           // 0..164
  uint8_t r, g, b;
  switch (h >> 5) {
    case 0:  r = 255 - third;      g = third;           b = 0;               break;  // kırmızı → turuncu
    case 1:  r = 171;              g = 85 + third;      b = 0;               break;  // turuncu → sarı
    case 2:  r = 171 - twothirds;  g = 170 + third;     b = 0;               break;  // sarı → yeşil
    case 3:  r = 0;                g = 255 - third;     b = third;           break;  // yeşil → turkuaz
    case 4:  r = 0;                g = 171 - twothirds; b = 85 + twothirds;  break;  // turkuaz → mavi
    case 5:  r = third;            g = 0;               b = 255 - third;     break;  // mavi → mor
    case 6:  r = 85 + third;       g = 0;               b = 171 - third;     break;  // mor → pembe
    default: r = 170 + third;      g = 0;               b = 85 - third;      break;  // pembe → kırmızı
  }
  if (s != 255) {
    if (s == 0) {
      r = g = b = 255;
    } else {
      // Beyaza doğru seyreltme: kanallar (s) ile ölçeklenir, kalan eklenir.
      uint8_t desat = 255 - s;
      desat = scale8(desat, desat);                      // video benzeri eğri: orta sat. daha canlı
      const uint8_t sc = 255 - desat;
      r = (uint8_t)(scale8(r, sc) + desat);
      g = (uint8_t)(scale8(g, sc) + desat);
      b = (uint8_t)(scale8(b, sc) + desat);
    }
  }
  if (v != 255) {
    r = scale8(r, v); g = scale8(g, v); b = scale8(b, v);
  }
  return Rgb{r, g, b};
}

Rgb unpack565w(uint16_t w) {
  const uint16_t c = (uint16_t)((w >> 8) | (w << 8));
  uint8_t r = (uint8_t)((c >> 8) & 0xF8), g = (uint8_t)((c >> 3) & 0xFC), b = (uint8_t)((c << 3) & 0xF8);
  // Düşük bitleri çoğalt: tam beyaz 255 olsun.
  r |= r >> 5; g |= g >> 6; b |= b >> 5;
  return Rgb{r, g, b};
}

// Uç noktalar tam: t=0 → a, t=255 → b (işaretsiz aritmetik; negatif bölme yok).
static inline uint8_t mix255(uint8_t a, uint8_t b, uint8_t t) {
  return (uint8_t)(((uint32_t)a * (255u - t) + (uint32_t)b * t + 127u) / 255u);
}
Rgb rgb_lerp(Rgb a, Rgb b, uint8_t t) {
  return Rgb{mix255(a.r, b.r, t), mix255(a.g, b.g, t), mix255(a.b, b.b, t)};
}

const char* theme_name(uint8_t th) {
  static const char* const n[TH_COUNT] = {"SPECTRUM", "EMBER", "NEON", "FIRE", "CHROMA"};
  return th < TH_COUNT ? n[th] : "?";
}

}  // namespace fv

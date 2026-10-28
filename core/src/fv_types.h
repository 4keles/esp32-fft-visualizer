// fv_types.h — temel tipler ve doygun (saturating) yardımcılar.
// Kural: core/ içinde yalnızca <stdint.h>/<stddef.h>; heap, STL, float (çalışma
// zamanında), exception, RTTI ve global constructor yasak.
#pragma once
#include <stdint.h>
#include <stddef.h>

// FV_HOT: sıcak döngüler ESP32'de IRAM'e (flash cache ıskası yok; diğer çekirdekteki ekran
// render'ı ile flash veriyolu paylaşılmaz). Host/WASM'da etkisiz; sonuçları değiştirmez.
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define FV_HOT IRAM_ATTR
#else
#define FV_HOT
#endif

namespace fv {

using q15  = int16_t;   // [-1, 1)
using u16n = uint16_t;  // UQ0.16: 0..65535 = [0, 1]
using lvl  = int32_t;   // 1/256 log2(güç) birimi; 1 dB = 85.0403 lvl

struct Rgb  { uint8_t r, g, b; };
struct cq15 { int16_t re, im; };

constexpr lvl dB(double x) { return lvl(x * 85.0403 + (x < 0 ? -0.5 : 0.5)); }
constexpr lvl LVL_NEG_INF = -(1 << 24);
constexpr lvl LVL_FS = 6806;  // tam ölçek sinüsün bant seviyesi (test ±dB(0.5) doğrular)

constexpr u16n U16N_ONE = 65535;

static inline int16_t sat16(int32_t x) {
  return x > 32767 ? 32767 : (x < -32768 ? -32768 : (int16_t)x);
}
static inline uint16_t satu16(int32_t x) {
  return x > 65535 ? 65535 : (x < 0 ? 0 : (uint16_t)x);
}
static inline uint8_t satu8(int32_t x) {
  return x > 255 ? 255 : (x < 0 ? 0 : (uint8_t)x);
}
// Q15 çarpım, yuvarlamalı.
static inline int16_t mulq15(int16_t a, int16_t b) {
  return (int16_t)(((int32_t)a * b + (1 << 14)) >> 15);
}
// UQ0.16 çarpım.
static inline u16n mulu16n(u16n a, u16n b) {
  return (u16n)(((uint32_t)a * b + 32768u) >> 16);
}
// 8 bit ölçekleme (FastLED scale8 eşdeğeri, 255 → değişmez).
static inline uint8_t scale8(uint8_t x, uint8_t s) {
  return (uint8_t)(((uint16_t)x * (uint16_t)(s + 1)) >> 8);
}
static inline int32_t iabs32(int32_t x) { return x < 0 ? -x : x; }
template <typename T> static inline T vmin(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T vmax(T a, T b) { return a > b ? a : b; }
template <typename T> static inline T vclamp(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

// FNV-1a 32 bit (golden hash'ler için).
static inline uint32_t fnv1a(const void* data, size_t n, uint32_t h = 2166136261u) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

}  // namespace fv

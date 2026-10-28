// fv_math.cpp — tamsayı log/exp/sin ve PRNG.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_math.h"
#include "fv_tables.h"

namespace fv {

FV_HOT lvl log2_l8(uint64_t x) {
  if (!x) return LVL_NEG_INF;
  int e = 63 - __builtin_clzll(x);
  // 9 bit mantis → en yakına yuvarla (1.m: m 0..256; 256 → üst oktav)
  uint32_t m9 = (uint32_t)((e >= 9 ? (x >> (e - 9)) : (x << (9 - e))) & 511u);
  uint32_t m = (m9 + 1u) >> 1;
  if (m == 256u) { ++e; m = 0; }
  return (lvl)(e * 256 + tab::log2frac[m]);
}

uint32_t exp2_l8(lvl l) {
  if (l >= 32 * 256) return 0xFFFFFFFFu;
  int i = l >> 8;  // aritmetik kaydırma = floor (GCC/clang/xtensa)
  uint64_t m = 65536u + tab::exp2frac[l & 255];  // Q16, [1,2)
  if (i >= 16) return (uint32_t)(m << (i - 16));
  int sh = 16 - i;
  if (sh >= 40) return 0;
  return (uint32_t)((m + ((uint64_t)1 << (sh - 1))) >> sh);
}

// Çeyrek tablo üzerinden tam tur: idx 0..1023.
static inline int32_t sin1024(uint32_t idx) {
  idx &= 1023u;
  uint32_t q = idx & 255u;
  switch (idx >> 8) {
    case 0: return tab::sin_q15[q];
    case 1: return tab::sin_q15[256 - q];
    case 2: return -tab::sin_q15[q];
    default: return -tab::sin_q15[256 - q];
  }
}

int16_t sin16(uint16_t ph) {
  uint32_t i = ph >> 6, f = ph & 63u;
  int32_t a = sin1024(i), b = sin1024(i + 1);
  return (int16_t)(a + (((b - a) * (int32_t)f + 32) >> 6));
}

uint8_t sin8(uint8_t ph) {
  int32_t s = sin1024((uint32_t)ph << 2);  // ±32767
  return (uint8_t)(128 + ((s * 127 + (s >= 0 ? 16384 : -16384)) / 32768));
}

uint32_t xorshift32(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

}  // namespace fv

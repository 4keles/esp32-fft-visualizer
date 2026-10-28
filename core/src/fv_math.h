// fv_math.h — tamsayı matematik yardımcıları.
#pragma once
#include "fv_types.h"

namespace fv {

// 256 * log2(x). x == 0 → LVL_NEG_INF. clz + 256 girişli kesir LUT.
lvl log2_l8(uint64_t x);
// 2^(l/256), l >= 0 için; l < 0 → 0 değil, kesirli sonuç yuvarlanır. Taşmada UINT32_MAX.
uint32_t exp2_l8(lvl l);
// Sinüs: ph 0..255 bir tam tur; dönüş 0..255, 128 = sıfır geçiş.
uint8_t sin8(uint8_t ph);
// Q15 sinüs: ph 0..65535 bir tam tur.
int16_t sin16(uint16_t ph);
// Deterministik PRNG (durum 0 olmamalı).
uint32_t xorshift32(uint32_t& s);
// 8 bit doğrusal karışım: t=0 → a, t=255 → b.
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
  return (uint8_t)(a + (((int32_t)b - a) * t + 127) / 255);
}

}  // namespace fv

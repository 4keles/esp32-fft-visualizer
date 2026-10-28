// fv_tables.h — DSP tabloları (ÜRETİLİR: tools/gen_tables.cpp → fv_tables_gen.cpp).
// Görsel tablolar ayrı: fv_vis_tables.h.
#pragma once
#include "fv_config.h"

namespace fv {
namespace tab {

extern const int16_t  hann1024[FFT_N / 2 + 1];    // periyodik Hann w[0..512] (Q15, w[512]=32767); w[N-n]=w[n]
extern const int16_t  sin_q15[FFT_N / 4 + 1];     // çeyrek sinüs, 1024 adım/tam daire (Q15)
extern const uint16_t bitrev512[FFT_N / 2];       // 9 bit bit-reverse
extern const uint8_t  log2frac[256];              // 256*log2(1 + i/256) (yuvarlanmış)
extern const uint16_t exp2frac[256];              // 65536*(2^(i/256) - 1) (yuvarlanmış)
extern const int16_t  halfband[(HALFBAND_TAPS + 1) / 4 + 1];  // [0..11]: merkezden ±1,±3..±23 katsayıları, [12]: merkez (Q15)
extern const int16_t  noiseSub[128];              // 256*log2(1 - 2^-d) (lvl), d = i*8 lvl; [0] kırpılmış
extern const int16_t  logAdd[128];                // 256*log2(1 + 2^-d) (lvl), d = i*8 lvl

}  // namespace tab
}  // namespace fv

// fv_vis_tables.h — görsel tablolar (ÜRETİLİR: tools/gen_vis_tables.cpp → fv_vis_tables_gen.cpp).
#pragma once
#include "fv_config.h"

namespace fv {
namespace vtab {

extern const uint8_t gamma22[256];     // gamma 2.2
extern const uint16_t gamma22w[256];   // gamma 2.2, 16 bit (dither için)
extern const uint8_t cpBright[256];    // cp(e) = 3 + 252*((1+19e)/20)^2, e = i/255
extern const uint8_t heatPal[256][3];  // siyah→kırmızı→sarı→beyaz
extern const uint8_t perm120[NUM_LEDS];// Fisher–Yates (xorshift32, seed 0xC0DE)
extern const uint8_t font5x7[96][5];   // ASCII 32..127, sütun bazlı, LSB üstte

}  // namespace vtab
}  // namespace fv

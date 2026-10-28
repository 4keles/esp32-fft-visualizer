// ÜRETİLMİŞ DOSYA — elle düzenlemeyin. Kaynak: tools/gen_golden.cpp (--header)
// Firmware self-test DSP adımı: scenario_hash(sc, GOLDEN_FRAMES, GOLDEN_SEED, GH_FEATURES)
// değerini GOLDEN_FEATURES[sc] ile karşılaştırır (host = WASM = ESP32 bit-exact).
#pragma once
#include "fv_synth.h"

namespace fv {

constexpr int GOLDEN_FRAMES = 240;
constexpr uint32_t GOLDEN_SEED = 1;
constexpr bool GOLDEN_ALL_STABLE = true;
constexpr uint32_t GOLDEN_FEATURES[SC_COUNT] = {
    0xD46196C2u,  // silence
    0x1F52DD54u,  // sine1k
    0x612637DEu,  // sweep
    0x43307D3Eu,  // kick120
    0xAA0056ECu,  // mix
    0x21DECDE2u,  // pink
    0x47567698u,  // tone100
};
constexpr uint32_t GOLDEN_ALL[SC_COUNT] = {
    0x78728748u,  // silence
    0x04296D0Au,  // sine1k
    0xAB422D48u,  // sweep
    0xD69CF365u,  // kick120
    0x3EBC2AF8u,  // mix
    0x0F51F645u,  // pink
    0x92FE68B7u,  // tone100
};

}  // namespace fv

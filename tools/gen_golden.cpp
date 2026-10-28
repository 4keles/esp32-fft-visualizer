// gen_golden.cpp — senaryo golden hash'lerini üretir (host aracı, fvcore'a bağlanır).
// Kullanım:
//   gen_golden --json [--all-stable] > tests/golden/golden.json
//   gen_golden --header [--all-stable] > core/src/fv_golden_gen.h
// GH_FEATURES hash'leri yalnız DSP'ye bağlıdır ("stable": true). GH_ALL ayrıca LED ve ekran
// çıktısını içerir; görsel çekirdek kesinleşince orkestratör --all-stable ile yeniden üretir.
#include <cstdio>
#include <cstring>
#include "fftviz.h"

using namespace fv;

static constexpr int FRAMES = 240;
static constexpr uint32_t SEED = 1;
// `leds` parametresi golden'ı: MIX senaryosu, GH_LEDS, bu uzunluklar × tüm modlar (host = WASM = ESP32 paritesi).
static constexpr int LP_FRAMES = 120;
static constexpr int kLpLeds[] = {1, 37, 60, 180, 300};

int main(int argc, char** argv) {
  bool json = false, header = false, allStable = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--json")) json = true;
    else if (!std::strcmp(argv[i], "--header")) header = true;
    else if (!std::strcmp(argv[i], "--all-stable")) allStable = true;
    else { std::fprintf(stderr, "bilinmeyen argüman: %s\n", argv[i]); return 2; }
  }
  if (json == header) {
    std::fprintf(stderr, "kullanım: gen_golden --json|--header [--all-stable]\n");
    return 2;
  }
  uint32_t feat[SC_COUNT], all[SC_COUNT];
  for (int sc = 0; sc < SC_COUNT; ++sc) {
    feat[sc] = scenario_hash((Scenario)sc, FRAMES, SEED, GH_FEATURES);
    all[sc] = scenario_hash((Scenario)sc, FRAMES, SEED, GH_ALL);
  }
  if (json) {
    std::printf("{\n  \"frames\": %d,\n  \"seed\": %u,\n", FRAMES, SEED);
    const char* sec[2] = {"features", "all"};
    const uint32_t* val[2] = {feat, all};
    const bool stable[2] = {true, allStable};
    for (int s = 0; s < 2; ++s) {
      std::printf("  \"%s\": {\n    \"stable\": %s,\n    \"hashes\": {\n", sec[s], stable[s] ? "true" : "false");
      for (int sc = 0; sc < SC_COUNT; ++sc)
        std::printf("      \"%s\": %u%s\n", scenario_name((uint8_t)sc), val[s][sc], sc + 1 < SC_COUNT ? "," : "");
      std::printf("    }\n  },\n");
    }
    std::printf("  \"leds_param\": {\n    \"frames\": %d,\n    \"scenario\": \"mix\",\n    \"hashes\": {\n", LP_FRAMES);
    const int nl = (int)(sizeof(kLpLeds) / sizeof(kLpLeds[0]));
    for (int li = 0; li < nl; ++li)
      for (int m = 0; m < LM_COUNT; ++m)
        std::printf("      \"L%d_%s\": %u%s\n", kLpLeds[li], led_mode_name((uint8_t)m),
                    scenario_hash(SC_MIX, LP_FRAMES, SEED, GH_LEDS, (uint8_t)m, 0, 0, kLpLeds[li]),
                    (li + 1 == nl && m + 1 == LM_COUNT) ? "" : ",");
    std::printf("    }\n  }\n}\n");
  } else {
    std::printf("// ÜRETİLMİŞ DOSYA — elle düzenlemeyin. Kaynak: tools/gen_golden.cpp (--header)\n");
    std::printf("// Firmware self-test DSP adımı: scenario_hash(sc, GOLDEN_FRAMES, GOLDEN_SEED, GH_FEATURES)\n");
    std::printf("// değerini GOLDEN_FEATURES[sc] ile karşılaştırır (host = WASM = ESP32 bit-exact).\n");
    std::printf("#pragma once\n#include \"fv_synth.h\"\n\nnamespace fv {\n\n");
    std::printf("constexpr int GOLDEN_FRAMES = %d;\nconstexpr uint32_t GOLDEN_SEED = %u;\n", FRAMES, SEED);
    std::printf("constexpr bool GOLDEN_ALL_STABLE = %s;\n", allStable ? "true" : "false");
    const char* nm[2] = {"GOLDEN_FEATURES", "GOLDEN_ALL"};
    const uint32_t* val[2] = {feat, all};
    for (int s = 0; s < 2; ++s) {
      std::printf("constexpr uint32_t %s[SC_COUNT] = {\n", nm[s]);
      for (int sc = 0; sc < SC_COUNT; ++sc)
        std::printf("    0x%08Xu,  // %s\n", val[s][sc], scenario_name((uint8_t)sc));
      std::printf("};\n");
    }
    std::printf("\n}  // namespace fv\n");
  }
  return 0;
}

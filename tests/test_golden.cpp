// test_golden: scenario_hash değerleri tests/golden/golden.json ve fv_golden_gen.h ile aynı mı.
// GH_FEATURES her zaman; GH_ALL yalnız golden.json'da "all": {"stable": true} ise denetlenir.
#include <string>
#include "fvtest.h"
#include "fftviz.h"
#include "fv_golden_gen.h"

using namespace fv;

static std::string slurp(const char* path) {
  std::string s;
  FILE* f = std::fopen(path, "rb");
  if (!f) return s;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  std::fclose(f);
  return s;
}

// "section": { ... } bloğunu (iç içe süslü parantez sayarak) döndürür.
static std::string section(const std::string& js, const char* name) {
  size_t k = js.find(std::string("\"") + name + "\"");
  if (k == std::string::npos) return "";
  size_t open = js.find('{', k);
  if (open == std::string::npos) return "";
  int depth = 0;
  for (size_t i = open; i < js.size(); ++i) {
    if (js[i] == '{') ++depth;
    else if (js[i] == '}' && --depth == 0) return js.substr(open, i - open + 1);
  }
  return "";
}

static bool number_of(const std::string& sec, const char* key, unsigned long long* out) {
  size_t k = sec.find(std::string("\"") + key + "\"");
  if (k == std::string::npos) return false;
  size_t c = sec.find(':', k);
  if (c == std::string::npos) return false;
  return std::sscanf(sec.c_str() + c + 1, " %llu", out) == 1;
}

static bool is_stable(const std::string& sec) {
  size_t k = sec.find("\"stable\"");
  if (k == std::string::npos) return false;
  size_t c = sec.find(':', k);
  return c != std::string::npos && sec.compare(sec.find_first_not_of(" \t\r\n", c + 1), 4, "true") == 0;
}

TEST(golden_features) {
  std::string js = slurp(FV_SOURCE_DIR "/tests/golden/golden.json");
  CHECK(!js.empty());
  unsigned long long frames = 0, seed = 0;
  CHECK(number_of(js, "frames", &frames) && frames == (unsigned long long)GOLDEN_FRAMES);
  CHECK(number_of(js, "seed", &seed) && seed == GOLDEN_SEED);
  std::string feat = section(js, "features");
  CHECK(is_stable(feat));
  std::string hashes = section(feat, "hashes");
  for (int sc = 0; sc < SC_COUNT; ++sc) {
    uint32_t h = scenario_hash((Scenario)sc, GOLDEN_FRAMES, GOLDEN_SEED, GH_FEATURES);
    unsigned long long want = 0;
    bool ok = number_of(hashes, scenario_name((uint8_t)sc), &want);
    CHECK(ok);
    if (h != want) std::printf("    %s: hesaplanan %u, golden.json %llu\n", scenario_name((uint8_t)sc), h, want);
    CHECK_EQ((unsigned long long)h, want);
    CHECK_EQ(h, GOLDEN_FEATURES[sc]);  // firmware'e gömülen başlık güncel mi
  }
}

TEST(golden_all_if_stable) {
  std::string js = slurp(FV_SOURCE_DIR "/tests/golden/golden.json");
  std::string all = section(js, "all");
  CHECK(!all.empty());
  if (!is_stable(all)) {
    std::printf("    GH_ALL kararsız olarak işaretli; atlandı\n");
    return;
  }
  std::string hashes = section(all, "hashes");
  for (int sc = 0; sc < SC_COUNT; ++sc) {
    uint32_t h = scenario_hash((Scenario)sc, GOLDEN_FRAMES, GOLDEN_SEED, GH_ALL);
    unsigned long long want = 0;
    CHECK(number_of(hashes, scenario_name((uint8_t)sc), &want));
    CHECK_EQ((unsigned long long)h, want);
    if (GOLDEN_ALL_STABLE) CHECK_EQ(h, GOLDEN_ALL[sc]);
  }
}

// `leds` parametresi: MIX senaryosu, GH_LEDS, farklı uzunluk × mod (WASM testi de aynı bölümü doğrular).
TEST(golden_leds_param) {
  std::string js = slurp(FV_SOURCE_DIR "/tests/golden/golden.json");
  std::string sec = section(js, "leds_param");
  CHECK(!sec.empty());
  unsigned long long frames = 0;
  CHECK(number_of(sec, "frames", &frames));
  std::string hashes = section(sec, "hashes");
  const int lens[] = {1, 37, 60, 180, 300};
  int checked = 0;
  for (int n : lens) {
    for (int m = 0; m < LM_COUNT; ++m) {
      char key[40]; std::snprintf(key, sizeof key, "L%d_%s", n, led_mode_name((uint8_t)m));
      unsigned long long want = 0;
      CHECK(number_of(hashes, key, &want));
      const uint32_t h = scenario_hash(SC_MIX, (int)frames, GOLDEN_SEED, GH_LEDS, (uint8_t)m, 0, 0, n);
      if (h != want) std::printf("    %s: hesaplanan %u, golden.json %llu\n", key, h, want);
      CHECK_EQ((unsigned long long)h, want);
      ++checked;
    }
  }
  CHECK_EQ(checked, 45);
}

// Çağıranın çalışma alanıyla çalışan aşırı yükleme statik sürümle birebir aynı olmalı.
TEST(work_overload_matches) {
  ScenarioHashWork* w = new ScenarioHashWork();
  for (int sc = 0; sc < SC_COUNT; ++sc) {
    CHECK_EQ(scenario_hash(*w, (Scenario)sc, 64, 3, GH_ALL), scenario_hash((Scenario)sc, 64, 3, GH_ALL));
  }
  delete w;
}

FV_TEST_MAIN()

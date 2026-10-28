// Drift testi: gen_vis_tables çıktısı commit'teki fv_vis_tables_gen.cpp ile aynı mı + tablo sağlamlığı.
#include <string>
#include "fvtest.h"
#include "fftviz.h"

static std::string slurp(FILE* f) {
  std::string s; char buf[4096]; size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  return s;
}

TEST(vis_tables_drift) {
  FILE* p = popen(FV_BINARY_DIR "/gen_vis_tables", "r");
  CHECK(p != nullptr);
  if (!p) return;
  std::string gen = slurp(p);
  pclose(p);
  FILE* c = std::fopen(FV_SOURCE_DIR "/core/src/fv_vis_tables_gen.cpp", "rb");
  CHECK(c != nullptr);
  if (!c) return;
  std::string committed = slurp(c);
  std::fclose(c);
  CHECK(gen.size() > 1000);
  CHECK(gen == committed);
}

TEST(vis_tables_sane) {
  using namespace fv::vtab;
  for (int i = 1; i < 256; ++i) {
    CHECK(gamma22[i] >= gamma22[i - 1]);
    CHECK(gamma22w[i] >= gamma22w[i - 1]);
    CHECK(cpBright[i] >= cpBright[i - 1]);
  }
  CHECK_EQ(gamma22[0], 0); CHECK_EQ(gamma22[255], 255); CHECK_EQ(gamma22w[255], 65535);
  CHECK_EQ(cpBright[255], 255);
  CHECK(cpBright[0] >= 3 && cpBright[0] <= 5);
  // cp(0.5): 3 + 252*(10.5/20)^2
  CHECK_NEAR(cpBright[128], 3 + 252 * 0.5253 * 0.5253, 1.5);
  CHECK(heatPal[0][0] == 0 && heatPal[0][1] == 0 && heatPal[0][2] == 0);
  CHECK(heatPal[255][0] == 255 && heatPal[255][1] == 255 && heatPal[255][2] == 255);
  CHECK(heatPal[100][0] > heatPal[100][1] && heatPal[100][1] >= heatPal[100][2]);  // kırmızı baskın
  bool seen[fv::NUM_LEDS] = {};
  for (int i = 0; i < fv::NUM_LEDS; ++i) { CHECK(perm120[i] < fv::NUM_LEDS); seen[perm120[i]] = true; }
  for (int i = 0; i < fv::NUM_LEDS; ++i) CHECK(seen[i]);
  int moved = 0; for (int i = 0; i < fv::NUM_LEDS; ++i) moved += perm120[i] != i;
  CHECK(moved > 100);
  // font: boşluk boş; her yazdırılabilir karakter (boşluk hariç) en az bir piksel içerir
  for (int j = 0; j < 5; ++j) CHECK_EQ(font5x7[0][j], 0);
  for (int c = 1; c < 96; ++c) {
    int any = 0; for (int j = 0; j < 5; ++j) any |= font5x7[c][j];
    CHECK(any != 0);
  }
  CHECK_EQ(font5x7['A' - 32][2], 0x11);
  // seed 0xC0DE xorshift32 ile aynı permütasyon
  uint8_t p[120]; for (int i = 0; i < 120; ++i) p[i] = (uint8_t)i;
  uint32_t s = 0xC0DE;
  for (int i = 119; i > 0; --i) { int j = (int)(fv::xorshift32(s) % (uint32_t)(i + 1)); uint8_t t = p[i]; p[i] = p[j]; p[j] = t; }
  for (int i = 0; i < 120; ++i) CHECK_EQ(p[i], perm120[i]);
}
FV_TEST_MAIN()

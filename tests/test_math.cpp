// test_math: log2_l8 / exp2_l8 / sin8 / sin16 / xorshift32 doğruluğu.
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;

TEST(log2_l8_exact_powers) {
  CHECK_EQ(log2_l8(0), LVL_NEG_INF);
  CHECK_EQ(log2_l8(1), 0);
  for (int e = 0; e < 64; ++e) CHECK_EQ(log2_l8((uint64_t)1 << e), 256 * e);
}

TEST(log2_l8_accuracy) {
  // Yuvarlanmış 8 bit mantis + yuvarlanmış LUT → |hata| ≤ ~1.22 lvl (0.014 dB).
  double worst = 0;
  uint32_t s = 12345;
  for (int i = 0; i < 200000; ++i) {
    uint64_t hi = xorshift32(s), lo = xorshift32(s);
    uint64_t x = (hi << 32 | lo) >> (xorshift32(s) & 63);
    if (!x) continue;
    double err = std::fabs(log2_l8(x) - 256.0 * std::log2((double)x));
    if (err > worst) worst = err;
  }
  CHECK(worst <= 1.25);
  METRIC("max_err_lvl", worst);
}

TEST(log2_l8_monotonic) {
  lvl prev = log2_l8(1);
  int bad = 0;
  for (uint64_t x = 2; x < 70000; ++x) { lvl l = log2_l8(x); bad += l < prev; prev = l; }
  CHECK_EQ(bad, 0);
}

TEST(exp2_l8_accuracy) {
  CHECK_EQ(exp2_l8(0), 1u);
  CHECK_EQ(exp2_l8(256 * 10), 1024u);
  CHECK_EQ(exp2_l8(256 * 32), 0xFFFFFFFFu);
  CHECK_EQ(exp2_l8(1 << 20), 0xFFFFFFFFu);
  CHECK_EQ(exp2_l8(-256 * 40), 0u);
  CHECK_EQ(exp2_l8(-256), 1u);         // 0.5 → yuvarlanır
  CHECK_EQ(exp2_l8(-512 - 1), 0u);     // 0.249
  // Hata = tamsayı yuvarlaması (≤0.5) + Q16 mantis (≤8e-6 göreli).
  double worst = 0;
  int bad = 0;
  for (lvl l = -256 * 4; l < 256 * 32; l += 7) {
    double ref = std::pow(2.0, l / 256.0);
    double err = std::fabs(exp2_l8(l) - ref);
    bad += err > 0.5 + ref * 1e-5;
    if (ref >= 65536.0) worst = std::fmax(worst, err / ref);
  }
  CHECK_EQ(bad, 0);
  CHECK(worst < 1e-5);
  METRIC("max_rel_err", worst);
  // log2 ∘ exp2 ≈ özdeşlik
  bad = 0;
  for (lvl l = 256 * 8; l < 256 * 30; l += 37) bad += std::abs(log2_l8(exp2_l8(l)) - l) > 2;
  CHECK_EQ(bad, 0);
}

TEST(sin16_accuracy) {
  double worst = 0;
  for (int ph = 0; ph < 65536; ++ph) {
    double ref = 32767.0 * std::sin(2.0 * M_PI * ph / 65536.0);
    double e = std::fabs(sin16((uint16_t)ph) - ref);
    if (e > worst) worst = e;
  }
  CHECK(worst <= 2.0);
  CHECK_EQ(sin16(0), 0);
  CHECK_EQ(sin16(16384), 32767);
  CHECK_EQ(sin16(49152), -32767);
  METRIC("max_err_lsb", worst);
}

TEST(sin8_shape) {
  CHECK_EQ(sin8(0), 128);
  CHECK_EQ(sin8(64), 255);
  CHECK_EQ(sin8(128), 128);
  CHECK_EQ(sin8(192), 1);
  double worst = 0;
  for (int ph = 0; ph < 256; ++ph) {
    double ref = 128.0 + 127.0 * std::sin(2.0 * M_PI * ph / 256.0);
    worst = std::fmax(worst, std::fabs(sin8((uint8_t)ph) - ref));
  }
  CHECK(worst <= 1.0);
}

TEST(xorshift32_sequence) {
  uint32_t s = 1;
  CHECK_EQ(xorshift32(s), 270369u);
  CHECK_EQ(xorshift32(s), 67634689u);
  CHECK_EQ(xorshift32(s), 2647435461u);
}

FV_TEST_MAIN()

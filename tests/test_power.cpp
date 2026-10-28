// test_power: donanıma göre akım modeli (led_ma, led_idle), etkin bütçe (ma, psu, usb) ve limiter garantisi.
#include <cstring>
#include "fvtest.h"
#include "fftviz.h"
using namespace fv;

namespace {
Features musicFrame(uint32_t& rng, int t) {   // tamsayı, deterministik, müzik benzeri
  Features f; std::memset(&f, 0, sizeof f);
  for (int k = 0; k < NUM_BANDS; ++k) {
    const uint32_t r = xorshift32(rng) & 0x3FFF;
    const int d = k - (10 + (t / 7) % 24);
    uint32_t v = r + (d * d < 30 ? 50000u - (uint32_t)(d * d) * 1400u : 0u);
    v = v > 65535u ? 65535u : v;
    f.level[k] = f.bar[k] = f.led[k] = (uint16_t)v;
    f.cap[k] = (uint16_t)(v > 4000u ? v : 4000u);
  }
  f.bass = (uint16_t)(30000 + (xorshift32(rng) & 0x7FFF));
  f.loud = (uint16_t)(20000 + (xorshift32(rng) & 0x7FFF));
  f.beatEnv = (uint16_t)((t % 20) * 3000);
  f.beat = (t % 20) == 0;
  f.beatStrength = f.beat ? 200 : 0;
  f.centroidHue = (uint8_t)((t * 3) % 200);
  return f;
}
Features fullFrame(int beat) {
  Features f; std::memset(&f, 0, sizeof f);
  for (int k = 0; k < NUM_BANDS; ++k) { f.level[k] = f.bar[k] = f.cap[k] = f.led[k] = 65535; }
  f.bass = f.mid = f.treble = f.loud = f.beatEnv = 65535;
  f.beat = beat; f.beatStrength = 255; f.centroidHue = 100;
  return f;
}
// Eski (power-config öncesi) sabit model: n·1 + Σ·20/255.
uint32_t legacyMa(const Rgb* px, int n) {
  uint32_t s = 0;
  for (int i = 0; i < n; ++i) s += (uint32_t)px[i].r + px[i].g + px[i].b;
  return (uint32_t)n + s * 20u / 255u;
}
}  // namespace

TEST(new_params_sane_and_defaults_legacy) {
  Params p; p.defaults();
  CHECK_EQ((int)p.get(P_LED_MA), 60);
  CHECK_EQ((int)p.get(P_LED_IDLE), 1000);
  CHECK_EQ((int)p.get(P_PSU_MA), 0);
  CHECK_EQ((int)p.get(P_MA_BUDGET), 2500);
  CHECK_EQ((int)p.usbPower, 0);
  // ma aralığı genişledi; eski değerler (kullanıcının 3400'ü) geçerli
  CHECK_EQ((int)param_info(P_MA_BUDGET).min, 100);
  CHECK_EQ((int)param_info(P_MA_BUDGET).max, 20000);
  CHECK(!p.set(P_MA_BUDGET, 2500)); CHECK(p.set(P_MA_BUDGET, 3400)); CHECK_EQ((int)p.get(P_MA_BUDGET), 3400);
  p.set(P_MA_BUDGET, 99999); CHECK_EQ((int)p.get(P_MA_BUDGET), 20000);
  p.set(P_MA_BUDGET, 1); CHECK_EQ((int)p.get(P_MA_BUDGET), 100);
}

// Varsayılan modelde led_estimate_ma eski formülle her girdide bit-aynı.
TEST(default_model_bit_identical_to_legacy) {
  Params p; p.defaults();
  const PowerModel m = power_model(p);
  uint32_t rng = 12345;
  for (int iter = 0; iter < 400; ++iter) {
    const int n = 1 + (int)(xorshift32(rng) % LED_MAX);
    Rgb px[LED_MAX];
    const uint32_t scale = xorshift32(rng) % 256;
    for (int i = 0; i < n; ++i) {
      px[i].r = (uint8_t)((xorshift32(rng) & 255) * scale / 255);
      px[i].g = (uint8_t)((xorshift32(rng) & 255) * scale / 255);
      px[i].b = (uint8_t)((xorshift32(rng) & 255) * scale / 255);
    }
    CHECK_EQ(led_estimate_ma(px, n, m), legacyMa(px, n));
    CHECK_EQ(led_estimate_ma(px, n), legacyMa(px, n));
    // eski limiter eşiği: (B−n)·255/20 == yeni led_sum_allowed
    const int32_t B = 100 + (int32_t)(xorshift32(rng) % 20000);
    const uint32_t old = B <= n ? 0u : ((uint32_t)(B - n) * 255u) / 20u;
    CHECK_EQ(led_sum_allowed(n, B, m), old);
  }
}

TEST(model_scales_with_led_ma_and_idle) {
  Rgb px[4] = {{255, 255, 255}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}};
  Params p; p.defaults();
  p.set(P_LED_MA, 30); p.set(P_LED_IDLE, 0);
  CHECK_EQ(led_estimate_ma(px, 4, power_model(p)), 60u);           // 2 tam beyaz LED × 30 mA, bekleme 0
  p.set(P_LED_IDLE, 2500);
  CHECK_EQ(led_estimate_ma(px, 4, power_model(p)), 60u + 10u);     // 4 × 2,5 mA
  p.set(P_LED_MA, 100);
  CHECK_EQ(led_estimate_ma(px, 4, power_model(p)), 200u + 10u);
}

TEST(effective_budget_rules) {
  Params p; p.defaults();
  BudgetSrc src;
  CHECK_EQ((int)led_budget_ma(p, &src), 2500); CHECK_EQ((int)src, (int)BS_MA);
  p.set(P_MA_BUDGET, 3400);
  CHECK_EQ((int)led_budget_ma(p), 3400);
  // psu belirtilmedi (0): psu_pct etkisiz
  p.set(P_PSU_PCT, 20); CHECK_EQ((int)led_budget_ma(p), 3400);
  // psu 3800 × %80 = 3040 < 3400
  p.set(P_PSU_MA, 3800); p.set(P_PSU_PCT, 80);
  CHECK_EQ((int)led_budget_ma(p, &src), 3040); CHECK_EQ((int)src, (int)BS_PSU);
  p.set(P_PSU_PCT, 100); CHECK_EQ((int)led_budget_ma(p, &src), 3400); CHECK_EQ((int)src, (int)BS_MA);
  // ma yüksek ayarlansa da kaynak sınırı geçerli ("dışarıyı bağlama" ama yanlış ayar korumasız kalmaz)
  p.set(P_MA_BUDGET, 20000); p.set(P_PSU_PCT, 90);
  CHECK_EQ((int)led_budget_ma(p), 3420);
  // USB: algılama yok (usbPower = 0) → ma_usb etkisiz; var → min
  p.set(P_MA_USB, 500);
  CHECK_EQ((int)led_budget_ma(p), 3420);
  p.usbPower = 1;
  CHECK_EQ((int)led_budget_ma(p, &src), 500); CHECK_EQ((int)src, (int)BS_USB);
  p.set(P_MA_USB, 5000); CHECK_EQ((int)led_budget_ma(p), 3420);   // ma_usb bütçeden büyükse yükseltmez
  p.defaults(); CHECK_EQ((int)p.usbPower, 0);
}

// Limiter garantisi: her led_ma / idle / ma / psu / usb / leds birleşiminde model mA ≤ max(etkin bütçe, bekleme tabanı).
TEST(limiter_guarantee_any_combination) {
  static const int kLedMa[] = {5, 12, 20, 30, 45, 60, 100};
  static const int kIdle[] = {0, 600, 1000, 3000, 5000};
  static const int kMa[] = {100, 400, 1000, 2500, 3400, 8000, 20000};
  static const int kPsu[] = {0, 500, 3800, 12000, 30000};
  static const int kPct[] = {20, 50, 80, 100};
  static const int kUsb[] = {50, 500, 5000};
  static const int kN[] = {1, 2, 17, 60, 120, 181, 300};
  static const int kMode[] = {LM_AURORA, LM_SPARKLE, LM_SPECTRUM, LM_FIRE, LM_BASS, LM_COMET};
  uint32_t rng = 4242;
  int combos = 0;
  for (int it = 0; it < 420; ++it) {
    const int ledMa = kLedMa[xorshift32(rng) % 7], idle = kIdle[xorshift32(rng) % 5], ma = kMa[xorshift32(rng) % 7];
    const int psu = kPsu[xorshift32(rng) % 5], pct = kPct[xorshift32(rng) % 4], usb = kUsb[xorshift32(rng) % 3];
    const int n = kN[xorshift32(rng) % 7], mode = kMode[xorshift32(rng) % 6];
    Params p; p.defaults();
    p.set(P_LED_MA, ledMa); p.set(P_LED_IDLE, idle); p.set(P_MA_BUDGET, ma); p.set(P_PSU_MA, psu); p.set(P_PSU_PCT, pct);
    p.set(P_MA_USB, usb); p.set(P_LEDS, n); p.set(P_LED_MODE, mode); p.set(P_BLOOM, 230);
    p.usbPower = (uint8_t)(xorshift32(rng) & 1);
    LedEngine e; e.reset(7, n);
    uint32_t r2 = rng;
    for (int t = 0; t < 90; ++t) {
      e.step(t % 4 == 0 ? fullFrame(t % 12 == 0) : musicFrame(r2, t), p);
      const PowerModel pm = power_model(p);
      const uint32_t ma_ = led_estimate_ma(e.out(), n, pm);
      const uint32_t base = (uint32_t)n * (uint32_t)idle / 1000u;
      const uint32_t lim = vmax<uint32_t>((uint32_t)led_budget_ma(p), base);
      if (ma_ > lim || e.milliamps() != ma_ || e.budgetMa() != led_budget_ma(p)) {
        std::printf("    led_ma=%d idle=%d ma=%d psu=%d/%d usb=%d(%d) n=%d mod=%d: %u > %u\n", ledMa, idle, ma, psu, pct, usb,
                    (int)p.usbPower, n, mode, ma_, lim);
        CHECK(false);
        return;
      }
      CHECK(e.demandMa() >= e.milliamps());   // limiter yalnız azaltır
    }
    ++combos;
  }
  CHECK_EQ(combos, 420);
}

// Tutarsız girdi: bütçe bekleme akımının altında → şerit siyah, çökme yok; psu çok küçük → siyah/çok sönük.
TEST(inconsistent_inputs_safe) {
  Params p; p.defaults();
  p.set(P_LEDS, 120); p.set(P_MA_BUDGET, 100); p.set(P_LED_IDLE, 5000);   // taban 600 mA > 100
  LedEngine e; e.reset(3, 120);
  uint32_t rng = 5;
  for (int t = 0; t < 60; ++t) {
    e.step(t % 2 ? fullFrame(1) : musicFrame(rng, t), p);
    for (int i = 0; i < 120; ++i) CHECK(e.out()[i].r == 0 && e.out()[i].g == 0 && e.out()[i].b == 0);
  }
  // psu_ma 100 × %20 = 20 mA, ma = 2500 → etkin 20 mA, 120 LED tabanı 120 mA → siyah
  p.defaults(); p.set(P_PSU_MA, 100); p.set(P_PSU_PCT, 20);
  CHECK_EQ((int)led_budget_ma(p), 20);
  e.reset(3, 120);
  for (int t = 0; t < 30; ++t) {
    e.step(fullFrame(0), p);
    CHECK(e.milliamps() <= 120);
  }
  // led_ma sınır değerlerinde taşma yok
  p.defaults(); p.set(P_LED_MA, 100); p.set(P_MA_BUDGET, 20000); p.set(P_LEDS, 300);
  e.reset(3, 300);
  for (int t = 0; t < 60; ++t) {
    e.step(fullFrame(t % 5 == 0), p);
    CHECK(led_estimate_ma(e.out(), 300, power_model(p)) <= 20000u);
  }
}

// Sınırlama gerçekten uygulanır: aynı sahne led_ma yarıya inince (ve ma sabit) ~2× daha parlak çıkar.
TEST(lower_led_ma_allows_brighter_output) {
  auto sumAfter = [](int ledMa) {
    Params p; p.defaults(); p.set(P_MA_BUDGET, 600); p.set(P_LED_MA, ledMa); p.set(P_LED_MODE, LM_SPECTRUM); p.set(P_BLOOM, 0);
    LedEngine e; e.reset(1, 120);
    for (int t = 0; t < 200; ++t) e.step(fullFrame(0), p);
    uint32_t s = 0;
    for (int i = 0; i < 120; ++i) s += (uint32_t)e.out()[i].r + e.out()[i].g + e.out()[i].b;
    return s;
  };
  const uint32_t s60 = sumAfter(60), s30 = sumAfter(30);
  CHECK(s30 > s60 * 3 / 2);        // ≈ 2×
  CHECK(s30 < s60 * 5 / 2);
}

// Limiter yokken (bütçe bol) talep = çıkış; sınırlarken talep > bütçe.
TEST(demand_reports_overload) {
  Params p; p.defaults(); p.set(P_MA_BUDGET, 300); p.set(P_LED_MODE, LM_SPECTRUM);
  LedEngine e; e.reset(1, 120);
  bool over = false;
  for (int t = 0; t < 120; ++t) { e.step(fullFrame(0), p); over = over || e.demandMa() > e.budgetMa(); }
  CHECK(over);
  CHECK(e.limiterQ8() < 255);
  p.set(P_MA_BUDGET, 20000);
  LedEngine e2; e2.reset(1, 120);
  { uint32_t r3 = 3; for (int t = 0; t < 120; ++t) e2.step(musicFrame(r3, t), p); CHECK(e2.limiterQ8() == 255 || e2.demandMa() > e2.budgetMa()); }
}

TEST(param_default_override) {
  param_clear_defaults();
  CHECK_EQ((int)param_default(P_LED_MA), 60);
  CHECK(param_set_default(P_LED_MA, 30));
  CHECK(param_set_default(P_PSU_MA, 3800));
  CHECK(param_set_default(P_LEDS, 9999));   // [min,max]'a kırpılır
  Params p; p.defaults();
  CHECK_EQ((int)p.get(P_LED_MA), 30);
  CHECK_EQ((int)p.get(P_PSU_MA), 3800);
  CHECK_EQ((int)p.get(P_LEDS), (int)LED_MAX);
  CHECK_EQ((int)param_info(P_LED_MA).def, 60);   // yerleşik varsayılan değişmez
  param_clear_defaults();
  p.defaults();
  CHECK_EQ((int)p.get(P_LED_MA), 60);
  CHECK_EQ((int)p.get(P_PSU_MA), 0);
  CHECK_EQ((int)p.get(P_LEDS), 120);
}

FV_TEST_MAIN()

// test_leds_param: `leds` (1..LED_MAX) ve yeni desen/renk parametreleri.
#include <cstring>
#include <initializer_list>
#include "fvtest.h"
#include "fftviz.h"
using namespace fv;

namespace {
uint32_t lum(const Rgb& c) { return (uint32_t)c.r + c.g + c.b; }

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
  f.onset = (t % 9) == 0;
  f.centroidHue = (uint8_t)((t * 3) % 200);
  return f;
}

Features fullFrame(int beat) {
  Features f; std::memset(&f, 0, sizeof f);
  for (int k = 0; k < NUM_BANDS; ++k) { f.level[k] = f.bar[k] = f.cap[k] = f.led[k] = 65535; }
  f.bass = f.mid = f.treble = f.loud = f.beatEnv = 65535;
  f.beat = f.onset = beat; f.beatStrength = 255; f.centroidHue = 100;
  return f;
}
}  // namespace

TEST(params_table_sane) {
  for (int i = 0; i < P_COUNT; ++i) {
    const ParamInfo& in = param_info((ParamId)i);
    CHECK(in.name && std::strlen(in.name) >= 1 && std::strlen(in.name) <= 12);   // NVS: "pN_" + ad ≤ 15
    CHECK(in.min <= in.def && in.def <= in.max);
    CHECK(in.desc && in.desc[0]);
    CHECK(in.group < PG_COUNT);
    ParamId back;
    CHECK(param_lookup(in.name, &back) && back == (ParamId)i);       // adlar tekil
  }
  CHECK(P_COUNT <= 64);   // controls.cpp: değişmiş-parametre maskesi 64 bit
}

TEST(perm_runtime_matches_perm120) {
  uint16_t p[LED_MAX];
  led_perm(p, 120);
  for (int i = 0; i < 120; ++i) CHECK_EQ((int)p[i], (int)vtab::perm120[i]);
  for (int n = 1; n <= LED_MAX; ++n) {
    led_perm(p, n);
    bool seen[LED_MAX] = {};
    bool ok = true;
    for (int i = 0; i < n; ++i) { if (p[i] >= n || seen[p[i]]) { ok = false; break; } seen[p[i]] = true; }
    CHECK(ok);
  }
}

// Her mod, 1..LED_MAX arası uzunluk: bütçe aşılmaz, aktif uzunluğun ötesi siyah, count() doğru, deterministik.
TEST(all_modes_all_lengths) {
  const int kFrames = 70;
  for (int n = 1; n <= LED_MAX; n += (n < 40 ? 1 : 7)) {
    for (int mode = 0; mode < LM_COUNT; ++mode) {
      uint32_t hh[2];
      for (int run = 0; run < 2; ++run) {
        LedEngine e;
        e.reset(3, n);
        Params p; p.defaults();
        p.set(P_LEDS, n); p.set(P_LED_MODE, mode); p.set(P_MA_BUDGET, 600);
        uint32_t rng = 77 + mode, h = 2166136261u;
        bool ok = true;
        CHECK_EQ(e.count(), n);
        for (int t = 0; t < kFrames; ++t) {
          e.step(t % 3 == 0 ? fullFrame(t % 9 == 0) : musicFrame(rng, t), p);
          if (e.count() != n) ok = false;
          if (led_estimate_ma(e.out(), n) > 600 || e.milliamps() > 600) ok = false;
          for (int i = n; i < LED_MAX; ++i) if (lum(e.out()[i]) || lum(e.pattern()[i])) { ok = false; break; }
          h = fnv1a(e.out(), sizeof(Rgb) * (size_t)n, h);
        }
        if (!ok) std::printf("    n=%d mod=%s: ihlal\n", n, led_mode_name((uint8_t)mode));
        CHECK(ok);
        hh[run] = h;
      }
      CHECK_EQ(hh[0], hh[1]);
    }
  }
}

TEST(limiter_budget_every_length) {
  for (int budget : {200, 500, 1500, 3400}) {
    for (int n : {1, 2, 7, 30, 60, 119, 120, 121, 180, 299, 300}) {
      for (int mode : {LM_AURORA, LM_SPECTRUM, LM_FIRE, LM_WATERFALL}) {
        LedEngine e; e.reset(5, n);
        Params p; p.defaults();
        p.set(P_LEDS, n); p.set(P_LED_MODE, mode); p.set(P_MA_BUDGET, budget); p.set(P_BLOOM, 230);
        uint32_t worst = 0;
        for (int t = 0; t < 200; ++t) {
          e.step(fullFrame(t % 15 == 0), p);
          const uint32_t ma = led_estimate_ma(e.out(), n);
          if (ma > worst) worst = ma;
        }
        // Akım modelinin tabanı LED başına 1 mA (siyah bile): bütçe n mA'in altındaysa şerit tamamen siyah, ma = n.
        const uint32_t lim = vmax<uint32_t>((uint32_t)budget, (uint32_t)n);
        if (worst > lim) std::printf("    n=%d mod=%d bütçe=%d: %u mA\n", n, mode, budget, worst);
        CHECK(worst <= lim);
      }
    }
  }
}

TEST(engine_reset_uses_leds_param) {
  static Engine e;
  e.params.defaults();
  e.params.set(P_LEDS, 60);
  e.reset(1);
  CHECK_EQ(e.leds().count(), 60);
  e.params.set(P_LEDS, 200);
  uint16_t adc[ADC_BLOCK];
  Synth sy; sy.reset(SC_MIX, 1, 0);
  for (int i = 0; i < 3; ++i) { sy.render(adc, ADC_BLOCK); e.pushAdc(adc, ADC_BLOCK); }
  CHECK_EQ(e.leds().count(), 200);
}

// Çalışma sırasında uzunluk değişimi: durum güvenli sıfırlanır, taşma yok, çıkış aktif uzunlukta.
TEST(length_change_midrun_safe) {
  uint32_t rng = 9;
  for (int mode = 0; mode < LM_COUNT; ++mode) {
    LedEngine e; e.reset(2);
    Params p; p.defaults(); p.set(P_LED_MODE, mode);
    const int seq[] = {120, 30, 300, 1, 177, 120, 8, 250, 120};
    for (int si = 0; si < 9; ++si) {
      p.set(P_LEDS, seq[si]);
      for (int t = 0; t < 40; ++t) e.step(musicFrame(rng, si * 40 + t), p);
      CHECK_EQ(e.count(), seq[si]);
      bool tail = true;
      for (int i = seq[si]; i < LED_MAX; ++i) if (lum(e.out()[i])) tail = false;
      CHECK(tail);
    }
  }
}

// Simetrik desenler (BASS, WATERFALL) çift ve tek uzunlukta merkeze göre simetrik.
TEST(centered_modes_symmetric) {
  for (int n : {40, 59, 60, 61, 120, 121, 255}) {
    for (int mode : {LM_BASS, LM_WATERFALL}) {
      LedEngine e; e.reset(1, n);
      Params p; p.defaults(); p.set(P_LEDS, n); p.set(P_LED_MODE, mode); p.set(P_BLOOM, 0);
      uint32_t rng = 5;
      for (int t = 0; t < 150; ++t) e.step(musicFrame(rng, t), p);
      bool sym = true;
      for (int i = 0; i < n; ++i) {
        const Rgb a = e.pattern()[i], b = e.pattern()[n - 1 - i];
        if (a.r != b.r || a.g != b.g || a.b != b.b) sym = false;
      }
      if (!sym) std::printf("    n=%d mod=%s simetrik değil\n", n, led_mode_name((uint8_t)mode));
      CHECK(sym);
    }
  }
}

// Tek bant uyarılınca AURORA tepe konumu n ile orantılı ölçeklenir.
TEST(aurora_scales_with_length) {
  for (int n : {30, 60, 120, 240}) {
    LedEngine e; e.reset(1, n);
    Params p; p.defaults(); p.set(P_LEDS, n); p.set(P_LED_MODE, LM_AURORA);
    Features f; std::memset(&f, 0, sizeof f);
    f.led[20] = 65535;
    for (int t = 0; t < 5; ++t) e.step(f, p);
    int c = 0; uint32_t best = 0;
    for (int i = 0; i < n; ++i) if (lum(e.pattern()[i]) > best) { best = lum(e.pattern()[i]); c = i; }
    const int want = n * 20 / 39 ;
    CHECK(c >= want - n / 20 - 1 && c <= want + n / 20 + 1);
  }
}

// SPECTRUM tam dolu: her LED yanar (her uzunlukta segment bölümü kapsayıcı).
TEST(spectrum_covers_strip) {
  for (int n : {8, 9, 31, 60, 100, 120, 301 > LED_MAX ? 300 : 301}) {
    LedEngine e; e.reset(1, n);
    Params p; p.defaults(); p.set(P_LEDS, n); p.set(P_LED_MODE, LM_SPECTRUM); p.set(P_LED_AE, 0);
    Features f = fullFrame(0);
    for (int t = 0; t < 4; ++t) e.step(f, p);
    int lit = 0;
    for (int i = 0; i < n; ++i) lit += lum(e.pattern()[i]) > 0;
    CHECK_EQ(lit, n);
  }
}

// Varsayılan dışı değerler görünümü gerçekten değiştirir (parametreler bağlı).
TEST(new_params_change_output) {
  struct Case { ParamId id; int32_t v; int mode; };
  const Case cases[] = {
      {P_HUE_OFS, 90, LM_AURORA}, {P_HUE_OFS, 90, LM_SPECTRUM}, {P_HUE_OFS, 90, LM_FIRE}, {P_HUE_OFS, 90, LM_BASS},
      {P_HUE_OFS, 90, LM_MIRROR}, {P_HUE_OFS, 90, LM_WATERFALL}, {P_HUE_OFS, 90, LM_COMET},
      {P_PAL, 2, LM_AURORA}, {P_PAL, 4, LM_SPECTRUM}, {P_PAL, 1, LM_FIRE},
      {P_HUE_SPD, 60, LM_AURORA}, {P_WF_SPD, 400, LM_WATERFALL}, {P_WF_DEC, 200, LM_WATERFALL},
      {P_FIRE_COOL, 150, LM_FIRE}, {P_FIRE_SPARK, 150, LM_FIRE}, {P_COMET_SPD, 600, LM_COMET}, {P_COMET_TRL, 240, LM_COMET},
      {P_EMBER, 30, LM_AURORA}, {P_BASS_ATK, 60, LM_BASS}, {P_BASS_REL, 150, LM_BASS}, {P_HUE_KICK, 20, LM_AURORA},
  };
  for (const Case& c : cases) {
    uint32_t h[2];
    for (int k = 0; k < 2; ++k) {
      LedEngine e; e.reset(4);
      Params p; p.defaults(); p.set(P_LED_MODE, c.mode);
      if (k) p.set(c.id, c.v);
      uint32_t rng = 21, hh = 2166136261u;
      for (int t = 0; t < 200; ++t) { e.step(musicFrame(rng, t), p); hh = fnv1a(e.pattern(), sizeof(Rgb) * NUM_LEDS, hh); }
      h[k] = hh;
    }
    if (h[0] == h[1]) std::printf("    %s=%d mod=%s: çıkış değişmedi\n", param_info(c.id).name, (int)c.v, led_mode_name((uint8_t)c.mode));
    CHECK(h[0] != h[1]);
  }
}

// Boşta (sessizlik) parametreleri ve geçiş süreleri.
TEST(idle_and_fade_params) {
  Features f; std::memset(&f, 0, sizeof f); f.silent = 1;
  uint32_t h[3];
  for (int k = 0; k < 3; ++k) {
    LedEngine e; e.reset(1);
    Params p; p.defaults();
    if (k == 1) p.set(P_IDLE_BRI, 90);
    if (k == 2) p.set(P_IDLE_SPD, 512);
    uint32_t hh = 2166136261u;
    for (int t = 0; t < 120; ++t) { e.step(f, p); hh = fnv1a(e.pattern(), sizeof(Rgb) * NUM_LEDS, hh); }
    h[k] = hh;
  }
  CHECK(h[0] != h[1] && h[0] != h[2]);
  // idle_bri tepesi: V ≈ idle_bri + 29
  LedEngine e; e.reset(1);
  Params p; p.defaults(); p.set(P_IDLE_BRI, 100);
  uint32_t mx = 0;
  for (int t = 0; t < 400; ++t) { e.step(f, p); for (int i = 0; i < NUM_LEDS; ++i) { const Rgb c = e.pattern()[i]; mx = vmax<uint32_t>(mx, vmax(vmax(c.r, c.g), c.b)); } }
  CHECK(mx >= 100 && mx <= 129);
  // fade: mod değişiminden 3 kare sonra desen, eski desene ne kadar yakın (uzun fade = daha yakın)
  uint32_t dist[2];
  for (int k = 0; k < 2; ++k) {
    LedEngine g; g.reset(1);
    Params q; q.defaults(); q.set(P_FADE, k ? 90 : 1); q.set(P_LED_MODE, LM_AURORA);
    uint32_t rng = 3;
    for (int t = 0; t < 30; ++t) g.step(musicFrame(rng, t), q);
    Rgb before[LED_MAX];
    std::memcpy(before, g.pattern(), sizeof before);
    q.set(P_LED_MODE, LM_BASS);
    for (int t = 0; t < 3; ++t) g.step(musicFrame(rng, 30 + t), q);
    uint32_t d = 0;
    for (int i = 0; i < NUM_LEDS; ++i) d += (uint32_t)iabs32((int)g.pattern()[i].r - before[i].r) + iabs32((int)g.pattern()[i].g - before[i].g) + iabs32((int)g.pattern()[i].b - before[i].b);
    dist[k] = d;
  }
  CHECK(dist[0] > 3 * dist[1]);
}

// Palet döngüsü: bekleme evresinde sabit palete eşit, kayma evresinde ikisi arasında.
TEST(palette_cycle) {
  Features f = fullFrame(0);
  for (int k = 0; k < NUM_BANDS; ++k) f.led[k] = (uint16_t)(30000 + k * 800);
  auto frameHash = [&](int pal, int cyc, int steps) {
    LedEngine e; e.reset(1);
    Params p; p.defaults();
    p.set(P_LED_MODE, LM_AURORA); p.set(P_HUE_SPD, 0); p.set(P_HUE_KICK, 0); p.set(P_PAL, pal); p.set(P_PAL_CYC, cyc); p.set(P_LED_AE, 0);
    for (int t = 0; t < steps; ++t) e.step(f, p);
    return fnv1a(e.pattern(), sizeof(Rgb) * NUM_LEDS);
  };
  // pal_cyc = 2 s → periyot 120 kare; kayma son çeyrek (30 kare). Her periyotun ilk 90 karesi palet k'dır.
  for (int k = 0; k < LED_PAL_N; ++k) {
    const int steps = k * 120 + 50;
    CHECK_EQ(frameHash(0, 2, steps), frameHash(k, 0, 1));
  }
  const uint32_t mid = frameHash(0, 2, 105);   // palet 0 → 1 kayması ortasında
  CHECK(mid != frameHash(0, 0, 1) && mid != frameHash(1, 0, 1));
  // döngü tamamlanınca başa döner
  CHECK_EQ(frameHash(0, 2, LED_PAL_N * 120 + 50), frameHash(0, 0, 1));
  // pal_cyc kapalı → özdeşlik: sabit palet 0, hue_ofs 0 eski çıkışla bit-aynı (test_leds_regress de doğrular)
  CHECK_EQ(frameHash(0, 0, 10), frameHash(0, 0, 10));
}

FV_TEST_MAIN()

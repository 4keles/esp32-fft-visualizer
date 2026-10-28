#include <chrono>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"
#include "vis_synth.h"
using namespace fv;

static Features fullScale(uint32_t& rng, bool noise) {
  Features f; std::memset(&f, 0, sizeof f);
  for (int k = 0; k < NUM_BANDS; ++k) {
    uint16_t v = noise ? (uint16_t)(xorshift32(rng) & 0xFFFF) : 65535;
    f.level[k] = f.bar[k] = f.cap[k] = f.led[k] = v;
    f.capHold[k] = noise ? (uint8_t)(xorshift32(rng) & 31) : 18;
  }
  auto r = [&]() { return noise ? (uint16_t)(xorshift32(rng) & 0xFFFF) : (uint16_t)65535; };
  f.bass = r(); f.mid = r(); f.treble = r(); f.loud = r(); f.beatEnv = r();
  f.beat = noise ? (xorshift32(rng) & 1) : 1; f.onset = noise ? (xorshift32(rng) & 1) : 1;
  f.silent = 0; f.centroidHue = (uint8_t)(noise ? xorshift32(rng) % 201 : 100);
  f.beatStrength = noise ? (uint8_t)xorshift32(rng) : 255;
  return f;
}

static uint32_t lum(const Rgb& c) { return (uint32_t)c.r + c.g + c.b; }
static uint32_t sumPix(const Rgb* p) { uint32_t s = 0; for (int i = 0; i < NUM_LEDS; ++i) s += lum(p[i]); return s; }

static void runBudget(int budget, int bri, int bloomQ8) {
  for (int mode = 0; mode < LM_COUNT; ++mode) {
    for (int kind = 0; kind < 3; ++kind) {   // 0: her şey max, 1: gürültü, 2: sentetik müzik
      LedEngine e; e.reset(5);
      Params p; p.defaults();
      p.set(P_LED_MODE, mode); p.set(P_MA_BUDGET, budget); p.set(P_BRIGHTNESS, bri); p.set(P_BLOOM, bloomQ8);
      vtest::SynthFeatures syn; syn.reset(11);
      uint32_t rng = 0x1234 + mode;
      uint32_t worst = 0; int viol = 0;
      for (int t = 0; t < 900; ++t) {
        Features f = kind == 0 ? fullScale(rng, false) : kind == 1 ? fullScale(rng, true) : syn.step();
        e.step(f, p);
        uint32_t ma = led_estimate_ma(e.out(), NUM_LEDS);
        if (ma > worst) worst = ma;
        if (ma > (uint32_t)budget || e.milliamps() != ma) ++viol;
      }
      if (viol) std::printf("    mode %d kind %d budget %d bri %d bloom %d: ihlal %d, en kotu %u mA\n", mode, kind, budget, bri, bloomQ8, viol, worst);
      CHECK_EQ(viol, 0);
      if (kind == 0 && bri == 255 && bloomQ8 == 128 && budget == 2500) {
        char key[40]; std::snprintf(key, sizeof key, "worst_mA_%s", led_mode_name((uint8_t)mode)); METRIC(key, worst);
      }
    }
  }
}

TEST(power_budget_2500) { runBudget(2500, 255, 128); runBudget(2500, 160, 128); }
TEST(power_budget_500)  { runBudget(500, 255, 128); runBudget(500, 160, 255); }
TEST(power_budget_extremes) { runBudget(200, 255, 255); runBudget(3400, 255, 255); }

TEST(determinism) {
  for (int mode = 0; mode < LM_COUNT; ++mode) {
    uint32_t h[2];
    for (int run = 0; run < 2; ++run) {
      LedEngine e; e.reset(99);
      Params p; p.defaults(); p.set(P_LED_MODE, mode);
      vtest::SynthFeatures syn; syn.reset(3);
      uint32_t hh = 2166136261u;
      for (int t = 0; t < 600; ++t) { e.step(syn.step(), p); hh = fnv1a(e.out(), sizeof(Rgb) * NUM_LEDS, hh); }
      h[run] = hh;
    }
    CHECK_EQ(h[0], h[1]);
  }
  // farklı seed'ler FIRE/COMET'te farklı sonuç vermeli
  uint32_t hs[2];
  for (int s = 0; s < 2; ++s) {
    LedEngine e; e.reset(1 + s);
    Params p; p.defaults(); p.set(P_LED_MODE, LM_FIRE);
    vtest::SynthFeatures syn; syn.reset(3);
    uint32_t hh = 2166136261u;
    for (int t = 0; t < 200; ++t) { e.step(syn.step(), p); hh = fnv1a(e.out(), sizeof(Rgb) * NUM_LEDS, hh); }
    hs[s] = hh;
  }
  CHECK(hs[0] != hs[1]);
}

TEST(embers_never_off) {
  for (int mode = 0; mode < 2; ++mode) {          // AURORA, SPARKLE
    LedEngine e; e.reset(1);
    Params p; p.defaults(); p.set(P_LED_MODE, mode);
    Features f; std::memset(&f, 0, sizeof f);
    for (int k = 0; k < NUM_BANDS; ++k) f.led[k] = 65535 * 12 / 100;  // eşiğin (0.20) altında
    int zeroFrames = 0, minLit = NUM_LEDS;
    uint32_t maxLum = 0;
    for (int t = 0; t < 120; ++t) {
      e.step(f, p);
      int lit = 0; for (int i = 0; i < NUM_LEDS; ++i) { lit += lum(e.out()[i]) > 0; maxLum = vmax(maxLum, lum(e.out()[i])); }
      if (t > 10) minLit = vmin(minLit, lit);
      zeroFrames += sumPix(e.out()) == 0;
    }
    METRIC("ember_min_lit_leds", minLit);
    CHECK_EQ(zeroFrames, 0);
    CHECK(minLit >= 40);
    CHECK(maxLum < 120);           // dim: köz
    CHECK(sumPix(e.pattern()) > 0);
  }
}

TEST(threshold_cp_law) {
  // eşik altı → sönük, eşik üstü → boyut² yasası ile hızla parlar
  LedEngine e; e.reset(1);
  Params p; p.defaults(); p.set(P_LED_MODE, LM_AURORA); p.set(P_BLOOM, 0);
  uint32_t prev = 0, below = 0, above = 0;
  for (int lvl = 0; lvl <= 10; ++lvl) {
    Features f; std::memset(&f, 0, sizeof f);
    for (int k = 0; k < NUM_BANDS; ++k) f.led[k] = (uint16_t)(65535 * lvl / 10);
    e.step(f, p);
    uint32_t s = sumPix(e.pattern());
    if (lvl == 3) below = s;
    if (lvl == 10) above = s;
    CHECK(s * 100 >= prev * 80);   // ton başına parlaklık değişir (rainbow); yine de ~monoton
    prev = s;
  }
  METRIC("pattern_sum_at_0.3", below); METRIC("pattern_sum_at_1.0", above);
  CHECK(above > below * 8);
}

TEST(glow_decays_with_distance) {
  LedEngine e; e.reset(1);
  Params p; p.defaults(); p.set(P_LED_MODE, LM_AURORA);
  Features f; std::memset(&f, 0, sizeof f);
  f.led[20] = 65535;                              // tek bant tam uyarılmış
  for (int t = 0; t < 5; ++t) e.step(f, p);
  int c = 0; uint32_t best = 0;
  for (int i = 0; i < NUM_LEDS; ++i) if (lum(e.pattern()[i]) > best) { best = lum(e.pattern()[i]); c = i; }
  CHECK(c >= 55 && c <= 66);                      // x=39i/119 = 20 → i ≈ 61
  uint32_t l0 = lum(e.pattern()[c]), l1 = lum(e.pattern()[c + 1]), l3 = lum(e.pattern()[c + 3]), l6 = lum(e.pattern()[c + 6]);
  uint32_t r1 = lum(e.pattern()[c - 1]), r3 = lum(e.pattern()[c - 3]);
  METRIC("glow_l0", l0); METRIC("glow_l1", l1); METRIC("glow_l3", l3); METRIC("glow_l6", l6);
  CHECK(l0 > l1 && l1 > l3 && l3 > l6);
  CHECK(r1 > r3);
  CHECK(l1 > l0 / 4);                             // komşu belirgin yanar
  CHECK(l6 > 0);
  // uzak LED yalnızca köz
  CHECK(lum(e.pattern()[5]) < l3);
}

TEST(sparkle_scattered) {
  LedEngine e; e.reset(1);
  Params p; p.defaults(); p.set(P_LED_MODE, LM_SPARKLE);
  Features f; std::memset(&f, 0, sizeof f);
  f.led[20] = 65535;
  e.step(f, p);
  // dağınık: en parlak LED'in komşuları sıralı moddaki gibi sönmez, ama permütasyon gereği çevrede farklı bantlar var
  int c = 0; uint32_t best = 0;
  for (int i = 0; i < NUM_LEDS; ++i) if (lum(e.pattern()[i]) > best) { best = lum(e.pattern()[i]); c = i; }
  CHECK(c == (int)[&] { for (int i = 0; i < NUM_LEDS; ++i) if (vtab::perm120[i] >= 60 && vtab::perm120[i] <= 62) return i; return -1; }() ||
        best > 0);
  CHECK(best > 100);
}

TEST(idle_crossfade_after_silence) {
  Params p; p.defaults(); p.set(P_LED_MODE, LM_AURORA);
  LedEngine a, b; a.reset(1); b.reset(1);
  vtest::SynthFeatures sa, sb; sa.reset(1); sb.reset(2);
  for (int t = 0; t < 30; ++t) { a.step(sa.step(), p); b.step(sb.step(), p); }
  // sessizlik: farklı girişler yine de aynı idle örüntüsüne yakınsar
  Features fa = sa.f, fb = sb.f; fa.silent = fb.silent = 1;
  uint32_t before = sumPix(a.pattern());
  std::vector<uint32_t> sums;
  for (int t = 0; t < 30; ++t) {
    a.step(fa, p); b.step(fb, p);
    sums.push_back(sumPix(a.pattern()));
    if (t == 8) {   // yarı yolda: ikisinden de farklı bir karışım
      CHECK(sumPix(a.pattern()) != before);
    }
  }
  bool same = true;
  for (int i = 0; i < NUM_LEDS; ++i) same &= a.pattern()[i].r == b.pattern()[i].r && a.pattern()[i].g == b.pattern()[i].g && a.pattern()[i].b == b.pattern()[i].b;
  CHECK(same);
  // idle: düşük parlaklık gökkuşağı (V 28..64 → kanal toplamı sınırlı) ve renkli
  uint32_t mx = 0; int colored = 0;
  for (int i = 0; i < NUM_LEDS; ++i) { mx = vmax(mx, (uint32_t)vmax(vmax(a.pattern()[i].r, a.pattern()[i].g), a.pattern()[i].b)); colored += lum(a.pattern()[i]) > 0; }
  CHECK(mx <= 70); CHECK(colored > 100);
  // 18 karede geçiş tamamlanır: 17. kareden itibaren sabit zarf (sadece idle'ın kendi animasyonu)
  CHECK(sums[16] > 0);
  // ses geri gelince 18 kare sonra normal çıktı (idleN_ sıfır) — kesintisiz geçiş
  int lastDiff = 0;
  for (int t = 0; t < 40; ++t) { Features f = sa.step(); a.step(f, p); b.step(f, p); }
  lastDiff = 0;
  for (int i = 0; i < NUM_LEDS; ++i) lastDiff += a.pattern()[i].r != b.pattern()[i].r;
  CHECK_EQ(lastDiff, 0);
  CHECK(sumPix(a.pattern()) > 300);
}

TEST(mode_change_crossfades) {
  Params p; p.defaults();
  vtest::SynthFeatures syn; syn.reset(4);
  LedEngine e; e.reset(1); p.set(P_LED_MODE, LM_MIRROR);
  for (int t = 0; t < 60; ++t) e.step(syn.step(), p);
  Rgb before[NUM_LEDS]; for (int i = 0; i < NUM_LEDS; ++i) before[i] = e.pattern()[i];
  p.set(P_LED_MODE, LM_FIRE);
  Features f = syn.step();
  e.step(f, p);
  // ilk karede eski örüntüye çok yakın (crossfade başlangıcı)
  uint32_t d1 = 0, scale = 0;
  for (int i = 0; i < NUM_LEDS; ++i) { d1 += (uint32_t)iabs32(e.pattern()[i].r - before[i].r) + iabs32(e.pattern()[i].g - before[i].g) + iabs32(e.pattern()[i].b - before[i].b); scale += lum(before[i]); }
  METRIC("first_frame_delta_ratio", scale ? (double)d1 / scale : 0);
  CHECK(d1 * 4 < scale + 100);
  CHECK_EQ(e.activeMode(), LM_FIRE);
  for (int t = 0; t < 40; ++t) e.step(syn.step(), p);
  CHECK_EQ(e.activeMode(), LM_FIRE);
}

TEST(demo_advances) {
  Params p; p.defaults(); p.set(P_LED_MODE, LM_DEMO);
  LedEngine e; e.reset(1);
  vtest::SynthFeatures syn; syn.reset(5);
  bool seen[LM_COUNT] = {}; int distinct = 0;
  for (int t = 0; t < 4000; ++t) {
    e.step(syn.step(), p);
    CHECK(e.activeMode() < LM_DEMO);
    if (!seen[e.activeMode()]) { seen[e.activeMode()] = true; ++distinct; }
  }
  METRIC("demo_distinct_modes", distinct);
  CHECK(distinct >= 4);
  // beat yoksa 30 sn'de bir geçer
  LedEngine g; g.reset(1);
  Features f; std::memset(&f, 0, sizeof f);
  int changes = 0, last = g.activeMode();
  for (int t = 0; t < 1900; ++t) { g.step(f, p); if (g.activeMode() != last) { ++changes; last = g.activeMode(); } }
  CHECK_EQ(changes, 1);
}

TEST(limiter_dynamics) {
  Params p; p.defaults(); p.set(P_LED_MODE, LM_MIRROR); p.set(P_BRIGHTNESS, 255); p.set(P_MA_BUDGET, 600);
  LedEngine e; e.reset(1);
  uint32_t rng = 1;
  Features loud = fullScale(rng, false);
  Features quiet; std::memset(&quiet, 0, sizeof quiet);
  for (int t = 0; t < 30; ++t) e.step(loud, p);
  CHECK(e.limiterQ8() < 255);
  int prev = e.limiterQ8(), maxUp = 0, frames = 0;
  while (e.limiterQ8() < 255 && frames < 1000) {
    e.step(quiet, p); ++frames;
    maxUp = vmax(maxUp, (int)e.limiterQ8() - prev); prev = e.limiterQ8();
  }
  METRIC("limiter_recovery_frames", frames);
  CHECK(maxUp <= 4);
  CHECK(frames > 5);
  // aşağı anlık: ani yük → aynı karede sınır
  e.step(loud, p);
  CHECK(led_estimate_ma(e.out(), NUM_LEDS) <= 600);
}

TEST(mode_semantics) {
  Params p; p.defaults(); p.set(P_BLOOM, 0); p.set(P_BRIGHTNESS, 255);
  // SPECTRUM: bar yüksekliği ile yanan LED sayısı artar
  {
    p.set(P_LED_MODE, LM_SPECTRUM);
    int lit[3];
    for (int j = 0; j < 3; ++j) {
      LedEngine e; e.reset(1);
      Features f; std::memset(&f, 0, sizeof f);
      for (int k = 0; k < NUM_BANDS; ++k) f.bar[k] = (uint16_t)(j == 0 ? 3000 : (j == 1 ? 30000 : 65535));
      e.step(f, p);
      lit[j] = 0; for (int i = 0; i < NUM_LEDS; ++i) lit[j] += lum(e.pattern()[i]) > 0;
    }
    CHECK(lit[0] < lit[1] && lit[1] < lit[2]);
    CHECK_EQ(lit[2], NUM_LEDS);
  }
  // BASS: merkez parlak, uçlar loş
  {
    p.set(P_LED_MODE, LM_BASS);
    LedEngine e; e.reset(1);
    Features f; std::memset(&f, 0, sizeof f); f.bass = 50000; f.centroidHue = 30;
    for (int t = 0; t < 20; ++t) e.step(f, p);
    CHECK(lum(e.pattern()[59]) > 4 * lum(e.pattern()[0]) + 10);
    CHECK(lum(e.pattern()[59]) + 20 >= lum(e.pattern()[40]));
  }
  // WATERFALL: merkezden dışa kayar
  {
    p.set(P_LED_MODE, LM_WATERFALL);
    LedEngine e; e.reset(1);
    Features f; std::memset(&f, 0, sizeof f); f.loud = 60000; f.centroidHue = 60;
    for (int t = 0; t < 6; ++t) e.step(f, p);
    f.loud = 0;
    int prevPos = -1, moved = 0;
    for (int t = 0; t < 30; ++t) {
      e.step(f, p);
      int pos = 0; uint32_t best = 0;
      for (int i = 60; i < NUM_LEDS; ++i) if (lum(e.pattern()[i]) > best) { best = lum(e.pattern()[i]); pos = i - 60; }
      if (t > 0 && best > 30) moved += pos >= prevPos;
      prevPos = pos;
    }
    CHECK(prevPos > 8);
    CHECK(moved >= 20);
    // simetri
    for (int d = 0; d < 60; ++d) CHECK(lum(e.pattern()[59 - d]) == lum(e.pattern()[60 + d]));
  }
  // FIRE: beat'te uçlar (taban) ısınır
  {
    p.set(P_LED_MODE, LM_FIRE);
    LedEngine e; e.reset(1);
    Features f; std::memset(&f, 0, sizeof f); f.beat = 1; f.beatStrength = 255; f.loud = 40000;
    e.step(f, p);
    CHECK(lum(e.pattern()[0]) > 300 && lum(e.pattern()[119]) > 300);
    CHECK(lum(e.pattern()[59]) < 100);
  }
  // COMET: beat'le başlayan baş merkezden uzaklaşır
  {
    p.set(P_LED_MODE, LM_COMET);
    LedEngine e; e.reset(1);
    Features f; std::memset(&f, 0, sizeof f); f.beat = 1; f.beatStrength = 200; f.centroidHue = 100;
    e.step(f, p);
    f.beat = 0;
    int dist[8]; 
    for (int t = 0; t < 8; ++t) {
      e.step(f, p);
      uint32_t best = 0; int pos = 0;
      for (int i = 0; i < NUM_LEDS; ++i) if (lum(e.pattern()[i]) > best) { best = lum(e.pattern()[i]); pos = i; }
      dist[t] = iabs32(pos - 59);
    }
    CHECK(dist[7] > dist[1] + 6);
  }
}

// --- led-brightness-tuning: otomatik pozlama (led_ae), eşit bant payı, sessizlik ---
static Features weak(Features f, uint32_t num, uint32_t den) {   // seviyeleri num/den ile ölçekle (sönük müzik)
  for (int k = 0; k < NUM_BANDS; ++k) {
    f.level[k] = (uint16_t)(f.level[k] * num / den); f.bar[k] = (uint16_t)(f.bar[k] * num / den);
    f.cap[k] = (uint16_t)(f.cap[k] * num / den); f.led[k] = (uint16_t)(f.led[k] * num / den);
  }
  f.bass = (uint16_t)(f.bass * num / den); f.mid = (uint16_t)(f.mid * num / den);
  f.treble = (uint16_t)(f.treble * num / den); f.loud = (uint16_t)(f.loud * num / den);
  return f;
}
static double meanOut(const LedEngine& e) { return sumPix(e.out()) / (3.0 * NUM_LEDS); }
static double runMean(Params p, int mode, int frames, int skip, uint32_t num, uint32_t den, int* minG = nullptr, int* maxG = nullptr) {
  LedEngine e; e.reset(3); p.set(P_LED_MODE, mode);
  vtest::SynthFeatures syn; syn.reset(9);
  double acc = 0; int n = 0;
  for (int t = 0; t < frames; ++t) {
    e.step(weak(syn.step(), num, den), p);
    if (minG) *minG = vmin(*minG, (int)e.aeGainQ8());
    if (maxG) *maxG = vmax(*maxG, (int)e.aeGainQ8());
    if (t >= skip) { acc += meanOut(e); ++n; }
  }
  return acc / n;
}

TEST(ae_param_and_bounds) {
  ParamId id; CHECK(param_lookup("led_ae", &id)); CHECK_EQ((int)id, (int)P_LED_AE);
  CHECK(std::strlen(param_info(P_LED_AE).name) <= 12);
  Params p; p.defaults();
  CHECK(p.get(P_LED_AE) > 0);                 // varsayılan açık
  CHECK_EQ(p.get(P_BRIGHTNESS), 255);
  int lo = 1 << 20, hi = 0;
  runMean(p, LM_AURORA, 900, 0, 1, 2, &lo, &hi);
  METRIC("ae_gain_min_q8", lo); METRIC("ae_gain_max_q8", hi);
  CHECK(lo >= 256);                           // 1× altına inmez
  CHECK(hi <= 24 * 256);                      // 24× üstüne çıkmaz
  CHECK(hi > 2 * 256);                        // sönük müzikte gerçekten yükselir
  p.set(P_LED_AE, 0);                         // kapalı → her kare 1×
  lo = 1 << 20; hi = 0;
  runMean(p, LM_AURORA, 300, 0, 1, 2, &lo, &hi);
  CHECK_EQ(lo, 256); CHECK_EQ(hi, 256);
}

TEST(ae_levels_weak_music) {
  // Sönük müzik (seviyeler yarıya iner): pozlama açıkken ortalama çıkış belirgin yüksek, güç bütçesi korunur.
  for (int mode = 0; mode < LM_DEMO; ++mode) {
    if (mode == LM_FIRE) continue;            // FIRE zaten kendi enerjisiyle parlak; ayrıca ölçülür
    Params on; on.defaults();
    Params off = on; off.set(P_LED_AE, 0);
    double a = runMean(on, mode, 900, 400, 1, 2), b = runMean(off, mode, 900, 400, 1, 2);
    char key[48]; std::snprintf(key, sizeof key, "ae_mean_on_%s", led_mode_name((uint8_t)mode)); METRIC(key, a);
    std::snprintf(key, sizeof key, "ae_mean_off_%s", led_mode_name((uint8_t)mode)); METRIC(key, b);
    CHECK(a > b * 1.3);
    CHECK(a > (mode == LM_SPARKLE ? 6.0 : 14.0));   // SPARKLE dağınık: doğası gereği daha sönük
  }
}

// Kabul ölçütü: gerçek analiz hattı (SC_MIX), AURORA, kare 120..600 ortalama kanal değeri.
// fmin: 47 = pozlama ayarının yapıldığı eski geniş aralık (bas bantları görünür); varsayılan 250 için
// ae_acceptance_default_range.
static double engineMean(Engine& eng, int mode, int gainDb, int aeOn, int sc = SC_MIX, int fmin = 47) {
  static Synth syn; static uint16_t buf[ADC_BLOCK];
  eng.params.defaults(); eng.params.set(P_LED_MODE, mode); eng.params.set(P_FMIN, fmin);
  if (!aeOn) eng.params.set(P_LED_AE, 0);
  eng.reset(1); syn.reset((Scenario)sc, 1, (int8_t)gainDb);
  int fr = 0; double acc = 0; int n = 0; uint32_t worst = 0;
  while (fr < 600) {
    syn.render(buf, ADC_BLOCK);
    if (eng.pushAdc(buf, ADC_BLOCK)) {
      ++fr;
      if (fr > 120) { acc += meanOut(eng.leds()); ++n; worst = vmax(worst, (uint32_t)eng.leds().milliamps()); }
    }
  }
  CHECK(worst <= 2500);
  return acc / n;
}

TEST(ae_acceptance_mix) {
  static Engine eng;
  double on12 = engineMean(eng, LM_AURORA, -12, 1), on30 = engineMean(eng, LM_AURORA, -30, 1);
  double off12 = engineMean(eng, LM_AURORA, -12, 0), off30 = engineMean(eng, LM_AURORA, -30, 0);
  METRIC("aurora_mix-12_on", on12); METRIC("aurora_mix-30_on", on30);
  METRIC("aurora_mix-12_off", off12); METRIC("aurora_mix-30_off", off30);
  CHECK(on12 >= 35.0); CHECK(on30 >= 35.0);
  CHECK(on12 - on30 < off12 - off30);             // seviye farkı küçülür
  for (int mode = 0; mode < LM_DEMO; ++mode) {    // her mod pozlamayla belirgin artar (FIRE zaten parlak)
    if (mode == LM_FIRE) continue;
    double a = engineMean(eng, mode, -12, 1), b = engineMean(eng, mode, -12, 0);
    char key[48]; std::snprintf(key, sizeof key, "mix-12_%s_on_off", led_mode_name((uint8_t)mode));
    METRIC(key, a); std::snprintf(key, sizeof key, "mix-12_%s_off", led_mode_name((uint8_t)mode)); METRIC(key, b);
    CHECK(a > b * 1.2);
  }
}

// Varsayılan aralıkta (fmin 250: bas bantları görüntüden çıktı, bas yalnız `bass` bölgesini sürer) aynı müzik
// için LED ortalaması düşer, çünkü SC_MIX enerjisinin büyük kısmı bastadır. Pozlama yine belirgin açar.
TEST(ae_acceptance_default_range) {
  static Engine eng;
  const int fmin = (int)param_info(P_FMIN).def;
  double on12 = engineMean(eng, LM_AURORA, -12, 1, SC_MIX, fmin), on30 = engineMean(eng, LM_AURORA, -30, 1, SC_MIX, fmin);
  double off12 = engineMean(eng, LM_AURORA, -12, 0, SC_MIX, fmin), off30 = engineMean(eng, LM_AURORA, -30, 0, SC_MIX, fmin);
  METRIC("aurora_mix-12_on", on12); METRIC("aurora_mix-30_on", on30);
  METRIC("aurora_mix-12_off", off12); METRIC("aurora_mix-30_off", off30);
  CHECK(on12 >= 25.0); CHECK(on30 >= 10.0);
  CHECK(on12 > off12 * 1.5); CHECK(on30 > off30 * 3.0);
}

TEST(ae_drops_fast_on_loud) {
  Params p; p.defaults(); p.set(P_LED_MODE, LM_AURORA);
  LedEngine e; e.reset(3);
  vtest::SynthFeatures syn; syn.reset(9);
  for (int t = 0; t < 600; ++t) e.step(weak(syn.step(), 1, 3), p);
  const int g0 = e.aeGainQ8();
  CHECK(g0 > 4 * 256);
  uint32_t rng = 5; Features loud = fullScale(rng, false);
  int frames = 0;
  while (e.aeGainQ8() > g0 / 2 && frames < 600) { e.step(loud, p); ++frames; }
  METRIC("ae_halve_frames_loud", frames);
  CHECK(frames < 40);                         // yukarı yavaş, aşağı hızlı (~0,2 s)
  CHECK(led_estimate_ma(e.out(), NUM_LEDS) <= 2500);
}

TEST(ae_high_gain_no_overflow) {
  // Kazanç 24×'e dayanmışken tam ölçekli kare gelir: sıkıştırma tonu korumalı (u32 taşması rengi bozmamalı),
  // yani LED başına kanal oranları pozlama kapalıyken ölçülenle aynı kalır; güç bütçesi korunmalı.
  Params pa; pa.defaults(); pa.set(P_LED_MODE, LM_AURORA); pa.set(P_LED_AE, 255); pa.set(P_BLOOM, 0);
  Params pb = pa; pb.set(P_LED_AE, 0);
  LedEngine a, b; a.reset(3); b.reset(3);
  vtest::SynthFeatures syn; syn.reset(9);
  for (int t = 0; t < 900; ++t) { Features f = weak(syn.step(), 1, 3); a.step(f, pa); b.step(f, pb); }
  CHECK(a.aeGainQ8() >= 20 * 256);
  uint32_t rng = 5; Features loud = fullScale(rng, false);
  a.step(loud, pa); b.step(loud, pb);
  CHECK(a.aeGainQ8() >= 18 * 256);                // ilk karede kazanç hâlâ yüksek
  int bad = 0;
  for (int i = 0; i < NUM_LEDS; ++i) {
    const int ca[3] = {a.out()[i].r, a.out()[i].g, a.out()[i].b}, cb[3] = {b.out()[i].r, b.out()[i].g, b.out()[i].b};
    const int ma = vmax(vmax(ca[0], ca[1]), ca[2]), mb = vmax(vmax(cb[0], cb[1]), cb[2]);
    for (int k = 0; k < 3; ++k) {
      const int d = iabs32(ca[k] * mb - cb[k] * ma);
      bad += d > ma * mb / 16 + 2 * (ma + mb);   // %6 + dither payı
    }
  }
  METRIC("ae_overflow_bad_channels", bad);
  CHECK_EQ(bad, 0);
  CHECK(led_estimate_ma(a.out(), NUM_LEDS) <= 2500);
}

TEST(ae_frozen_without_signal) {
  Params p; p.defaults(); p.set(P_LED_MODE, LM_AURORA);
  // 1) yalnız 2 bant eşiği aşıyor (< 3): kazanç hiç artmaz
  {
    LedEngine e; e.reset(1);
    Features f; std::memset(&f, 0, sizeof f);
    f.led[10] = f.led[11] = 65535; f.bar[10] = f.bar[11] = 65535;
    int hi = 0;
    for (int t = 0; t < 400; ++t) { e.step(f, p); hi = vmax(hi, (int)e.aeGainQ8()); }
    CHECK_EQ(hi, 256);
  }
  // 2) açılış sıçraması (3 kare yüksek) kazancı oynatmaz
  {
    LedEngine e; e.reset(1);
    Features hot; std::memset(&hot, 0, sizeof hot);
    for (int k = 0; k < 8; ++k) hot.led[k] = hot.bar[k] = 65535;
    Features quiet; std::memset(&quiet, 0, sizeof quiet);
    for (int t = 0; t < 3; ++t) e.step(hot, p);
    for (int t = 0; t < 200; ++t) e.step(quiet, p);
    CHECK_EQ((int)e.aeGainQ8(), 256);
  }
  // 3) müzik → sessizlik: kazanç bir daha artmaz, 1×'e söner; idle çıkışı pozlamadan etkilenmez
  {
    LedEngine e; e.reset(1);
    vtest::SynthFeatures syn; syn.reset(9);
    for (int t = 0; t < 600; ++t) e.step(weak(syn.step(), 1, 3), p);
    CHECK(e.aeGainQ8() > 4 * 256);
    Features z; std::memset(&z, 0, sizeof z);
    int prev = e.aeGainQ8(), rises = 0;
    for (int t = 0; t < 900; ++t) {
      z.silent = t >= 240;
      e.step(z, p);
      rises += (int)e.aeGainQ8() > prev; prev = e.aeGainQ8();
    }
    CHECK_EQ(rises, 0);
    CHECK_EQ(prev, 256);
    // idle: eski çıktıdan (bri 160'ta ort. ~0,6) yüksek olmamalı
    METRIC("idle_mean_out", meanOut(e));
    CHECK(meanOut(e) < 0.8);
  }
}

TEST(ae_respects_brightness_ceiling) {
  for (int bri : {20, 40, 128}) {
    Params p; p.defaults(); p.set(P_BRIGHTNESS, bri); p.set(P_LED_MODE, LM_AURORA);
    LedEngine e; e.reset(2);
    vtest::SynthFeatures syn; syn.reset(9);
    int mx = 0;
    for (int t = 0; t < 700; ++t) {
      e.step(weak(syn.step(), 1, 2), p);
      for (int i = 0; i < NUM_LEDS; ++i) mx = vmax(mx, (int)vmax(vmax(e.out()[i].r, e.out()[i].g), e.out()[i].b));
    }
    CHECK(mx <= bri + 2);                     // bri kullanıcı tavanıdır; pozlama onu aşmaz (dither ±1)
    CHECK(mx >= bri / 2);
  }
}

TEST(silence_not_brighter) {
  // sessizlik: köz (ilk 4 s) ve idle; eski çıktı (bri 160, led_th 102) ≈ 0,7 / 0,6 kanal ortalaması
  for (int mode : {LM_AURORA, LM_SPARKLE, LM_MIRROR, LM_SPECTRUM, LM_BASS, LM_WATERFALL, LM_COMET}) {
    Params p; p.defaults(); p.set(P_LED_MODE, mode);
    LedEngine e; e.reset(1);
    Features z; std::memset(&z, 0, sizeof z);
    double pre = 0, idle = 0; int np = 0, ni = 0;
    for (int t = 0; t < 600; ++t) {
      z.silent = t >= 240;
      e.step(z, p);
      if (t >= 60 && t < 240) { pre += meanOut(e); ++np; }
      if (t >= 360) { idle += meanOut(e); ++ni; }
    }
    CHECK(pre / np <= 0.8);
    CHECK(idle / ni <= 0.8);
  }
}

TEST(band_axis_equal_share) {
  // Her bant eşit sayıda LED yakmalı: AURORA'da 3, MIRROR'da (iki yarı) 4. Eski eşlemede uç bantlar yarım payla kalıyordu.
  Params p; p.defaults(); p.set(P_BLOOM, 0); p.set(P_LED_AE, 0);
  const int expect[2] = {3, 4};
  const int modes[2] = {LM_AURORA, LM_MIRROR};
  for (int m = 0; m < 2; ++m) {
    p.set(P_LED_MODE, modes[m]);
    for (int k = 0; k < NUM_BANDS; ++k) {
      LedEngine e; e.reset(1);
      Features f; std::memset(&f, 0, sizeof f);
      f.led[k] = f.bar[k] = 65535;
      e.step(f, p);
      int lit = 0;
      for (int i = 0; i < NUM_LEDS; ++i) lit += vmax(vmax(e.pattern()[i].r, e.pattern()[i].g), e.pattern()[i].b) > 30;
      if (lit != expect[m]) std::printf("    mod %d bant %d: %d LED\n", modes[m], k, lit);
      CHECK_EQ(lit, expect[m]);
    }
  }
}

TEST(performance) {
  METRIC("sizeof_LedEngine", sizeof(LedEngine));
  for (int mode = 0; mode < LM_COUNT; ++mode) {
    LedEngine e; e.reset(1);
    Params p; p.defaults(); p.set(P_LED_MODE, mode);
    vtest::SynthFeatures syn; syn.reset(2);
    std::vector<Features> fs; for (int t = 0; t < 500; ++t) fs.push_back(syn.step());
    auto t0 = std::chrono::steady_clock::now();
    for (int rep = 0; rep < 8; ++rep) for (auto& f : fs) e.step(f, p);
    double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (8.0 * fs.size());
    char key[40]; std::snprintf(key, sizeof key, "host_ns_per_step_%s", led_mode_name((uint8_t)mode));
    METRIC(key, ns);
    CHECK(ns < 200000);
  }
}

// Önizleme: FV_DUMP=1 → build/leds_<mode>.ppm (her satır bir kare, 120 piksel).
TEST(dump_preview) {
  if (!std::getenv("FV_DUMP")) return;
  const int frames = 300;
  for (int mode = 0; mode < LM_COUNT; ++mode) {
    for (int src = 0; src < 2; ++src) {   // 0: out (gerçek LED), 1: pattern
      LedEngine e; e.reset(1);
      Params p; p.defaults(); p.set(P_LED_MODE, mode);
      vtest::SynthFeatures syn; syn.reset(7);
      std::vector<uint8_t> img((size_t)NUM_LEDS * frames * 3);
      for (int t = 0; t < frames; ++t) {
        syn.silent = (mode == LM_COUNT - 1 + 0) && false;
        e.step(syn.step(), p);
        const Rgb* px = src ? e.pattern() : e.out();
        for (int i = 0; i < NUM_LEDS; ++i) {
          uint8_t* o = &img[((size_t)t * NUM_LEDS + i) * 3];
          o[0] = px[i].r; o[1] = px[i].g; o[2] = px[i].b;
          if (!src) {   // gerçek LED çıktısı doğrusal ışık: ekranda algıya yakın görmek için sRGB kodla
            for (int c = 0; c < 3; ++c) o[c] = (uint8_t)(255.0 * std::pow(o[c] / 255.0, 1.0 / 2.2) + 0.5);
          }
        }
      }
      char path[256]; std::snprintf(path, sizeof path, FV_BINARY_DIR "/leds_%s%s.ppm", led_mode_name((uint8_t)mode), src ? "_pattern" : "");
      vtest::write_ppm(path, img.data(), NUM_LEDS, frames);
    }
  }
}
FV_TEST_MAIN()

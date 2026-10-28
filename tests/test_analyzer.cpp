// test_analyzer: audio-spectrum-analysis spesifikasyon senaryoları + doğrusallık, AGC, cap, maliyet.
#include <chrono>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;

static double todb(lvl l) { return l / 85.0403; }

// 24 kHz sinüs besleyici (desimatörü atlar).
struct Tone {
  double f = 1000, amp = 0, ph = 0;
  void fill(q15* x, int n) {
    for (int i = 0; i < n; ++i) {
      x[i] = (q15)std::lround(amp * std::sin(ph));
      ph += 2.0 * M_PI * f / FS;
    }
  }
};

// Bu dosyadaki algoritma testleri eski geniş aralıkta (47 Hz – 10 kHz) koşar: bas bantları da
// ölçülür (100 Hz tonu, sessizlikte tüm bantlar). Varsayılan aralık (fmin 250) test_bands.cpp'de.
static constexpr int kLegacyFmin = 47;

static const BandMap& legacy_map() {
  static BandMap m;
  static bool init = false;
  if (!init) { make_band_map(kLegacyFmin, 10000, &m); init = true; }
  return m;
}

static int band_of(double hz) {
  double k = hz * FFT_N / FS;
  const BandMap& m = legacy_map();
  for (int b = 0; b < NUM_BANDS; ++b)
    if (k >= m.edge[b] - 0.5 && k < m.edge[b + 1] - 0.5) return b;
  return -1;
}

static Engine& fresh_engine(uint32_t seed = 1) {
  static Engine e;
  e.params.defaults();
  e.params.set(P_FMIN, kLegacyFmin);
  e.reset(seed);
  return e;
}

static void run_tone(Engine& e, Tone& t, int frames) {
  static q15 x[HOP];
  for (int i = 0; i < frames; ++i) { t.fill(x, HOP); e.pushPcm(x, HOP); }
}

static void run_synth(Engine& e, Synth& s, int frames) {
  static uint16_t adc[ADC_BLOCK];
  for (int i = 0; i < frames; ++i) { s.render(adc, ADC_BLOCK); e.pushAdc(adc, ADC_BLOCK); }
}

// Spesifikasyon: 1 kHz sinüs doğru banda düşer, komşu olmayan bantlar ≥ 20 dB aşağıda.
TEST(sine_1k_lands_in_band) {
  const int want = band_of(1000.0);
  CHECK(want >= 0);
  // (a) doğrudan PCM, −6 dBFS
  {
    Engine& e = fresh_engine();
    Tone t{1000.0, 16384.0};
    run_tone(e, t, 30);
    const Features& f = e.features();
    int best = 0;
    for (int b = 1; b < NUM_BANDS; ++b) if (f.raw[b] > f.raw[best]) best = b;
    CHECK_EQ(best, want);
    double worstGap = 1e9;
    for (int b = 0; b < NUM_BANDS; ++b)
      if (std::abs(b - want) > 1) worstGap = std::fmin(worstGap, todb(f.raw[want] - f.raw[b]));
    METRIC("nonadjacent_gap_db", worstGap);
    CHECK(worstGap >= 20.0);
    int bestLev = 0;
    for (int b = 1; b < NUM_BANDS; ++b) if (f.level[b] > f.level[bestLev]) bestLev = b;
    CHECK_EQ(bestLev, want);
  }
  // (b) ADC modeli (SC_SINE1K, 12 bit + gürültü, desimasyon)
  {
    Engine& e = fresh_engine();
    static Synth s;
    s.reset(SC_SINE1K, 1, -6);
    run_synth(e, s, 30);
    const Features& f = e.features();
    int best = 0;
    for (int b = 1; b < NUM_BANDS; ++b) if (f.raw[b] > f.raw[best]) best = b;
    CHECK_EQ(best, want);
    double worstGap = 1e9;
    for (int b = 0; b < NUM_BANDS; ++b)
      if (std::abs(b - want) > 1) worstGap = std::fmin(worstGap, todb(f.raw[want] - f.raw[b]));
    METRIC("adc_nonadjacent_gap_db", worstGap);
    CHECK(worstGap >= 20.0);
  }
}

// LVL_FS: tam ölçek sinüsün (genlik 32767) tiltsiz bant seviyesi 0 dBFS ± 0.5 dB.
TEST(full_scale_reference) {
  const int b = band_of(1000.0);
  Engine& e = fresh_engine();
  Tone t{1000.0, 32767.0};
  run_tone(e, t, 20);
  const Features& f = e.features();
  lvl untilted = f.raw[b] - legacy_map().tilt[b] * e.params.get(P_TILT) / 15;
  METRIC("fs_level_lvl_abs", untilted + LVL_FS);
  CHECK(std::abs(untilted) <= dB(0.5));
  CHECK_EQ((int)f.bfpShift, 0);
}

// Doğrusallık −60…0 dBFS ±1 dB: (a) q15 PCM, (b) 12 bit ADC modeli (gürültü ±3 LSB).
TEST(linearity) {
  const int b = band_of(1000.0);
  const int tilt = legacy_map().tilt[b] * 15 / 15;
  double worst = 0, worstAdc = 0;
  for (int db = 0; db >= -80; db -= 10) {
    Engine& e = fresh_engine();
    Tone t{1000.0, 32767.0 * std::pow(10.0, db / 20.0)};
    run_tone(e, t, 20);
    double got = todb(e.features().raw[b] - tilt);
    char key[48];
    std::snprintf(key, sizeof key, "pcm_err_db_at_%d", db);
    METRIC(key, got - db);
    if (db >= -60) worst = std::fmax(worst, std::fabs(got - db));
  }
  for (int db = 0; db >= -60; db -= 10) {
    Engine& e = fresh_engine();
    static Synth s;
    s.reset(SC_SINE1K, 1, (int8_t)db);
    run_synth(e, s, 20);
    double got = todb(e.features().raw[b] - tilt);
    char key[48];
    std::snprintf(key, sizeof key, "adc_err_db_at_%d", db);
    METRIC(key, got - db);
    worstAdc = std::fmax(worstAdc, std::fabs(got - db));
  }
  METRIC("pcm_max_err_db_0_to_m60", worst);
  METRIC("adc_max_err_db_0_to_m60", worstAdc);
  CHECK(worst <= 1.0);
  CHECK(worstAdc <= 1.0);
}

// Spesifikasyon: yalnız ADC gürültüsü → 1 s sonra tüm barlar 0; 4 s sonra silent. 6 tohum.
TEST(silence_bars_zero_and_silent) {
  int firstSilentWorst = 0, barFrames = 0, beats = 0, onsets = 0;
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    Engine& e = fresh_engine(seed);
    static Synth s;
    s.reset(SC_SILENCE, seed, 0);
    static uint16_t adc[ADC_BLOCK];
    int firstSilent = -1;
    for (int fr = 0; fr < 6 * FRAME_HZ; ++fr) {
      s.render(adc, ADC_BLOCK);
      e.pushAdc(adc, ADC_BLOCK);
      const Features& f = e.features();
      if (fr >= FRAME_HZ) {
        for (int b = 0; b < NUM_BANDS; ++b) barFrames += f.bar[b] != 0;
        beats += f.beat;
        onsets += f.onset;
      }
      if (f.silent && firstSilent < 0) firstSilent = fr + 1;
      if (firstSilent >= 0) CHECK(f.silent == 1);
    }
    CHECK(firstSilent > 0);
    firstSilentWorst = vmax(firstSilentWorst, firstSilent);
  }
  METRIC("first_silent_frame_worst", firstSilentWorst);
  METRIC("nonzero_bar_band_frames", barFrames);
  CHECK_EQ(barFrames, 0);
  CHECK_EQ(beats, 0);
  CHECK_EQ(onsets, 0);
  // 240 sessiz kare; ilk ~0.1 s gürültü tabanı öğrenilirken tek tük aktif kare olabilir.
  CHECK(firstSilentWorst <= SILENCE_FRAMES + 15);
}

// Sessizlikten çıkış hızlı olmalı (≤ 3 kare).
TEST(silence_exit_fast) {
  Engine& e = fresh_engine();
  static Synth s;
  s.reset(SC_SILENCE, 3, 0);
  run_synth(e, s, 5 * FRAME_HZ);
  CHECK(e.features().silent == 1);
  s.reset(SC_MIX, 3, 0);
  int fr = 0;
  static uint16_t adc[ADC_BLOCK];
  while (e.features().silent && fr < 60) { s.render(adc, ADC_BLOCK); e.pushAdc(adc, ADC_BLOCK); ++fr; }
  METRIC("exit_frames", fr);
  CHECK(fr <= 4);
}

// Spesifikasyon: giriş 20 dB düşer → AGC birkaç saniyede uyum sağlar, barlar görünür aralığa döner.
TEST(agc_recovers_after_20db_drop) {
  Engine& e = fresh_engine();
  static Synth s;
  s.reset(SC_MIX, 1, 0);
  static uint16_t adc[ADC_BLOCK];
  double rBefore = 0;
  for (int fr = 0; fr < 6 * FRAME_HZ; ++fr) {
    s.render(adc, ADC_BLOCK);
    e.pushAdc(adc, ADC_BLOCK);
    if (fr >= 5 * FRAME_HZ) rBefore += todb(e.features().ceiling) / FRAME_HZ;
  }
  s.reset(SC_MIX, 1, -20);
  std::vector<double> r;
  std::vector<int> maxBar;
  for (int fr = 0; fr < 10 * FRAME_HZ; ++fr) {
    s.render(adc, ADC_BLOCK);
    e.pushAdc(adc, ADC_BLOCK);
    r.push_back(todb(e.features().ceiling));
    int mb = 0;
    for (int b = 0; b < NUM_BANDS; ++b) mb = vmax(mb, (int)e.features().bar[b]);
    maxBar.push_back(mb);
  }
  double rAfter = 0;
  for (int i = 9 * FRAME_HZ; i < 10 * FRAME_HZ; ++i) rAfter += r[i] / FRAME_HZ;
  // uyum süresi: R'nin son değere 3 dB yaklaştığı ilk an
  int settle = -1;
  for (size_t i = 0; i < r.size(); ++i) if (std::fabs(r[i] - rAfter) <= 3.0) { settle = (int)i; break; }
  int peakBarLast = 0;
  for (int i = 9 * FRAME_HZ; i < 10 * FRAME_HZ; ++i) peakBarLast = vmax(peakBarLast, maxBar[i]);
  METRIC("ceiling_before_dbfs", rBefore);
  METRIC("ceiling_after_dbfs", rAfter);
  METRIC("settle_s", settle / (double)FRAME_HZ);
  METRIC("peak_bar_after", peakBarLast / 65535.0);
  CHECK(std::fabs((rBefore - rAfter) - 20.0) <= 3.0);
  CHECK(settle >= 0 && settle <= 5 * FRAME_HZ);
  CHECK(peakBarLast >= 0.7 * 65535);
}

// Bar attack/release: kare başına katsayılar (0.60 / 0.12) ve 1 s içinde sıfıra iniş.
TEST(bar_attack_release) {
  Engine& e = fresh_engine();
  const int b = band_of(1000.0);
  Tone t{1000.0, 16384.0};
  run_tone(e, t, 60);
  const u16n top = e.features().bar[b];
  CHECK(top > 50000);
  // release: sessiz giriş
  Tone z{1000.0, 0.0};
  std::vector<int> bar;
  for (int i = 0; i < 90; ++i) { run_tone(e, z, 1); bar.push_back(e.features().bar[b]); }
  // pencere boşaldıktan sonra (3 kare) oran ≈ 1 − 31/256
  double ratio = (double)bar[8] / bar[7];
  METRIC("release_ratio", ratio);
  CHECK_NEAR(ratio, 1.0 - 31.0 / 256.0, 0.01);
  int zeroAt = -1;
  for (int i = 0; i < 90; ++i) if (bar[i] == 0) { zeroAt = i; break; }
  METRIC("release_to_zero_ms", zeroAt * 1000.0 / FRAME_HZ);
  CHECK(zeroAt > 0 && zeroAt <= FRAME_HZ);
  // attack: sessizlikten tona; ilk tam kare sonrası adım ≈ 0.60·(hedef − bar)
  Engine& e2 = fresh_engine();
  Tone z2{1000.0, 0.0};
  run_tone(e2, z2, 30);
  Tone t2{1000.0, 16384.0};
  std::vector<int> up;
  for (int i = 0; i < 10; ++i) { run_tone(e2, t2, 1); up.push_back(e2.features().bar[b]); }
  int reach90 = -1;
  for (int i = 0; i < 10; ++i) if (up[i] >= 0.9 * up[9]) { reach90 = i + 1; break; }
  METRIC("attack_90pct_frames", reach90);
  CHECK(reach90 > 0 && reach90 <= 6);
}

// Spesifikasyon: tam seviyeden sessizliğe → şapka ~18 kare bekler, ~0.75 s'de tabana iner.
TEST(cap_hold_and_gravity) {
  Engine& e = fresh_engine();
  const int b = band_of(1000.0);
  Tone t{1000.0, 32767.0};
  run_tone(e, t, 60);
  Tone z{1000.0, 0.0};
  std::vector<int> cap, bar, hold;
  for (int i = 0; i < 120; ++i) {
    run_tone(e, z, 1);
    cap.push_back(e.features().cap[b]);
    bar.push_back(e.features().bar[b]);
    hold.push_back(e.features().capHold[b]);
  }
  // bar şapkanın altına indiği ilk kare → şapka sabit kaldığı kare sayısı
  int below = -1;
  for (int i = 0; i < 120; ++i) if (bar[i] < cap[i]) { below = i; break; }
  CHECK(below >= 0);
  int held = 0;
  while (below + held < 120 && cap[below + held] == cap[below]) ++held;
  int zero = -1;
  for (int i = below + held; i < 120; ++i) if (cap[i] == 0) { zero = i; break; }
  const double fallS = (zero - (below + held) + 1) / (double)FRAME_HZ;
  METRIC("hold_frames", held);
  METRIC("fall_s", fallS);
  METRIC("cap_start", cap[below] / 65535.0);
  CHECK(held >= 17 && held <= 19);
  CHECK(zero > 0);
  CHECK(fallS >= 0.55 && fallS <= 0.85);
}

// Spesifikasyon: 10 s ADC → 600 ± 6 kare (sıfır dolgulu başlangıç: tam 600). Parça boyutu bağımsız.
TEST(frame_rate_600_per_10s) {
  Engine& e = fresh_engine();
  static Synth s;
  s.reset(SC_MIX, 1, 0);
  std::vector<uint16_t> adc(10 * FS_ADC);
  s.render(adc.data(), (int)adc.size());
  uint32_t r = 99;
  int frames = 0;
  size_t i = 0;
  while (i < adc.size()) {
    int n = 1 + (int)(xorshift32(r) % 1500);
    if (i + n > adc.size()) n = (int)(adc.size() - i);
    frames += e.pushAdc(adc.data() + i, n);
    i += n;
  }
  METRIC("frames", frames);
  CHECK(std::abs(frames - 600) <= 6);
  CHECK_EQ(frames, 600);
  CHECK_EQ((int)e.features().frame, 600);
}

// Determinizm: aynı senaryo iki kez → aynı hash; farklı parça boyutları → aynı özellikler.
TEST(determinism) {
  for (int sc = 0; sc < SC_COUNT; ++sc) {
    uint32_t a = scenario_hash((Scenario)sc, 120, 1, GH_FEATURES);
    uint32_t b = scenario_hash((Scenario)sc, 120, 1, GH_FEATURES);
    CHECK_EQ(a, b);
  }
  CHECK(scenario_hash(SC_PINK, 60, 1, GH_FEATURES) != scenario_hash(SC_PINK, 60, 2, GH_FEATURES));
  // parça boyutu bağımsızlığı
  static Engine e1, e2;
  e1.params.defaults(); e1.reset(1);
  e2.params.defaults(); e2.reset(1);
  static Synth s;
  s.reset(SC_MIX, 7, 0);
  std::vector<uint16_t> adc(3 * FS_ADC);
  s.render(adc.data(), (int)adc.size());
  e1.pushAdc(adc.data(), (int)adc.size());
  for (size_t i = 0; i < adc.size(); i += 333) e2.pushAdc(adc.data() + i, (int)vmin<size_t>(333, adc.size() - i));
  const Features& f1 = e1.features();
  const Features& f2 = e2.features();
  CHECK(std::memcmp(f1.bar, f2.bar, sizeof f1.bar) == 0);
  CHECK(std::memcmp(f1.raw, f2.raw, sizeof f1.raw) == 0);
  CHECK_EQ(f1.frame, f2.frame);
}

// Sabit kazanç modu: R = P_GAIN_DB.
TEST(fixed_gain) {
  Engine& e = fresh_engine();
  e.params.set(P_GAIN_DB, -20);
  Tone t{1000.0, 16384.0};
  run_tone(e, t, 30);
  CHECK_EQ(e.features().ceiling, dB(-20));
}

// Gürültü kalibrasyonu: AGC tabanından yüksek gürültü (gerçek donanım benzeri) kalibrasyonsuz
// barlarda görünür; calibrateNoise sonrası barlar 0 olur.
TEST(noise_calibration) {
  Engine& e = fresh_engine();
  static q15 x[HOP];
  uint32_t r = 12345;
  auto noise = [&](int frames) {
    for (int i = 0; i < frames; ++i) {
      for (int k = 0; k < HOP; ++k) x[k] = (q15)((int32_t)(xorshift32(r) % 4001) - 2000);  // ≈ −27 dBFS rms
      e.pushPcm(x, HOP);
    }
  };
  noise(5 * FRAME_HZ);
  int before = 0;
  for (int b = 0; b < NUM_BANDS; ++b) before += e.features().bar[b] > 0;
  e.analyzer().calibrateNoise(2 * FRAME_HZ);
  noise(2 * FRAME_HZ);
  noise(FRAME_HZ);  // kalibrasyon sırasında oluşmuş barların sönmesi (release ≤ 1 s)
  int after = 0;
  for (int i = 0; i < 2 * FRAME_HZ; ++i) {
    noise(1);
    for (int b = 0; b < NUM_BANDS; ++b) after += e.features().bar[b] > 0;
  }
  METRIC("bars_before_cal", before);
  METRIC("bar_band_frames_after_cal", after);
  CHECK(before > 0);
  CHECK_EQ(after, 0);
}

// Centroid hue: düşük ton → küçük hue, yüksek ton → büyük hue (0..200).
TEST(centroid_hue) {
  Engine& e = fresh_engine();
  Tone lo{100.0, 16384.0};
  run_tone(e, lo, 120);
  int hLo = e.features().centroidHue;
  Engine& e2 = fresh_engine();
  Tone hi{6000.0, 16384.0};
  run_tone(e2, hi, 120);
  int hHi = e2.features().centroidHue;
  METRIC("hue_100hz", hLo);
  METRIC("hue_6khz", hHi);
  CHECK(hLo < 40);
  CHECK(hHi > 150 && hHi <= 200);
}

// Onset: tiz patlamalar (hi-hat benzeri) tetikler, sabit tiz ton tetiklemez.
TEST(onset_hihat) {
  Engine& e = fresh_engine();
  static q15 x[HOP];
  uint32_t r = 7;
  int onsets = 0;
  for (int fr = 0; fr < 4 * FRAME_HZ; ++fr) {
    bool burst = (fr % 15) < 2;
    int32_t prev = 0;
    for (int k = 0; k < HOP; ++k) {
      int32_t w = burst ? (int32_t)(xorshift32(r) % 16001) - 8000 : 0;
      x[k] = (q15)vclamp(w - prev, -32767, 32767);  // birinci fark → tiz
      prev = w;
    }
    e.pushPcm(x, HOP);
    if (fr >= 30) onsets += e.features().onset;
  }
  METRIC("onsets_in_3_5s", onsets);
  CHECK(onsets >= 12);
  Engine& e2 = fresh_engine();
  Tone hi{6000.0, 16384.0};
  int steady = 0;
  for (int fr = 0; fr < 4 * FRAME_HZ; ++fr) { run_tone(e2, hi, 1); if (fr >= 30) steady += e2.features().onset; }
  CHECK_EQ(steady, 0);
}

// DSP maliyeti (host): desimasyon + analiz, kare başına ns.
TEST(dsp_cost_host) {
  Engine& e = fresh_engine();
  static Synth s;
  s.reset(SC_MIX, 1, 0);
  const int frames = 1200;
  std::vector<uint16_t> adc(frames * ADC_BLOCK);
  s.render(adc.data(), (int)adc.size());
  static Decimator d;
  d.reset();
  static q15 pcm[ADC_BLOCK / 2];
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < frames; ++i) d.process(adc.data() + i * ADC_BLOCK, ADC_BLOCK, pcm);
  auto t1 = std::chrono::steady_clock::now();
  static Analyzer an;
  Params p;
  p.defaults();
  an.reset(p);
  static q15 frame[FFT_N];
  for (int i = 0; i < FFT_N; ++i) frame[i] = (q15)(adc[i] * 8 - 16384);
  auto t2 = std::chrono::steady_clock::now();
  for (int i = 0; i < frames; ++i) { frame[i & 1023] ^= 1; an.analyze(frame, p); }
  auto t3 = std::chrono::steady_clock::now();
  double decNs = std::chrono::duration<double, std::nano>(t1 - t0).count() / frames;
  double anNs = std::chrono::duration<double, std::nano>(t3 - t2).count() / frames;
  METRIC("decim_ns_per_frame", decNs);
  METRIC("analyze_ns_per_frame", anNs);
  // Kaba işlem sayımı (kare başına): FFT radix-2² 4 geçiş × 128 kelebek × 3 kompleks çarpma +
  // radix-2 + split 256 + desimasyon 400×13 MAC.
  METRIC("cmul_per_frame", 4 * 128 * 3 + 256);
  METRIC("decim_mac_per_frame", 400 * 13);
  CHECK(anNs > 0);
}

FV_TEST_MAIN()

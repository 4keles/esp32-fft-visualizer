// test_bands: ayarlanabilir frekans aralığı (fmin/fmax) — bant düzeni üretimi, yeniden başlatma ve
// beat'in görüntü aralığından bağımsızlığı.
#include <vector>
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;

static long rha(double x) { return (long)std::floor(x + 0.5); }

// Eski sabit tablo (tools/gen_tables.cpp, 47 Hz – 10 kHz): çalışma zamanı üretici bunu birebir vermeli.
TEST(bands_legacy_range_exact) {
  static const int E0[41] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 25, 28, 32, 36, 40, 45,
                             51, 57, 64, 72, 81, 91, 102, 115, 130, 146, 164, 185, 208, 234, 264, 298, 336, 379, 427};
  static const int T0[40] = {-569, -491, -437, -395, -362, -333, -308, -287, -267, -250, -227, -199, -175, -154,
                             -135, -114, -91, -68, -45, -24, -3, 19, 41, 62, 84, 106, 128, 149, 171, 193, 215,
                             236, 258, 280, 302, 324, 346, 368, 391, 413};
  BandMap m;
  make_band_map(47, 10000, &m);
  int bad = 0, badT = 0;
  for (int i = 0; i <= NUM_BANDS; ++i) bad += m.edge[i] != E0[i];
  for (int i = 0; i < NUM_BANDS; ++i) badT += m.tilt[i] != T0[i];
  CHECK_EQ(bad, 0);
  CHECK_EQ(badT, 0);
  // bas kanalı tilt'i = aynı kutulardaki eski bantların tilt'i (bantlar 0..4 = kutu 2..6)
  for (int i = 0; i < BASS_BINS; ++i) CHECK_EQ((int)m.bassTilt[i], (int)T0[i]);
}

// Tarama: her (fmin, fmax) için uçlar, monotonluk, ≥ 1 kutu, her adım en yakın tamsayıya yuvarlanmış
// ideal log aralığı (çift hassasiyetli referans, ±1e-4 kutu: .5 eşitliklerinde iki seçim de geçerli),
// tilt referansa ±1 lvl.
TEST(bands_sweep_valid) {
  const double df = (double)FS / FFT_N;
  long cases = 0, bad = 0, tiltBad = 0;
  for (int fmin = 40; fmin <= 1500; fmin += 13) {
    for (int fmax = 4500; fmax <= 10000; fmax += 250) {
      BandMap m;
      make_band_map(fmin, fmax, &m);
      ++cases;
      const int lo = (int)rha(fmin / df), hi = (int)std::ceil(fmax / df);
      bool ok = m.edge[0] == (lo < BASS_BIN_LO ? BASS_BIN_LO : lo) && m.edge[NUM_BANDS] == hi;
      for (int b = 0; b < NUM_BANDS && ok; ++b) {
        const int e = m.edge[b], nx = m.edge[b + 1];
        ok = nx >= e + 1 && nx <= hi - (NUM_BANDS - b - 1);
        const double ideal = e * std::pow((double)hi / e, 1.0 / (NUM_BANDS - b));
        if (ok && nx > e + 1 && nx < hi - (NUM_BANDS - b - 1)) ok = std::fabs(nx - ideal) <= 0.5 + 1e-4;
      }
      bad += !ok;
      for (int b = 0; b < NUM_BANDS; ++b) {
        const double flo = (m.edge[b] - 0.5) * df, fhi = (m.edge[b + 1] - 0.5) * df;
        const long t = rha(1.5 * std::log2(std::sqrt(flo * fhi) / 1000.0) * 85.0403);
        tiltBad += std::abs(t - (long)m.tilt[b]) > 1;
        if (b) tiltBad += m.tilt[b] <= m.tilt[b - 1];
      }
    }
  }
  METRIC("cases", (double)cases);
  CHECK_EQ(bad, 0);
  CHECK_EQ(tiltBad, 0);
}

// Aşırı girişler: çok dar aralık ≥ 40 kutuya genişler; FFT_BINS aşılmaz.
TEST(bands_degenerate_inputs) {
  BandMap m;
  make_band_map(1500, 1600, &m);  // dar → 40 kutu
  CHECK_EQ(m.edge[NUM_BANDS] - m.edge[0], NUM_BANDS);
  for (int b = 0; b < NUM_BANDS; ++b) CHECK_EQ(m.edge[b + 1] - m.edge[b], 1);
  make_band_map(40, 12000, &m);  // fmax FFT_BINS'e kısılır
  CHECK(m.edge[NUM_BANDS] <= FFT_BINS);
  CHECK_EQ((int)m.edge[0], 2);
  make_band_map(11000, 12000, &m);
  CHECK(m.edge[NUM_BANDS] <= FFT_BINS && m.edge[0] + NUM_BANDS <= m.edge[NUM_BANDS]);
  for (int b = 0; b < NUM_BANDS; ++b) CHECK(m.edge[b + 1] > m.edge[b]);
}

// Varsayılan aralık: ilk bant 250 Hz civarında başlar, boş bas çubukları yok; üst uç 10 kHz.
TEST(bands_default_range) {
  Params p;
  p.defaults();
  CHECK_EQ(p.get(P_FMIN), 250);
  CHECK_EQ(p.get(P_FMAX), 10000);
  BandMap m;
  make_band_map(p.get(P_FMIN), p.get(P_FMAX), &m);
  const double lowHz = (m.edge[0] - 0.5) * FS / FFT_N, topHz = (m.edge[NUM_BANDS] - 0.5) * FS / FFT_N;
  METRIC("low_edge_hz", lowHz);
  METRIC("top_edge_hz", topHz);
  CHECK(lowHz >= 230 && lowHz <= 270);
  CHECK(topHz >= 9900 && topHz <= 10100);
  int oneBin = 0;
  for (int b = 0; b < NUM_BANDS; ++b) oneBin += m.edge[b + 1] - m.edge[b] == 1;
  METRIC("one_bin_bands", oneBin);
  CHECK(oneBin <= 8);  // eski aralıkta 11 bant tek kutuydu (boşa giden bas çubukları)
}

// Parametre sınırları ve CLI/NVS adları.
TEST(bands_params) {
  ParamId id;
  CHECK(param_lookup("fmin", &id) && id == P_FMIN);
  CHECK(param_lookup("fmax", &id) && id == P_FMAX);
  Params p;
  p.defaults();
  p.set(P_FMIN, 5);
  CHECK_EQ(p.get(P_FMIN), 40);
  p.set(P_FMIN, 99999);
  CHECK_EQ(p.get(P_FMIN), 1500);
  p.set(P_FMAX, 100);
  CHECK_EQ(p.get(P_FMAX), 4500);
  p.set(P_FMAX, 99999);
  CHECK_EQ(p.get(P_FMAX), 10000);
}

namespace {
struct Tone {
  double f, amp, ph = 0;
  void fill(q15* x, int n) {
    for (int i = 0; i < n; ++i) { x[i] = (q15)std::lround(amp * std::sin(ph)); ph += 2.0 * M_PI * f / FS; }
  }
};
Engine& fresh(int fmin, int fmax = 10000) {
  static Engine e;
  e.params.defaults();
  e.params.set(P_FMIN, fmin);
  e.params.set(P_FMAX, fmax);
  e.reset(1);
  return e;
}
void tone(Engine& e, double hz, double amp, int frames) {
  static q15 x[HOP];
  static Tone t{1000, 0};
  t.f = hz; t.amp = amp;
  for (int i = 0; i < frames; ++i) { t.fill(x, HOP); e.pushPcm(x, HOP); }
}
}  // namespace

// Aralık değişince bant düzeni, tilt ve gürültü izleyicileri yeniden başlar; ton doğru banda düşer.
TEST(restart_on_range_change) {
  Engine& e = fresh(47);
  tone(e, 3000.0, 16384.0, 40);
  CHECK_EQ((int)e.features().edge[0], 2);
  const uint32_t frameBefore = e.features().frame;
  CHECK(frameBefore >= 40);
  const int fmins[] = {250, 600, 1200, 47, 40, 1500};
  for (int fmin : fmins) {
    e.params.set(P_FMIN, fmin);
    tone(e, 3000.0, 16384.0, 30);
    const Features& f = e.features();
    BandMap m;
    make_band_map(fmin, 10000, &m);
    int bad = 0;
    for (int i = 0; i <= NUM_BANDS; ++i) bad += f.edge[i] != m.edge[i];
    CHECK_EQ(bad, 0);
    CHECK(f.frame >= 25 && f.frame <= 31);  // izleyiciler yeniden başladı (kare sayacı sıfırlandı)
    int best = 0;
    for (int b = 1; b < NUM_BANDS; ++b) if (f.raw[b] > f.raw[best]) best = b;
    const double k = 3000.0 * FFT_N / FS;  // tonun kutusu
    CHECK(k >= m.edge[best] - 0.5 && k < m.edge[best + 1] - 0.5);
    // tilt tutarlı: ham seviye = tiltsiz seviye + bant tilt'i (tam ölçek referansı −6 dBFS ± 0,5)
    const lvl untilted = f.raw[best] - m.tilt[best] * e.params.get(P_TILT) / 15;
    CHECK_NEAR(untilted / 85.0403, -6.0, 0.7);
  }
  // fmax da aynı şekilde yeniden başlatır
  e.params.set(P_FMAX, 6000);
  tone(e, 3000.0, 16384.0, 3);
  CHECK(e.features().frame <= 4);
  BandMap m;
  make_band_map(1500, 6000, &m);
  CHECK_EQ((int)e.features().edge[NUM_BANDS], (int)m.edge[NUM_BANDS]);
}

// Aralık değişmeden analyze durumu bozulmaz (yanlış yeniden başlatma yok).
TEST(no_restart_without_change) {
  Engine& e = fresh(250);
  tone(e, 1000.0, 16384.0, 90);
  CHECK_EQ((int)e.features().frame, 90);
  e.params.set(P_FMIN, 250);  // aynı değer
  tone(e, 1000.0, 16384.0, 10);
  CHECK_EQ((int)e.features().frame, 100);
}

// Beat/bas, görüntü aralığından bağımsız: kick120 her fmin'de ~16 vuruş, BPM 120 ± 3; sabit ton yok.
TEST(beat_independent_of_fmin) {
  static Synth s;
  static uint16_t adc[ADC_BLOCK];
  const int fmins[] = {40, 47, 150, 250, 400, 800, 1500};
  for (int fmin : fmins) {
    Engine& e = fresh(fmin);
    s.reset(SC_KICK120, 1, 0);
    int beats = 0, bpm = 0;
    for (int fr = 0; fr < 8 * FRAME_HZ; ++fr) {
      s.render(adc, ADC_BLOCK);
      e.pushAdc(adc, ADC_BLOCK);
      beats += e.features().beat;
      bpm = e.features().bpm;
    }
    char key[32];
    std::snprintf(key, sizeof key, "beats_fmin_%d", fmin);
    METRIC(key, beats);
    CHECK(std::abs(beats - 16) <= 1);
    CHECK(std::abs(bpm - 120) <= 3);
    // sabit 100 Hz ton: yanlış vuruş yok (ilk saniyeden sonra)
    Engine& e2 = fresh(fmin);
    s.reset(SC_TONE, 1, 0);
    int late = 0;
    for (int fr = 0; fr < 8 * FRAME_HZ; ++fr) {
      s.render(adc, ADC_BLOCK);
      e2.pushAdc(adc, ADC_BLOCK);
      if (fr >= FRAME_HZ) late += e2.features().beat;
    }
    CHECK_EQ(late, 0);
  }
}

// Bas bölge enerjisi (LED bass) fmin'e bağlı değil: 100 Hz tonu fmin = 250 iken de bass'ı sürer.
TEST(bass_zone_works_with_high_fmin) {
  Engine& lo = fresh(47);
  tone(lo, 100.0, 16384.0, 60);
  const u16n bassLo = lo.features().bass;
  Engine& hi = fresh(1500);
  tone(hi, 100.0, 16384.0, 60);
  const u16n bassHi = hi.features().bass;
  METRIC("bass_fmin47", bassLo / 65535.0);
  METRIC("bass_fmin1500", bassHi / 65535.0);
  CHECK(bassLo > 20000);
  CHECK(bassHi > 20000);
  // ve görüntü barları (1500 Hz üstü) bu tonla yanmaz
  int lit = 0;
  for (int b = 0; b < NUM_BANDS; ++b) lit += hi.features().bar[b] > 0;
  CHECK_EQ(lit, 0);
}

// 3 s buton kalibrasyonu (EV_VLONG → controls::calibrate → Analyzer::calibrateNoise) yeni aralıkta da çalışır:
// ADC tabanından yüksek gürültü (gerçek donanım benzeri) kalibrasyonsuz bar üretir, `cal` sonrası barlar 0.
TEST(calibration_works_after_range_change) {
  static q15 x[HOP];
  uint32_t r = 4242;
  auto noise = [&](Engine& e, int frames) {
    for (int i = 0; i < frames; ++i) {
      for (int k = 0; k < HOP; ++k) x[k] = (q15)((int32_t)(xorshift32(r) % 4001) - 2000);
      e.pushPcm(x, HOP);
    }
  };
  const int fmins[] = {250, 600};
  for (int fmin : fmins) {
    Engine& e = fresh(47);
    noise(e, 3 * FRAME_HZ);
    e.params.set(P_FMIN, fmin);  // aralık değişti → yeniden başlar
    noise(e, 5 * FRAME_HZ);
    int before = 0;
    for (int b = 0; b < NUM_BANDS; ++b) before += e.features().bar[b] > 0;
    e.analyzer().calibrateNoise(2 * FRAME_HZ);
    noise(e, 3 * FRAME_HZ);
    int after = 0;
    for (int i = 0; i < 2 * FRAME_HZ; ++i) {
      noise(e, 1);
      for (int b = 0; b < NUM_BANDS; ++b) after += e.features().bar[b] > 0;
    }
    METRIC("bars_before_cal", before);
    METRIC("bar_band_frames_after_cal", after);
    CHECK_EQ(after, 0);
  }
}

// Regresyon koruması: fmin = 47 (eski sabit tablo aralığı) ile analiz, bu change'den ÖNCEKİ golden
// hash'leriyle birebir aynı (bas kanalları ilk bantlarla aynı kutular; tilt/kenar/AGC/beat değişmedi).
// GH_ALL de aynı: ekran ve LED, bant frekansına değil bar/level değerlerine bakar (LED: eski zamansal dither, dither=0;
// varsayılan sabit uzaysal dither led-flicker, yumuşak renk led-smooth change'inde geldi).
TEST(legacy_range_matches_previous_golden) {
  static const uint32_t FEAT[SC_COUNT] = {3106432838u, 1900599591u, 2603249403u, 2906962716u, 4160242735u, 387151148u, 3635158516u};
  static const uint32_t ALL[SC_COUNT] = {3618528613u, 1644974918u, 3054006128u, 2821277217u, 2098841109u, 544983115u, 734740560u};
  for (int sc = 0; sc < SC_COUNT; ++sc) {
    CHECK_EQ(scenario_hash((Scenario)sc, 240, 1, GH_FEATURES, LM_AURORA, 0, 47), FEAT[sc]);
    CHECK_EQ(scenario_hash((Scenario)sc, 240, 1, GH_ALL, LM_AURORA, 0, 47, 0, /*dither=*/0, /*smooth=*/0), ALL[sc]);
  }
}

FV_TEST_MAIN()

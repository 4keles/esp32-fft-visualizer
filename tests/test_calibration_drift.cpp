// test_calibration_drift: kalibrasyondan sonra uzun sessizlik.
// Gürültü: kartın gerçek sessizlik ham ADC kaydından çıkarılmış PSD'den (tests/data/mic_noise_psd.h) Gauss + rasgele
// faz ile sentezlenir (sqrt-Hann OLA, 48 kHz, 12 bit); tam hat (desimatör dahil) Engine::pushAdc ile beslenir.
// Bulgu: kalıcı gürültü artışında (ortam/ısınma/ekran) barlar kalibrasyondan sonra bile SÖNMÜYORDU
// (tavan N'yi ncal'e çiviliyordu); yalnız yeni `cal` kurtarıyordu.
#include <functional>
#include "fvtest.h"
#include "fftviz.h"
#include "noise_psd.h"

using namespace fv;

namespace {

using noisepsd::NoiseGen;

// Senaryo koşucusu: gain(t_s) dB ile gürültü (+ isteğe bağlı Synth tonu), kare kare callback.
struct Run {
  Engine e;
  NoiseGen ng;
  Synth sy;
  bool tone = false;
  double buf[512];
  int bi = 512;
  uint16_t adc[ADC_BLOCK];
  uint16_t mus[ADC_BLOCK];
  Run() { e.params.defaults(); e.reset(1); }
  // Bir hop (800 ADC örneği) üretir ve motora verir; kare üretildiyse true.
  bool step(double gainDb) {
    if (tone) sy.render(mus, ADC_BLOCK);
    for (int i = 0; i < ADC_BLOCK; ++i) {
      if (bi == 512) { ng.block(gainDb, buf); bi = 0; }
      double v = kMicNoiseMeanAdc + buf[bi++];
      if (tone) v += (double)mus[i] - 2048.0;
      adc[i] = (uint16_t)(v < 0 ? 0 : v > 4095 ? 4095 : std::lround(v));
    }
    return e.pushAdc(adc, ADC_BLOCK) > 0;
  }
  int visible() const {
    int n = 0;
    for (int b = 0; b < NUM_BANDS; ++b) n += (e.features().bar[b] >> 8) > 8;
    return n;
  }
};

// t0..t1 saniye aralığında kare başına ortalama görünür bar. gain(t) = dB (kalibrasyon 15. saniyede).
double scenario(const std::function<double(double)>& gain, double t0, double t1, double total, bool tone = false,
                int toneGainDb = -22, double* barSum0 = nullptr, double* barSum1 = nullptr) {
  static Run r;
  r = Run();
  r.tone = tone;
  if (tone) r.sy.reset(SC_SINE1K, 7, (int8_t)toneGainDb);
  double sum = 0, bs0 = 0, bs1 = 0;
  long n = 0, n0 = 0, n1 = 0;
  for (long k = 0; (double)k < total * FRAME_HZ;) {
    const double t = (double)k / FRAME_HZ;
    const bool toneNow = tone && t >= 20.0;  // ton kalibrasyondan sonra girer
    r.tone = toneNow;
    if (!r.step(gain(t))) continue;
    ++k;
    if (k == 15 * FRAME_HZ) r.e.analyzer().calibrateNoise(2 * FRAME_HZ);
    if (t >= t0 && t < t1) { sum += r.visible(); ++n; }
    if (toneNow) {
      double s = 0;
      for (int b = 0; b < NUM_BANDS; ++b) s += r.e.features().bar[b] / 65535.0;
      if (t >= 40.0 && t < 100.0) { bs0 += s; ++n0; }
      if (t >= total - 60.0) { bs1 += s; ++n1; }
    }
  }
  if (barSum0) *barSum0 = n0 ? bs0 / n0 : 0;
  if (barSum1) *barSum1 = n1 ? bs1 / n1 : 0;
  return n ? sum / n : 0;
}

// --- cal-drift v2: müzik senaryoları (kabul ölçütü: kalıcı gürültü payı müziği SİLMEMELİ) ---
// Düzenek: kartın PSD gürültüsü her zaman çalar, 15. sn'de kalibrasyon (sessizlikte),
// 20. sn'den itibaren Synth senaryosu `g` dB (Synth 0 dB'ye göre) ile eklenir. Ölçüt: müzik başladıktan sonra dakika dakika
// kare başına Σ bar/65535. Beklenen değerler kapısız pay mekanizması öncesi kodla ölçülmüş tabanlardır; v2 ≥ %90'ını vermeli.
struct Music {
  Scenario sc;
  double g0, g1, rampS;    // müzik kazancı: g0'dan g1'e rampS saniyede doğrusal (rampS = 0: sabit g0)
  int onMs, offMs;         // kapılı müzik (onMs ms açık, offMs ms kapalı); onMs = 0: sürekli
  double dropAt, dropDb;   // t >= dropAt iken ek kazanç (müzik → daha sessiz müzik)
};

void musicMinutes(const Music& m, int mins, double* out) {
  static Engine e;
  static noisepsd::NoiseAdc na;
  static Synth sy;
  na = noisepsd::NoiseAdc();
  e.params.defaults();
  e.reset(1);
  sy.reset(m.sc, 7, 0);
  uint16_t adc[ADC_BLOCK], mus[ADC_BLOCK];
  double add[ADC_BLOCK];
  long smp = 0;
  double acc = 0;
  long cnt = 0;
  int minute = 0;
  for (long blk = 0; minute < mins; ++blk) {
    const double t = (double)blk * ADC_BLOCK / FS_ADC;
    sy.render(mus, ADC_BLOCK);
    double g = t < 20.0 ? -200.0 : m.g0 + (m.g1 - m.g0) * std::fmin(std::fmax((t - 20.0) / (m.rampS > 0 ? m.rampS : 1.0), 0.0), 1.0);
    if (t >= m.dropAt) g += m.dropDb;
    const double scale = std::pow(10.0, g / 20.0);
    for (int i = 0; i < ADC_BLOCK; ++i, ++smp) {
      bool on = true;
      if (m.onMs > 0) on = (smp / (FS_ADC / 1000)) % (m.onMs + m.offMs) < m.onMs;
      add[i] = on ? ((double)mus[i] - 2048.0) * scale : 0.0;
    }
    na.block(adc, 0.0, add);
    if (e.pushAdc(adc, ADC_BLOCK) == 0) continue;
    const long fr = e.features().frame;
    if (fr == 15 * FRAME_HZ) e.analyzer().calibrateNoise(2 * FRAME_HZ);
    if (fr < 20 * FRAME_HZ) continue;
    double sb = 0;
    for (int b = 0; b < NUM_BANDS; ++b) sb += e.features().bar[b] / 65535.0;
    acc += sb;
    if (++cnt == 60 * FRAME_HZ) { out[minute++] = acc / cnt; cnt = 0; acc = 0; }
  }
}

}  // namespace

// Durağan gerçek-şekilli gürültüde 10 dk sessizlik: kalibrasyondan sonra bar sayısı zamanla artmamalı.
TEST(long_silence_stationary_no_drift) {
  auto g = [](double) { return 0.0; };
  const double early = scenario(g, 25, 145, 400);
  const double late = scenario(g, 280, 400, 400);
  METRIC("vis_early", early);
  METRIC("vis_late", late);
  CHECK(early < 2.0);
  CHECK(late < 2.0);
  CHECK(late <= early + 0.5);
}

// Kalıcı +6 dB basamak: eski kodda 400 sn sonra bile ~12 bar yanıyordu (kilitlenme); şimdi toparlanmalı.
TEST(noise_step_up_recovers) {
  auto g = [](double t) { return t >= 120.0 ? 6.0 : 0.0; };
  const double after = scenario(g, 200, 320, 320);
  METRIC("vis_after_step", after);
  CHECK(after < 3.0);
}

// Yavaş artış (+8 dB / 5 dk): bar sayısı sınırlı kalmalı (eski kodda ~10 bar).
TEST(noise_ramp_up_tracks) {
  auto g = [](double t) { return 8.0 * std::fmin(std::fmax((t - 60.0) / 300.0, 0.0), 1.0); };
  const double after = scenario(g, 420, 540, 540);
  METRIC("vis_after_ramp", after);
  CHECK(after < 3.0);
}

// Gürültü düşüp geri gelince (−6 dB sonra 0) barlar yanmamalı/toparlanmalı.
TEST(noise_dip_and_return) {
  auto g = [](double t) { return (t >= 100.0 && t < 220.0) ? -6.0 : 0.0; };
  const double during = scenario(g, 140, 220, 400);
  const double back = scenario(g, 300, 400, 400);
  METRIC("vis_during_dip", during);
  METRIC("vis_after_return", back);
  CHECK(during < 1.0);
  CHECK(back < 3.0);
}

// Kalıcı ton (tabanın az üstünde) "gürültü" diye öğrenilip silinmemeli: 4 dk sonra bar toplamı ilk dakikanın ≥ %80'i.
TEST(sustained_tone_not_learned_as_noise) {
  auto g = [](double) { return 0.0; };
  double s0 = 0, s1 = 0;
  scenario(g, 0, 1, 300, true, -22, &s0, &s1);
  METRIC("tone_bar_first_min", s0);
  METRIC("tone_bar_last_min", s1);
  CHECK(s0 > 0.3);
  CHECK(s1 >= 0.8 * s0);
}

// ---- cal-drift v2 müzik testleri ----
// Ölçüt: müzik başladıktan sonraki her dakikada bar toplamı, EĞİTİM tabanının (pay mekanizması olmayan eski kod,
// bu düzenekte ölçüldü) ≥ %90'ı olmalı. Kapısız pay mekanizmalı ilk sürüm bu testlerde FAIL eder (geniş bantlı müzik 1–2 dk'da
// gürültü sanılıp siliniyordu).
#define CHECK_GE_OLD90(measured, old) CHECK((measured) >= 0.9 * (old))

static void pink_case(const char* name, double g, double old0, double old3) {
  double m[4];
  musicMinutes({SC_PINK, g, g, 0, 0, 0, 1e9, 0}, 4, m);
  METRIC(name, m[0]);
  METRIC(name, m[3]);
  CHECK_GE_OLD90(m[0], old0);
  CHECK_GE_OLD90(m[3], old3);     // 4. dakikada hâlâ görünüyor (silinmedi)
  CHECK(m[3] >= 0.9 * m[0]);
}
// Geniş bantlı sürekli müzik (pembe gürültü, +2/+8/+14 dB): ilk dakika ile son dakika arasında düşüş olmamalı.
TEST(music_pink_plus2_not_erased) { pink_case("pink_plus2", 2, 15.85, 15.77); }
TEST(music_pink_plus8_not_erased) { pink_case("pink_plus8", 8, 24.21, 24.06); }
TEST(music_pink_plus14_not_erased) { pink_case("pink_plus14", 14, 26.26, 26.08); }

// Crescendo: müzik 5 dk'da 0 → +14 dB yükselir; gürültü payı büyüyüp yükselen müziği bastırmamalı.
TEST(music_crescendo_not_erased) {
  static const double old[5] = {7.68, 19.76, 23.25, 25.04, 25.84};
  double m[5];
  musicMinutes({SC_PINK, 0, 14, 300, 0, 0, 1e9, 0}, 5, m);
  for (int i = 0; i < 5; ++i) { METRIC("crescendo_min", m[i]); CHECK_GE_OLD90(m[i], old[i]); }
  CHECK(m[4] >= m[2]);
}

// Boşluklu müzik: her boşlukta gürültü tabanına iner (200 ms / 50 ms); kapı boşlukları "gürültü artışı" saymamalı.
// v1 bu ikisinde de %85'e düşer (18,4 → 15,7 ve 23,1 → 0,07).
TEST(music_gapped_300_200_not_erased) {
  double m[4];
  musicMinutes({SC_PINK, 8, 8, 0, 300, 200, 1e9, 0}, 4, m);
  METRIC("gapped_300_200_first", m[0]);
  METRIC("gapped_300_200_last", m[3]);
  CHECK_GE_OLD90(m[0], 18.54);
  CHECK_GE_OLD90(m[3], 18.40);
}
TEST(music_gapped_450_50_not_erased) {
  double m[4];
  musicMinutes({SC_PINK, 8, 8, 0, 450, 50, 1e9, 0}, 4, m);
  METRIC("gapped_450_50_first", m[0]);
  METRIC("gapped_450_50_last", m[3]);
  CHECK_GE_OLD90(m[0], 23.28);
  CHECK_GE_OLD90(m[3], 23.09);
}

// Bastırma denetimi, toparlanma ölçütü DEĞİL: müzik → 15 dB sessiz müzik (200. sn) bu düzenekte gürültü tabanının altında kalır
// (eski kodda da ≈ 0,2), ama öncesinde müzik silinmemiş olmalı. `m[7] <= 2.0` sıfır çıktıyı da kabul eder; toparlanma, kontrole göre
// saniye çözünürlüklü, tests/test_quiet_recovery.cpp'de sınanır.
TEST(music_loud_then_quiet_same_as_old) {
  double m[8];
  musicMinutes({SC_PINK, 14, 14, 0, 0, 0, 200.0, -15}, 8, m);
  METRIC("loud_then_quiet_min3", m[2]);
  METRIC("loud_then_quiet_min8", m[7]);
  CHECK_GE_OLD90(m[0], 26.2);
  CHECK_GE_OLD90(m[2], 26.01);
  CHECK(m[7] <= 2.0);
}

// Müzik benzeri karışım (SC_MIX) ve sweep: eski kodla aynı kalmalı (kapı kapalı).
TEST(music_mix_and_sweep_unchanged) {
  double mix[3], sw[3];
  musicMinutes({SC_MIX, 0, 0, 0, 0, 0, 1e9, 0}, 3, mix);
  musicMinutes({SC_SWEEP, -6, -6, 0, 0, 0, 1e9, 0}, 3, sw);
  METRIC("mix_last", mix[2]);
  METRIC("sweep_last", sw[2]);
  CHECK_GE_OLD90(mix[2], 0.979);
  CHECK_GE_OLD90(sw[2], 2.127);
}

FV_TEST_MAIN()

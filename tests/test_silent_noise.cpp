// test_silent_noise: kartın kalibre edilmiş ekran gürültüsünde `silent` ve LED davranışı.
// Gürültü: tests/data/mic_noise_psd.h PSD'sinden (Gauss + rasgele faz, sqrt-Hann OLA);
// PSD'nin doğrudan ölçeği kartın ölçülen ekran-açık düzeyinin ≈ 8 dB altında olduğundan SCREEN_ON_DB eklenir (rms ≈ 650 LSB).
// Eski kural: tek bir bant bar eşiğini iki kare aşsa `silent` düşüyor → sessizlikte idle hiç devreye girmiyor,
// BASS/FIRE gürültüyü çiziyordu. Yeni kural: ≥ 2 bant ya da bas kanalı gürültünün 8 dB üstünde → etkin.
#include <complex>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"
#include "data/mic_noise_psd.h"

using namespace fv;

namespace {

constexpr int NB = 1024;
constexpr double SCREEN_ON_DB = 8.3;    // ekran açık (kart ölçümü): rms ≈ 650 LSB
constexpr double SCREEN_OFF_DB = -14.0;  // ekran kapalı (disp 0): rms ≈ 50 LSB (0,2 × 250 LSB)

struct Rng {
  uint32_t s;
  double u() { return ((xorshift32(s) >> 8) + 0.5) / 16777216.0; }
  double g() { return std::sqrt(-2.0 * std::log(u())) * std::cos(2.0 * M_PI * u()); }
};

void ifft(std::vector<std::complex<double>>& a) {  // yerinde, e^{+j}, ölçeksiz
  const int n = (int)a.size();
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    const double ang = 2.0 * M_PI / len;
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (int i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (int k = 0; k < len / 2; ++k) {
        auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

struct NoiseGen {
  Rng rng;
  double tail[NB / 2] = {};
  std::vector<std::complex<double>> z{std::vector<std::complex<double>>(NB)};
  explicit NoiseGen(uint32_t seed) : rng{seed} {}
  void block(double gainDb, double* out512) {
    const double gl = std::pow(10.0, gainDb / 20.0);
    for (int k = 0; k <= NB / 2; ++k) {
      const double a = k == 0 ? 0 : std::sqrt(NB * std::pow(10.0, kMicNoisePsd[k] / 10.0) / 2.0) * gl;
      std::complex<double> v(rng.g() * a, rng.g() * a);
      if (k == NB / 2) v = v.real() * std::sqrt(2.0);
      z[k] = v;
      if (k > 0 && k < NB / 2) z[NB - k] = std::conj(v);
    }
    ifft(z);
    for (int i = 0; i < NB / 2; ++i) {
      const double w0 = std::sin(M_PI * (i + 0.5) / NB), w1 = std::sin(M_PI * (i + NB / 2 + 0.5) / NB);
      out512[i] = tail[i] + z[i].real() / NB * w0;
      tail[i] = z[i + NB / 2].real() / NB * w1;
    }
  }
};

struct Result {
  double silentFrac = 0;   // ölçüm penceresinde silent=1 kare oranı
  double litMean = 0;      // kare başına tel parlaklığı (luma) ≥ 8 olan LED sayısı (ortalama)
  double burstPerMin = 0;  // ≥ 10 LED'in bir anda yanması ("nabız") olayı / dk
  double sumY = 0;         // kare başına toplam luma (ortalama)
};

// 20 s gürültü (kalibrasyon 10. s'de), sonra [sig, sigDb] ile 60 s ölçüm. noiseDb: ekran açık/kapalı.
// multi = true: Synth yerine 6 sabit ton (300, 700, 1500, 3000, 6000, 9000 Hz; her biri tepe 250 LSB): ekran açıkken gürültü ADC payını
// yediği için ancak bu kadar yüksek, çok bantlı bir sinyal görünür barlar üretir (gerçek müzik rms ≈ -9 dBFS ≈ 700 LSB).
Result run(int ledMode, double noiseDb, Scenario sig, int sigDb, bool withSig, uint32_t seed, bool multi = false, double measSec = 60.0) {
  static Engine e;
  static Synth sy;
  e.params.defaults();
  e.params.set(P_LED_MODE, ledMode);
  e.reset(1);
  NoiseGen ng(seed);
  if (withSig && !multi) sy.reset(sig, 5, (int8_t)sigDb);
  static const double kTone[6] = {300, 700, 1500, 3000, 6000, 9000};
  long sampleNo = 0;
  double buf[512];
  int bi = 512;
  uint16_t adc[ADC_BLOCK], mus[ADC_BLOCK];
  Result r;
  long k = 0, meas = 0, silent = 0, bursts = 0;
  double lit = 0, sumY = 0;
  bool inBurst = false;
  const long total = (long)((20.0 + measSec) * FRAME_HZ);
  while (k < total) {
    const bool musicNow = withSig && k >= 20 * FRAME_HZ;
    if (musicNow && !multi) sy.render(mus, ADC_BLOCK);
    for (int i = 0; i < ADC_BLOCK; ++i) {
      if (bi == 512) { ng.block(noiseDb, buf); bi = 0; }
      double v = kMicNoiseMeanAdc + buf[bi++];
      if (musicNow && !multi) v += (double)mus[i] - 2048.0;
      if (musicNow && multi) { for (double f : kTone) v += 250.0 * std::sin(2.0 * M_PI * f * (double)sampleNo / FS_ADC); ++sampleNo; }
      adc[i] = (uint16_t)(v < 0 ? 0 : v > 4095 ? 4095 : std::lround(v));
    }
    if (e.pushAdc(adc, ADC_BLOCK) == 0) continue;
    ++k;
    if (k == 10 * FRAME_HZ) e.analyzer().calibrateNoise(2 * FRAME_HZ);
    if (k <= 20 * FRAME_HZ) continue;
    ++meas;
    silent += e.features().silent;
    const Rgb* o = e.leds().out();
    int nlit = 0;
    double y = 0;
    for (int i = 0, n = e.leds().count(); i < n; ++i) {
      const int Y = (54 * o[i].r + 183 * o[i].g + 19 * o[i].b) >> 8;
      nlit += Y >= 8;
      y += Y;
    }
    lit += nlit;
    sumY += y;
    const bool b = nlit >= 10;
    if (b && !inBurst) ++bursts;
    inBurst = b;
  }
  r.silentFrac = (double)silent / meas;
  r.litMean = lit / meas;
  r.burstPerMin = bursts / (meas / (60.0 * FRAME_HZ));
  r.sumY = sumY / meas;
  return r;
}

}  // namespace

// Kök neden 1: kalibre ekran gürültüsünde silent kalkıyordu (kare başına tek bant yetiyordu). 4 s bekleme + sonrası ≥ %70.
TEST(calibrated_display_noise_is_silent) {
  double worst = 1;
  for (uint32_t seed = 1; seed <= 3; ++seed) {
    const Result r = run(LM_AURORA, SCREEN_ON_DB, SC_SILENCE, 0, false, seed);
    worst = std::fmin(worst, r.silentFrac);
  }
  METRIC("silent_frac_worst", worst);
  CHECK(worst >= 0.70);
}

// BASS: sessizlikte nabız yok (eski: ≈ 17 olay/dk, şeridin ortasından yanlara).
TEST(bass_mode_quiet_in_silence) {
  double worst = 0;
  for (uint32_t seed = 1; seed <= 3; ++seed) worst = std::fmax(worst, run(LM_BASS, SCREEN_ON_DB, SC_SILENCE, 0, false, seed).burstPerMin);
  METRIC("bass_bursts_per_min_worst", worst);
  CHECK(worst <= 4.0);
}

// FIRE: sessizlikte iki uçta sürekli alev yok (eski: ortalama ≈ 23 LED yanık).
TEST(fire_mode_dark_in_silence) {
  double worst = 0;
  for (uint32_t seed = 1; seed <= 3; ++seed) worst = std::fmax(worst, run(LM_FIRE, SCREEN_ON_DB, SC_SILENCE, 0, false, seed).litMean);
  METRIC("fire_lit_leds_worst", worst);
  CHECK(worst <= 5.0);
}

// Yalnız bas içeren müzik (kick) görünür bantları aşmasa bile silent=0 kalmalı: bas kanalı gürültünün 8 dB üstünde etkin sayılır.
TEST(bass_only_music_is_not_silent) {
  const Result r = run(LM_BASS, SCREEN_OFF_DB, SC_KICK120, -12, true, 1);
  METRIC("kick_silent_frac", r.silentFrac);
  CHECK(r.silentFrac <= 0.10);
}

// Geniş bantlı müzik: ekran kapalı (kick + bas + akor + hi-hat, -12 dB) ve ekran açık (6 tonlu yüksek sinyal) silent=0 kalmalı,
// LED tepkisi sürmeli. (Ekran açıkken SC_MIX/SC_PINK tek başına gürültü seviyesinde kalır -- ortalama 1,5 bar -- ve sessizlikten
// ayırt edilemez; bu bir donanım sınırıdır, kural değişikliğinin sonucu değil: eski kodda da yalnız gürültü çubukları yanıyordu.)
TEST(broadband_music_is_not_silent) {
  const Result off = run(LM_AURORA, SCREEN_OFF_DB, SC_MIX, -12, true, 1);
  const Result on = run(LM_AURORA, SCREEN_ON_DB, SC_SILENCE, 0, true, 1, true);
  METRIC("mix_off_silent_frac", off.silentFrac);
  METRIC("multi_on_silent_frac", on.silentFrac);
  METRIC("mix_off_lit", off.litMean);
  METRIC("multi_on_lit", on.litMean);
  CHECK(off.silentFrac <= 0.10);
  CHECK(on.silentFrac <= 0.10);
  CHECK(off.litMean > 20.0);
  CHECK(on.litMean > 20.0);
}

// Tek ton (−30 dBFS, ekran kapalı): Hann sızıntısı ≥ 2 banda yayılır, silent=0 kalır.
TEST(single_tone_is_not_silent) {
  const Result r = run(LM_AURORA, SCREEN_OFF_DB, SC_SINE1K, -30, true, 1);
  METRIC("tone_silent_frac", r.silentFrac);
  CHECK(r.silentFrac <= 0.10);
}

FV_TEST_MAIN()

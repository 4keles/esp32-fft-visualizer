// test_decim: DC HPF + 47 tap halfband (48k → 24k): geçiş bandı dalgalanması, durdurma bandı,
// DC giderme, kırpma sayacı ve parça boyutundan bağımsızlık.
#include <vector>
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;

// x[n] ≈ a·cos(ωn) + b·sin(ωn) en küçük kareler → genlik.
static double fit_amp(const std::vector<q15>& y, size_t from, double w) {
  double cc = 0, ss = 0, cs = 0, yc = 0, ys = 0;
  for (size_t n = from; n < y.size(); ++n) {
    double c = std::cos(w * n), s = std::sin(w * n);
    cc += c * c; ss += s * s; cs += c * s; yc += y[n] * c; ys += y[n] * s;
  }
  double det = cc * ss - cs * cs;
  double a = (yc * ss - ys * cs) / det, b = (ys * cc - yc * cs) / det;
  return std::sqrt(a * a + b * b);
}

static std::vector<q15> run_tone(double f, double amp, int nOut) {
  static Decimator d;
  d.reset();
  std::vector<uint16_t> raw(2 * nOut);
  for (size_t i = 0; i < raw.size(); ++i)
    raw[i] = (uint16_t)std::lround(2048.0 + amp * std::sin(2.0 * M_PI * f * i / FS_ADC + 0.4));
  std::vector<q15> y(nOut);
  CHECK_EQ(d.process(raw.data(), (int)raw.size(), y.data()), nOut);
  return y;
}

// Tablo katsayılarının (kuantize) kuramsal yanıtı.
static double table_resp_db(double f) {
  double w = 2.0 * M_PI * f / FS_ADC, H = tab::halfband[12];
  for (int j = 0; j < 12; ++j) H += 2.0 * tab::halfband[j] * std::cos(w * (2 * j + 1));
  return 20.0 * std::log10(std::fabs(H) / 32768.0 + 1e-30);
}

TEST(halfband_table_response) {
  long sum = tab::halfband[12];
  for (int j = 0; j < 12; ++j) sum += 2 * tab::halfband[j];
  CHECK_EQ(sum, 32768L);  // DC kazancı tam 1
  double pmax = -1e9, pmin = 1e9, stop = -1e9;
  for (double f = 0; f <= 10000; f += 10) { double g = table_resp_db(f); pmax = std::fmax(pmax, g); pmin = std::fmin(pmin, g); }
  for (double f = 14000; f <= 24000; f += 10) stop = std::fmax(stop, table_resp_db(f));
  METRIC("pass_ripple_db", pmax - pmin);
  METRIC("stopband_db", -stop);
  CHECK(pmax - pmin <= 0.1);
  CHECK(-stop >= 55.0);
}

TEST(decim_passband) {
  const double A = 1800.0;  // ADC LSB
  double gmax = -1e9, gmin = 1e9;
  for (double f = 100; f <= 10000; f += 330) {
    std::vector<q15> y = run_tone(f, A, 6000);
    double g = 20.0 * std::log10(fit_amp(y, 1000, 2.0 * M_PI * f / FS) / (A * 16.0));
    gmax = std::fmax(gmax, g); gmin = std::fmin(gmin, g);
  }
  METRIC("gain_max_db", gmax);
  METRIC("gain_min_db", gmin);
  CHECK(gmax <= 0.1 && gmin >= -0.1);
}

TEST(decim_stopband) {
  const double A = 2000.0;
  double worst = -1e9;
  for (double f = 14000; f <= 23900; f += 370) {
    std::vector<q15> y = run_tone(f, A, 8000);
    double fa = FS - (f - FS);  // 24 kHz örneklemede katlanan frekans (= 48k - f)
    double wa = 2.0 * M_PI * fa / FS;
    double g = 20.0 * std::log10(fit_amp(y, 1000, wa) / (A * 16.0) + 1e-12);
    worst = std::fmax(worst, g);
  }
  METRIC("stopband_db", -worst);
  CHECK(-worst >= 55.0);
}

TEST(decim_dc_removal) {
  static Decimator d;
  d.reset();
  std::vector<uint16_t> raw(48000, 3000);
  std::vector<q15> y(24000);
  d.process(raw.data(), (int)raw.size(), y.data());
  double mx = 0;
  for (size_t i = 100; i < y.size(); ++i) mx = std::fmax(mx, std::fabs((double)y[i]));
  CHECK(mx <= 2.0);
  CHECK(std::abs((int)d.dcRaw() - 3000) <= 1);
  // Ani DC değişimi: zaman sabiti ~43 ms; 0.5 s sonra kalıntı < %1.
  std::vector<uint16_t> raw2(48000, 2000);
  d.process(raw2.data(), (int)raw2.size(), y.data());
  double resid = std::fabs((double)y[12000]) / (1000.0 * 16.0);
  METRIC("dc_step_resid_500ms", resid);
  CHECK(resid < 0.01);
  CHECK(std::abs((int)d.dcRaw() - 2000) <= 2);
  CHECK_EQ(d.clipCount(), 0u);
}

TEST(decim_clip_count) {
  static Decimator d;
  d.reset();
  uint16_t raw[8] = {2048, 0, 4095, 2048, 4095, 1, 4094, 0};
  q15 y[4];
  CHECK_EQ(d.process(raw, 8, y), 4);
  CHECK_EQ(d.clipCount(), 4u);
  d.clearClip();
  CHECK_EQ(d.clipCount(), 0u);
}

TEST(decim_chunk_invariance) {
  static Decimator a, b;
  a.reset(); b.reset();
  std::vector<uint16_t> raw(9600);
  uint32_t s = 5;
  for (size_t i = 0; i < raw.size(); ++i) raw[i] = (uint16_t)(xorshift32(s) & 4095);
  std::vector<q15> ya(4800), yb(4800);
  a.process(raw.data(), (int)raw.size(), ya.data());
  int o = 0;
  for (size_t i = 0; i < raw.size();) {
    int n = 2 * (1 + (int)(xorshift32(s) % 400));
    if (i + n > raw.size()) n = (int)(raw.size() - i);
    o += b.process(raw.data() + i, n, yb.data() + o);
    i += n;
  }
  CHECK_EQ(o, 4800);
  CHECK(ya == yb);
}

FV_TEST_MAIN()

// test_fft: Q15 cfft/rfft'nin double DFT'ye karşı SNR'ı ve tam ölçek girişte taşmazlık.
#include <complex>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;
using cd = std::complex<double>;

static std::vector<cd> dft(const std::vector<cd>& x) {
  const size_t n = x.size();
  std::vector<cd> X(n);
  for (size_t k = 0; k < n; ++k) {
    cd s = 0;
    for (size_t t = 0; t < n; ++t) s += x[t] * std::polar(1.0, -2.0 * M_PI * (double)((k * t) % n) / (double)n);
    X[k] = s;
  }
  return X;
}

// SNR (dB): referans = DFT(x)·2^-sh.
static double snr_db(const std::vector<cd>& ref, const cq15* out, int nb, int sh, bool realPacked) {
  double sig = 0, err = 0, sc = std::ldexp(1.0, -sh);
  for (int k = 0; k < nb; ++k) {
    cd r = ref[k] * sc;
    cd o(out[k].re, out[k].im);
    if (realPacked && k == 0) o = cd(out[0].re, 0);
    sig += std::norm(r);
    err += std::norm(o - r);
  }
  if (realPacked) {  // Nyquist x[0].im
    cd r = ref[nb] * sc;
    sig += std::norm(r);
    err += std::norm(cd(out[0].im, 0) - r);
  }
  return 10.0 * std::log10(sig / (err + 1e-30));
}

enum Kind { K_RANDOM, K_SINE, K_SQUARE, K_IMPULSE0, K_IMPULSE, K_MAXPOS, K_ALT, K_RANDPM, K_COUNT };
static const char* kname[K_COUNT] = {"random", "sine", "square", "impulse0", "impulse", "maxpos", "alt", "randpm"};

static int16_t gen(Kind k, int t, int n, uint32_t& s) {
  const int A = 1 << 14;
  switch (k) {
    case K_RANDOM: return (int16_t)((int32_t)(xorshift32(s) % (2 * A + 1)) - A);
    case K_SINE: return (int16_t)std::lround(A * std::sin(2.0 * M_PI * 37.3 * t / n + 0.3));
    case K_SQUARE: return (int16_t)(((t * 13 / n) & 1) ? A : -A);
    case K_IMPULSE0: return (int16_t)(t == 0 ? A : 0);
    case K_IMPULSE: return (int16_t)(t == n / 3 + 1 ? A : 0);
    case K_MAXPOS: return (int16_t)A;
    case K_ALT: return (int16_t)((t & 1) ? -A : A);
    case K_RANDPM: return (int16_t)((xorshift32(s) & 1) ? A : -A);
    default: return 0;
  }
}

TEST(cfft_snr) {
  static cq15 x[512];
  for (int log2n = 4; log2n <= 9; ++log2n) {
    int n = 1 << log2n;
    for (int k = 0; k < K_COUNT; ++k) {
      uint32_t s = 777 + k;
      std::vector<cd> xin(n);
      for (int t = 0; t < n; ++t) {
        x[t].re = gen((Kind)k, t, n, s);
        x[t].im = gen((Kind)k, (t * 7 + 3) % n, n, s);
        xin[t] = cd(x[t].re, x[t].im);
      }
      int sh = cfft_q15(x, log2n);
      CHECK(sh >= 0 && sh <= log2n);
      double snr = snr_db(dft(xin), x, n, sh, false);
      if (log2n == 9) {
        char key[64];
        std::snprintf(key, sizeof key, "snr_db_%s", kname[k]);
        METRIC(key, snr);
      }
      if (!(snr >= 60.0)) {
        std::printf("    cfft n=%d %s snr=%.2f sh=%d\n", n, kname[k], snr, sh);
        CHECK(snr >= 60.0);
      }
    }
  }
}

TEST(rfft_snr) {
  static cq15 x[512];
  const int log2n = 10, n = 1024;
  for (int k = 0; k < K_COUNT; ++k) {
    uint32_t s = 99 + k;
    std::vector<cd> xin(n);
    for (int t = 0; t < n; ++t) {
      int16_t v = gen((Kind)k, t, n, s);
      xin[t] = cd(v, 0);
      if (t & 1) x[t >> 1].im = v; else x[t >> 1].re = v;
    }
    int sh = rfft_q15(x, log2n);
    CHECK(sh >= 0 && sh <= log2n);
    double snr = snr_db(dft(xin), x, n / 2, sh, true);
    char key[64];
    std::snprintf(key, sizeof key, "snr_db_%s", kname[k]);
    METRIC(key, snr);
    if (!(snr >= 60.0)) {
      std::printf("    rfft %s snr=%.2f sh=%d\n", kname[k], snr, sh);
      CHECK(snr >= 60.0);
    }
  }
}

// Tam ölçek sinüs taraması: 64 frekans/faz; en kötü ve ortalama SNR.
TEST(rfft_sine_sweep) {
  static cq15 x[512];
  const int n = 1024;
  uint32_t s = 31337;
  double worst = 1e9, sum = 0;
  int cnt = 0;
  for (int it = 0; it < 64; ++it) {
    double f = 1.0 + (xorshift32(s) % 500000) / 1000.0;
    double ph = (xorshift32(s) % 6283) / 1000.0;
    double amp = 16384.0 * (it % 2 ? 1.0 : 0.5);
    std::vector<cd> xin(n);
    for (int t = 0; t < n; ++t) {
      int16_t v = (int16_t)std::lround(std::fmin(16384.0, std::fmax(-16384.0, amp * std::sin(2.0 * M_PI * f * t / n + ph))));
      xin[t] = cd(v, 0);
      if (t & 1) x[t >> 1].im = v; else x[t >> 1].re = v;
    }
    int sh = rfft_q15(x, 10);
    double snr = snr_db(dft(xin), x, n / 2, sh, true);
    worst = std::fmin(worst, snr);
    sum += snr;
    ++cnt;
  }
  METRIC("min_snr_db", worst);
  METRIC("mean_snr_db", sum / cnt);
  CHECK(worst >= 60.0);
}

// Fuzz: tam ölçekli (|x| ≤ 2^14) rastgele/uç girişler. Taşma (int16 sarması) olsaydı ilgili
// kutuda hata ~2^16 olur ve SNR çökerdi; her denemede tüm kutuların hatası ayrıca sınırlanır.
TEST(fft_fuzz_no_overflow) {
  static cq15 x[512];
  uint32_t s = 4242;
  double worst = 1e9, maxErr = 0;
  int maxAbs = 0;
  for (int it = 0; it < 300; ++it) {
    const int n = 1024;
    std::vector<cd> xin(n);
    int mode = it % 4;
    for (int t = 0; t < n; ++t) {
      int16_t v;
      uint32_t r = xorshift32(s);
      if (mode == 0) v = (int16_t)((r & 1) ? 16384 : -16384);
      else if (mode == 1) v = (int16_t)((int32_t)(r % 32769) - 16384);
      else if (mode == 2) v = (int16_t)(((t / (1 + it % 17)) & 1) ? 16384 : -16384);
      else v = (int16_t)((r % 5 == 0) ? ((r & 2) ? 16384 : -16384) : 0);
      xin[t] = cd(v, 0);
      if (t & 1) x[t >> 1].im = v; else x[t >> 1].re = v;
    }
    int sh = rfft_q15(x, 10);
    for (int k = 0; k < 512; ++k) maxAbs = vmax(maxAbs, vmax(std::abs((int)x[k].re), std::abs((int)x[k].im)));
    if (it < 40) {
      std::vector<cd> ref = dft(xin);
      worst = std::fmin(worst, snr_db(ref, x, 512, sh, true));
      double sc = std::ldexp(1.0, -sh), emax = 0;
      for (int k = 1; k < 512; ++k) emax = std::fmax(emax, std::abs(cd(x[k].re, x[k].im) - ref[k] * sc));
      maxErr = std::fmax(maxErr, emax);
    }
  }
  METRIC("min_snr_db", worst);
  METRIC("max_abs_out", maxAbs);
  METRIC("max_bin_err_lsb", maxErr);
  CHECK(worst >= 58.0);
  CHECK(maxErr < 16.0);
}

TEST(power_spectrum_basic) {
  cq15 X[4] = {{3, 4}, {-23170, 23170}, {0, 0}, {-32768, 0}};
  uint32_t P[4];
  power_spectrum(X, P, 4);
  CHECK_EQ(P[0], 25u);
  CHECK_EQ(P[1], 2u * 23170u * 23170u);
  CHECK_EQ(P[2], 0u);
  CHECK_EQ(P[3], 1073741824u);
}

FV_TEST_MAIN()

// gen_tables.cpp — DSP tablolarını üretir (host aracı, double kullanır).
// Kullanım: gen_tables > core/src/fv_tables_gen.cpp
// Çıktı deterministiktir; tests/test_tables_drift commit'teki dosyayla bayt bayt karşılaştırır.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

#include "fv_config.h"

using namespace fv;

static const double PI = 3.14159265358979323846;

static long lround_half_away(double x) { return (long)(x < 0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5)); }

static void emit(const char* decl, const std::vector<long>& v, int perLine = 12) {
  std::printf("%s = {\n", decl);
  for (size_t i = 0; i < v.size(); ++i) {
    if (i % perLine == 0) std::printf("    ");
    std::printf("%ld,", v[i]);
    if (i % perLine == perLine - 1 || i + 1 == v.size()) std::printf("\n");
    else std::printf(" ");
  }
  std::printf("};\n\n");
}

// --- Kaiser halfband ---
static double bessel_i0(double x) {
  double s = 1.0, t = 1.0;
  for (int k = 1; k < 64; ++k) {
    t *= (x / (2.0 * k)) * (x / (2.0 * k));
    s += t;
    if (t < 1e-17 * s) break;
  }
  return s;
}

// 12 tek katsayı (Q15) + merkez 16384; DC kazancı tam 1 olacak şekilde düzeltilir.
static void halfband_q15(double beta, long c[12]) {
  const int H = HALFBAND_TAPS / 2;  // 23
  double h[12];
  for (int j = 0; j < 12; ++j) {
    int n = 2 * j + 1;
    double ideal = std::sin(PI * n / 2.0) / (PI * n);
    double r = (double)n / H;
    double w = bessel_i0(beta * std::sqrt(1.0 - r * r)) / bessel_i0(beta);
    h[j] = ideal * w;
  }
  long sum = 0;
  for (int j = 0; j < 12; ++j) { c[j] = lround_half_away(h[j] * 32768.0); sum += c[j]; }
  c[0] += 8192 - sum;  // 16384 + 2*Σc = 32768
}

// Kuantize filtrenin [f0,f1] Hz aralığındaki en büyük |H| (dB), fs=48k.
static double hb_resp_db(const long c[12], double f) {
  double w = 2.0 * PI * f / FS_ADC;
  double H = 16384.0;
  for (int j = 0; j < 12; ++j) H += 2.0 * c[j] * std::cos(w * (2 * j + 1));
  return 20.0 * std::log10(std::fabs(H) / 32768.0 + 1e-30);
}
static double hb_stop_db(const long c[12]) {
  double worst = -1e9;
  for (double f = 14000.0; f <= 24000.0; f += 5.0) worst = std::fmax(worst, hb_resp_db(c, f));
  return worst;
}

int main() {
  std::printf("// ÜRETİLMİŞ DOSYA — elle düzenlemeyin. Kaynak: tools/gen_tables.cpp\n");
  std::printf("// Yeniden üretmek: build/gen_tables > core/src/fv_tables_gen.cpp\n");
  std::printf("#include \"fv_tables.h\"\n\nnamespace fv {\nnamespace tab {\n\n");

  // Hann (periyodik), w[0..N/2]
  {
    std::vector<long> v;
    for (int n = 0; n <= FFT_N / 2; ++n) {
      double w = 0.5 - 0.5 * std::cos(2.0 * PI * n / FFT_N);
      long q = lround_half_away(w * 32768.0);
      v.push_back(q > 32767 ? 32767 : q);
    }
    emit("const int16_t hann1024[FFT_N / 2 + 1]", v);
  }
  // Çeyrek sinüs, 1024 adım / tur
  {
    std::vector<long> v;
    for (int i = 0; i <= FFT_N / 4; ++i) {
      long q = lround_half_away(std::sin(2.0 * PI * i / FFT_N) * 32768.0);
      v.push_back(q > 32767 ? 32767 : q);
    }
    emit("const int16_t sin_q15[FFT_N / 4 + 1]", v);
  }
  // 9 bit bit-reverse
  {
    std::vector<long> v;
    for (int i = 0; i < FFT_N / 2; ++i) {
      int r = 0;
      for (int b = 0; b < 9; ++b) if (i & (1 << b)) r |= 1 << (8 - b);
      v.push_back(r);
    }
    emit("const uint16_t bitrev512[FFT_N / 2]", v, 16);
  }
  // log2 kesir / exp2 kesir
  {
    std::vector<long> v, e;
    for (int i = 0; i < 256; ++i) {
      v.push_back(lround_half_away(256.0 * std::log2(1.0 + i / 256.0)));
      e.push_back(lround_half_away(65536.0 * (std::pow(2.0, i / 256.0) - 1.0)));
    }
    emit("const uint8_t log2frac[256]", v, 16);
    emit("const uint16_t exp2frac[256]", e);
  }
  // Halfband: β taraması, kuantize filtrenin durdurma bandını en iyi yapan β seçilir.
  {
    double bestBeta = 0, bestDb = 1e9;
    long best[12];
    for (int bi = 300; bi <= 900; ++bi) {
      double beta = bi / 100.0;
      long c[12];
      halfband_q15(beta, c);
      double s = hb_stop_db(c);
      if (s < bestDb - 1e-9) { bestDb = s; bestBeta = beta; for (int j = 0; j < 12; ++j) best[j] = c[j]; }
    }
    std::printf("// Halfband: 47 tap Kaiser, beta=%.2f, kuantize durdurma bandı (14-24 kHz) %.1f dB\n", bestBeta, bestDb);
    std::vector<long> v(best, best + 12);
    v.push_back(16384);
    emit("const int16_t halfband[(HALFBAND_TAPS + 1) / 4 + 1]", v, 13);
  }
  // Bant sınırları/tilt artık çalışma zamanında üretilir (fv_bands.cpp; fmin/fmax parametreleri).
  // Gürültü çıkarma ve log toplama LUT'ları (d = i*8 lvl).
  {
    std::vector<long> ns, la;
    for (int i = 0; i < 128; ++i) {
      double d = i * 8.0 / 256.0;  // log2 birimi
      double s = (i == 0) ? -4096.0 : 256.0 * std::log2(1.0 - std::pow(2.0, -d));
      if (s < -4096.0) s = -4096.0;
      ns.push_back(lround_half_away(s));
      la.push_back(lround_half_away(256.0 * std::log2(1.0 + std::pow(2.0, -d))));
    }
    emit("const int16_t noiseSub[128]", ns);
    emit("const int16_t logAdd[128]", la);
  }
  std::printf("}  // namespace tab\n}  // namespace fv\n");
  return 0;
}

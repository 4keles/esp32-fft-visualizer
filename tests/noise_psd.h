// noise_psd.h — kartın gerçek sessizlik ham ADC kaydının PSD'sinden (tests/data/mic_noise_psd.h) deterministik
// gürültü sentezi (sqrt-Hann OLA, Gauss + rasgele faz, 48 kHz, 12 bit). test_calibration_drift ve test_autocal ortak kullanır.
// Yalnız host testleri içindir (double, std::complex, vector kullanır); çekirdekte (core/) KULLANILMAZ.
#pragma once
#include <complex>
#include <vector>
#include <cmath>
#include "fv_math.h"
#include "fv_config.h"
#include "data/mic_noise_psd.h"

namespace noisepsd {

using fv::xorshift32;
constexpr int NB = 1024;  // sentez bloğu (48 kHz örnek), hop 512

struct Rng {
  uint32_t s;
  double u() { return ((xorshift32(s) >> 8) + 0.5) / 16777216.0; }
  double g() { return std::sqrt(-2.0 * std::log(u())) * std::cos(2.0 * M_PI * u()); }
};

inline void fft(std::vector<std::complex<double>>& a) {  // yerinde, ters (e^{+j}), ölçeksiz
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

// Zamanla değişen genlik (dB) ile PSD'den gürültü üretir; çıktı 12 bit ADC (0..4095).
struct NoiseGen {
  Rng rng{20261010u};
  double tail[NB / 2] = {};
  double amp[NB / 2 + 1];
  double shape[NB / 2 + 1];  // kutu başına genlik çarpanı (1 = PSD'nin kendisi); gürültü artışının spektral şekli için
  std::vector<std::complex<double>> z{std::vector<std::complex<double>>(NB)};
  NoiseGen() {
    for (int k = 0; k <= NB / 2; ++k) {
      amp[k] = k == 0 ? 0 : std::sqrt(NB * std::pow(10.0, kMicNoisePsd[k] / 10.0) / 2.0);
      shape[k] = 1.0;
    }
  }
  void block(double gainDb, double* out512) {  // 512 yeni örnek
    const double gl = std::pow(10.0, gainDb / 20.0);
    for (int k = 0; k <= NB / 2; ++k) {
      const double a = amp[k] * gl * shape[k];
      std::complex<double> v(rng.g() * a, rng.g() * a);
      if (k == 0) v = 0;
      if (k == NB / 2) v = v.real() * std::sqrt(2.0);
      z[k] = v;
      if (k > 0 && k < NB / 2) z[NB - k] = std::conj(v);
    }
    fft(z);  // ters (+j) dönüşüm, Hermit → gerçek; 1/N ölçeği
    for (int i = 0; i < NB / 2; ++i) {
      const double w0 = std::sin(M_PI * (i + 0.5) / NB), w1 = std::sin(M_PI * (i + NB / 2 + 0.5) / NB);
      out512[i] = tail[i] + z[i].real() / NB * w0;
      tail[i] = z[i + NB / 2].real() / NB * w1;
    }
  }
};

// 800 örneklik (1 hop) ADC bloğu üretir: kartın PSD gürültüsü (gainDb) + isteğe bağlı eklenen sinyal (ADC LSB, ortalamasız).
struct NoiseAdc {
  NoiseGen ng;
  Rng jr{987654321u};
  double jitterDb = 0;   // > 0: her 512 örneklik blokta rastgele ortak dB sapması N(0, jitterDb²) (gerçek karttaki hızlı geniş bant dalgalanması)
  double buf[512];
  int bi = 512;
  void block(uint16_t* adc, double gainDb, const double* add = nullptr) {
    for (int i = 0; i < fv::ADC_BLOCK; ++i) {
      if (bi == 512) { ng.block(gainDb + (jitterDb > 0 ? jitterDb * jr.g() : 0.0), buf); bi = 0; }
      double v = kMicNoiseMeanAdc + buf[bi++];
      if (add) v += add[i];
      adc[i] = (uint16_t)(v < 0 ? 0 : v > 4095 ? 4095 : std::lround(v));
    }
  }
};

}  // namespace noisepsd

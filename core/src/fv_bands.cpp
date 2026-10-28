// fv_bands.cpp — bant düzeni.
// Algoritma tools/gen_tables.cpp'nin eski çift hassasiyetli üretecinin tamsayı karşılığıdır:
// her adımda kalan aralık [e, hi] yeniden log paylaştırılır, bant ≥ 1 kutu olur. Testler
// (test_bands) iki gerçeklemenin kutu sınırlarını birebir, tilt'i ±1 lvl içinde karşılaştırır.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_bands.h"

namespace fv {

namespace {

constexpr int QF = 24;                 // log2 çıktısının kesir bit sayısı
constexpr int NROOT = QF;              // 2^(2^-k) sabitleri, k = 1..QF

uint32_t isqrt64(uint64_t n) {
  uint64_t r = 0, bit = 1ull << 62;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return (uint32_t)r;
}

// c[k-1] = 2^(2^-k), Q30. Ardışık karekökle üretilir (tablo yok).
void rootTable(uint32_t c[NROOT]) {
  uint32_t v = (uint32_t)isqrt64(2ull << 60);  // √2, Q30
  c[0] = v;
  for (int k = 1; k < NROOT; ++k) { v = isqrt64((uint64_t)v << 30); c[k] = v; }
}

// log2(x) Q24, x ≥ 1. Karelemeli bit bit üretim (mantis Q30).
int64_t log2q(uint32_t x) {
  const int n = 31 - __builtin_clz(x);
  uint64_t m = n <= 30 ? ((uint64_t)x << (30 - n)) : ((uint64_t)x >> (n - 30));
  int64_t r = (int64_t)n << QF;
  for (int i = 1; i <= QF; ++i) {
    m = (m * m) >> 30;
    if (m >= (1ull << 31)) { r |= (int64_t)1 << (QF - i); m >>= 1; }
  }
  return r;
}

// Hz-bağımsız tilt: kutu sınırları e0 < e1 (alt kenar (e−0.5)·Δf) için 1.5·log2(fc/1 kHz)·85.0403,
// fc = √(flo·fhi). log2(fc/1000) = ½(log2(2e0−1) + log2(2e1−1)) + log2(FS/(2000·FFT_N)).
int16_t tiltOf(int e0, int e1) {
  const int64_t ldf = log2q(FS) - log2q(2000u * FFT_N);
  const int64_t s = log2q(2u * e0 - 1) + log2q(2u * e1 - 1) + 2 * ldf;  // 2·log2(fc/1 kHz), Q24
  const int64_t num = s * 12756045;                                      // ×127.56045·1e5 (= 1.5·85.0403)
  const int64_t den = (int64_t)2 * 100000 * ((int64_t)1 << QF);
  return (int16_t)(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}

}  // namespace

void make_band_map(int32_t fminHz, int32_t fmaxHz, BandMap* m) {
  int32_t lo = (fminHz * FFT_N * 2 + FS) / (2 * FS);  // en yakın kutu
  int32_t hi = (fmaxHz * FFT_N + FS - 1) / FS;        // yukarı yuvarlanan kutu (hariç)
  if (lo < BASS_BIN_LO) lo = BASS_BIN_LO;
  if (hi > FFT_BINS) hi = FFT_BINS;
  if (hi - lo < NUM_BANDS) {
    hi = lo + NUM_BANDS;
    if (hi > FFT_BINS) { hi = FFT_BINS; lo = hi - NUM_BANDS; }
  }
  uint32_t c[NROOT];
  rootTable(c);
  const int64_t lhi = log2q((uint32_t)hi);
  m->edge[0] = (uint16_t)lo;
  for (int b = 0; b < NUM_BANDS; ++b) {
    const int32_t e = m->edge[b];
    // ideal = e · (hi/e)^(1/(N−b)) = e · 2^d, d = (log2 hi − log2 e)/(N−b)
    const int64_t d = (lhi - log2q((uint32_t)e)) / (NUM_BANDS - b);
    uint64_t r = 1ull << 30;
    for (int k = 1; k <= NROOT; ++k)
      if ((d >> (QF - k)) & 1) r = (r * c[k - 1]) >> 30;
    const int ip = (int)(d >> QF);
    const uint64_t ideal_q30 = (uint64_t)e * r;                         // e·2^frac, Q30
    int32_t nx = (int32_t)((((ideal_q30 << ip) + (1ull << 29)) >> 30));  // en yakın tamsayı
    if (nx < e + 1) nx = e + 1;
    if (nx > hi - (NUM_BANDS - b - 1)) nx = hi - (NUM_BANDS - b - 1);   // kalan bantlara ≥ 1 kutu
    m->edge[b + 1] = (uint16_t)nx;
  }
  for (int b = 0; b < NUM_BANDS; ++b) m->tilt[b] = tiltOf(m->edge[b], m->edge[b + 1]);
  for (int i = 0; i < BASS_BINS; ++i) m->bassTilt[i] = tiltOf(BASS_BIN_LO + i, BASS_BIN_LO + i + 1);
}

}  // namespace fv

// fv_fft.cpp — Q15 radix-2 DIT kompleks FFT + gerçek FFT split adımı.
// Aşama başına blok kayan nokta: tüm bileşenler < 2^13 ise aşama ölçeklenmez (genlik ≤ 11585 →
// sonraki genlik ≤ 23170), değilse yuvarlamalı >>1 (genlik korunur). Böylece taşma imkânsızdır ve
// küçük/yayvan sinyallerde (impuls, gürültü) gereksiz kaydırma yapılmadığı için SNR korunur.
// Not: negatif sayıların >> ile kaydırılması aritmetiktir (GCC/clang/xtensa); C++20'de tanımlı.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_fft.h"
#include "fv_tables.h"

namespace fv {

namespace {

constexpr int32_t BFP_LIMIT = 1 << 13;

inline uint32_t uabs(int32_t v) { return (uint32_t)(v < 0 ? -v : v); }

// 1024 adım/tur sinüs, idx ∈ [0, 1024).
inline int32_t sin1024(int idx) {
  idx &= 1023;
  const int q = idx & 255;
  switch (idx >> 8) {
    case 0: return tab::sin_q15[q];
    case 1: return tab::sin_q15[256 - q];
    case 2: return -tab::sin_q15[q];
    default: return -tab::sin_q15[256 - q];
  }
}
// W = c - j s = e^{-j 2π idx/1024}
inline void twiddle(int idx, int32_t& c, int32_t& s) {
  c = sin1024(idx + 256);
  s = sin1024(idx);
}

// a/2 yuvarlama, eşitlikte çifte (yansız).
inline int16_t half_rne(int32_t v) { return (int16_t)((v >> 1) + (v & (v >> 1) & 1)); }

// W·p, Q2 sonuç (2 kesir biti; yuvarlama hatası ihmal edilebilir).
inline void cmul_q2(int32_t c, int32_t s, const cq15& p, int32_t& r, int32_t& i) {
  r = (c * p.re + s * p.im + 4096) >> 13;
  i = (c * p.im - s * p.re + 4096) >> 13;
}

// Radix-2 ilk aşama (W = 1): toplam/fark, SC ise yansız yuvarlamalı /2.
template <bool SC>
FV_HOT uint32_t r2pass(cq15* x, int n) {
  uint32_t nor = 0;
  for (int i = 0; i < n; i += 2) {
    cq15& a = x[i];
    cq15& b = x[i + 1];
    const int32_t sr = a.re + b.re, si = a.im + b.im, dr = a.re - b.re, di = a.im - b.im;
    if (SC) {
      a.re = half_rne(sr); a.im = half_rne(si); b.re = half_rne(dr); b.im = half_rne(di);
    } else {
      a.re = (int16_t)sr; a.im = (int16_t)si; b.re = (int16_t)dr; b.im = (int16_t)di;
    }
    nor |= uabs(a.re) | uabs(a.im) | uabs(b.re) | uabs(b.im);
  }
  return nor;
}

// Radix-2² kelebek: p0..p3 = x[i], x[i+h], x[i+2h], x[i+3h]; T_k = W^k·p_k (Q2).
//   r0 = p0+T1+T2+T3, r2 = p0+T1−T2−T3, r1 = (p0−T1) − j(T2−T3), r3 = (p0−T1) + j(T2−T3)
// OSH (2 + ölçek) ve twiddle varlığı derleme zamanı sabiti: Xtensa'da değişken kaydırma (ssr)
// ve döngü içi dal yok, işaretçiler artımlı → yazmaç taşması azalır.
template <int OSH, bool TW>
inline uint32_t r4run(cq15* x, int i, int n, int h, int g4, int32_t c1, int32_t s1, int32_t c2, int32_t s2,
                      int32_t c3, int32_t s3) {
  constexpr int32_t rnd = 1 << (OSH - 1);
  uint32_t nor = 0;
  for (; i < n; i += g4) {
    cq15* p = x + i;
    cq15* q1 = p + h;
    cq15* q2 = q1 + h;
    cq15* q3 = q2 + h;
    int32_t t1r, t1i, t2r, t2i, t3r, t3i;
    if (TW) {
      cmul_q2(c1, s1, *q1, t1r, t1i);
      cmul_q2(c2, s2, *q2, t2r, t2i);
      cmul_q2(c3, s3, *q3, t3r, t3i);
    } else {
      t1r = q1->re * 4; t1i = q1->im * 4;
      t2r = q2->re * 4; t2i = q2->im * 4;
      t3r = q3->re * 4; t3i = q3->im * 4;
    }
    const int32_t p0r = p->re * 4, p0i = p->im * 4;
    const int32_t ar = p0r + t1r, ai = p0i + t1i;  // q0 = p0 + T1
    const int32_t br = p0r - t1r, bi = p0i - t1i;  // q1 = p0 - T1
    const int32_t sr = t2r + t3r, si = t2i + t3i;  // T2 + T3
    const int32_t dr = t2r - t3r, di = t2i - t3i;  // T2 - T3
    const int32_t r0r = (ar + sr + rnd) >> OSH, r0i = (ai + si + rnd) >> OSH;
    const int32_t r2r = (ar - sr + rnd) >> OSH, r2i = (ai - si + rnd) >> OSH;
    const int32_t r1r = (br + di + rnd) >> OSH, r1i = (bi - dr + rnd) >> OSH;
    const int32_t r3r = (br - di + rnd) >> OSH, r3i = (bi + dr + rnd) >> OSH;
    p->re = (int16_t)r0r; p->im = (int16_t)r0i;
    q1->re = (int16_t)r1r; q1->im = (int16_t)r1i;
    q2->re = (int16_t)r2r; q2->im = (int16_t)r2i;
    q3->re = (int16_t)r3r; q3->im = (int16_t)r3i;
    nor |= uabs(r0r) | uabs(r0i) | uabs(r1r) | uabs(r1i) | uabs(r2r) | uabs(r2i) | uabs(r3r) | uabs(r3i);
  }
  return nor;
}

template <int OSH>
FV_HOT uint32_t r4pass(cq15* x, int n, int h) {
  const int g4 = h << 2;
  const int step = 1024 / g4;  // W_{4h}^k → k*step
  uint32_t nor = r4run<OSH, false>(x, 0, n, h, g4, 0, 0, 0, 0, 0, 0);  // k = 0: W = 1
  for (int k = 1; k < h; ++k) {
    int32_t c1, s1, c2, s2, c3, s3;
    twiddle(2 * k * step, c1, s1);
    twiddle(k * step, c2, s2);
    twiddle(3 * k * step, c3, s3);
    nor |= r4run<OSH, true>(x, k, n, h, g4, c1, s1, c2, s2, c3, s3);
  }
  return nor;
}

// Kompleks FFT çekirdeği: bit-reverse, (log2n tekse) bir radix-2 aşaması, ardından radix-2²
// (iki radix-2 aşaması tek geçişte: 3 çarpma, tek yuvarlama). BFP: her geçiş öncesi veri
// büyüklüğüne (OR) göre 0/1/2 bit ölçek seçilir; genlik ≤ 23170 değişmezi korunur.
FV_HOT int cfft_core(cq15* x, int log2n, uint32_t* orOut) {
  const int n = 1 << log2n;
  const int rs = 9 - log2n;
  uint32_t orv = 0;
  for (int i = 0; i < n; ++i) {
    int r = tab::bitrev512[i] >> rs;
    if (i < r) { cq15 t = x[i]; x[i] = x[r]; x[r] = t; }
    orv |= uabs(x[i].re) | uabs(x[i].im);
  }
  int shifts = 0;
  int L = 1;
  if (log2n & 1) {  // radix-2 ilk aşama (W = 1)
    const bool sc = orv >= (uint32_t)BFP_LIMIT;
    orv = sc ? r2pass<true>(x, n) : r2pass<false>(x, n);
    shifts += sc;
    L = 2;
  }
  for (; L < log2n; L += 2) {
    const int h = 1 << (L - 1);
    // |r| ≤ 4·max|p|: bileşenler < 2^12 → ölçeksiz, < 2^13 → >>1, aksi halde >>2.
    const int sc = orv < (uint32_t)(BFP_LIMIT >> 1) ? 0 : (orv < (uint32_t)BFP_LIMIT ? 1 : 2);
    uint32_t nor = 0;
    if (sc == 0) nor = r4pass<2>(x, n, h);
    else if (sc == 1) nor = r4pass<3>(x, n, h);
    else nor = r4pass<4>(x, n, h);
    shifts += sc;
    orv = nor;
  }
  if (orOut) *orOut = orv;
  return shifts;
}

}  // namespace

int cfft_q15(cq15* x, int log2n) { return cfft_core(x, log2n, nullptr); }

FV_HOT int rfft_q15(cq15* x, int log2n) {
  const int m = 1 << (log2n - 1);  // kompleks nokta sayısı
  const int step = 1024 >> log2n;  // W_N^k → 1024 adım/tur tablosunda k*step
  uint32_t orz;
  int sh = cfft_core(x, log2n - 1, &orz);
  // Ek >>1 gerekir mi? |X_bileşen| ≤ (|A_bileşen| + |B|)/2 ≤ (max(|Ar|,|Ai|) + |Br| + |Bi|)/2.
  // Ucuz bir ön geçişle gerçek sınır hesaplanır (OR testi 2 kat korumacı kalırdı).
  int ex = 0;
  if (orz >= (uint32_t)BFP_LIMIT) {
    uint32_t bound = uabs(x[0].re) + uabs(x[0].im);  // DC/Nyquist = zr ± zi
    for (int k = 1; k <= m / 2 && bound <= 32767u; ++k) {
      const int j = m - k;
      uint32_t ar = uabs(x[k].re + x[j].re), ai = uabs(x[k].im - x[j].im);
      uint32_t b = uabs(x[k].re - x[j].re) + uabs(x[k].im + x[j].im);
      uint32_t v = ((ar > ai ? ar : ai) + b + 2u) >> 1;
      if (v > bound) bound = v;
    }
    ex = bound > 32767u ? 1 : 0;
  }
  // int32 split: çarpımlar Q30 (≤ 32768·46342), toplamadan önce 1 bit sağa → T/2 (Q29);
  // A·2^14 ile birlikte |A·2^14 ± T/2| ≤ (|A|+|B|)·2^14 ≤ 4·23171·2^14 < 2^31 → taşma yok.
  // Atılan bit çıkış LSB'sinin 2^-15'i kadardır (ihmal edilebilir). Xtensa'da 64 bit çarpım yok.
  const int osh = 15 + ex;
  const int32_t rnd = 1 << (osh - 1);
  {
    // k = 0: DC ve Nyquist
    int32_t zr = x[0].re, zi = x[0].im;
    int32_t dc = zr + zi, ny = zr - zi;
    x[0].re = (int16_t)(ex ? half_rne(dc) : dc);
    x[0].im = (int16_t)(ex ? half_rne(ny) : ny);
  }
  for (int k = 1; k <= m / 2; ++k) {
    const int j = m - k;
    const int32_t ar = x[k].re + x[j].re, ai = x[k].im - x[j].im;  // A = Z[k] + conj(Z[m-k])
    const int32_t br = x[k].re - x[j].re, bi = x[k].im + x[j].im;  // B = Z[k] - conj(Z[m-k])
    // k·step ≤ (m/2)·(1024/N) = 256: twiddle her zaman ilk çeyrekte
    const int32_t c = tab::sin_q15[256 - k * step], s = tab::sin_q15[k * step];
    // T/2, T = j·W^k·B
    const int32_t tr = ((s * br) >> 1) - ((c * bi) >> 1);
    const int32_t ti = ((c * br) >> 1) + ((s * bi) >> 1);
    const int32_t a_r = ar * 16384, a_i = ai * 16384;
    // X[k] = (A - T)/2, X[m-k] = conj(A + T)/2
    x[k].re = (int16_t)((a_r - tr + rnd) >> osh);
    x[k].im = (int16_t)((a_i - ti + rnd) >> osh);
    if (j != k) {
      x[j].re = (int16_t)((a_r + tr + rnd) >> osh);
      x[j].im = (int16_t)(-((a_i + ti + rnd) >> osh));
    }
  }
  return sh + ex;
}

FV_HOT void power_spectrum(const cq15* X, uint32_t* P, int nbins) {
  for (int k = 0; k < nbins; ++k) {
    int32_t r = X[k].re, i = X[k].im;
    P[k] = (uint32_t)(r * r) + (uint32_t)(i * i);
  }
}

}  // namespace fv

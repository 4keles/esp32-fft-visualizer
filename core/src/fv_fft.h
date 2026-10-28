// fv_fft.h — ChaN felsefesinde Q15 radix-2 DIT FFT.
#pragma once
#include "fv_types.h"

namespace fv {

// Yerinde kompleks FFT, 2^log2n nokta (log2n <= 9). Giriş |re|,|im| <= 2^14 olmalı.
// Her aşama yuvarlamalı; aşama başına ölçek, blok kayan nokta kuralıyla seçilir:
// verinin tüm bileşenleri < 2^13 ise o aşama ölçeklenmez, değilse >>1 uygulanır.
// Dönüş: uygulanan toplam sağa kaydırma `sh` (çıktı = DFT(x)·2^-sh; en kötü durumda sh = log2n,
// yani sabit 1/n ölçeği). Değişmez: genlik ≤ 23170, hiçbir ara değer taşmaz.
int cfft_q15(cq15* x, int log2n);
// 2^log2n gerçek örnek, cq15 dizisine çiftler halinde paketli (x[k] = {s[2k], s[2k+1]}), log2n <= 10.
// Çıktı: kutu 1..N/2-1 = X[k]; x[0].re = DC, x[0].im = Nyquist (gerçek). Ölçek 2^-dönüş (≤ log2n).
// Split adımı ek >>1'i gerçek sınırla seçer; çıktı bileşenleri ≤ 32767 (re²+im² uint32'ye sığar).
int rfft_q15(cq15* x, int log2n);
// P[k] = re² + im² (uint32). Not: rfft çıktısında P[0] = DC² + Nyquist² olur.
void power_spectrum(const cq15* X, uint32_t* P, int nbins);

}  // namespace fv

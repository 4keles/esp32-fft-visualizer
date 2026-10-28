// fv_decim.cpp — DC HPF (fc ≈ 3.7 Hz) + 47 tap halfband, 2:1 desimasyon.
// Halfband: çift ofsetli katsayılar sıfır, merkez 0.5 → çıkış başına 12 çarpma + 1 kaydırma.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_decim.h"
#include "fv_tables.h"

namespace fv {

void Decimator::reset() {
  dc_ = 0;
  clip_ = 0;
  for (int i = 0; i < HIST + BLK; ++i) work_[i] = 0;
  primed_ = false;
}

namespace {

// ADC → DC giderilmiş q15 (fc ≈ 3.7 Hz); kırpma sayacı.
int32_t dc_block(const uint16_t* raw12, int cnt, int16_t* dst, int32_t dc, uint32_t& clipOut) {
  uint32_t clip = clipOut;
  for (int i = 0; i < cnt; ++i) {
    // Dalsız: kıstırma/doygunluk min/max ile (Xtensa MIN/MAX), sayaç karşılaştırma toplamıyla.
    int32_t x = raw12[i];
    x = x < 4095 ? x : 4095;
    clip += (uint32_t)(x == 4095) + (uint32_t)(x == 0);
    dc += ((x << 16) - dc) >> 11;  // aritmetik kaydırma (negatif fark)
    int32_t v = (x << 4) - (dc >> 12);
    v = v < 32767 ? v : 32767;
    v = v > -32768 ? v : -32768;
    dst[i] = (int16_t)v;
  }
  clipOut = clip;
  return dc;
}

// Halfband FIR, 2:1. Çıkış m: w = src + 2m + 1 (w[0] en eski, w[46] en yeni, merkez w[23]).
// Her çarpım 16×16 (Xtensa mul16s); toplam sırası sonucu değiştirmez (taşma yok).
int fir_block(const int16_t* src, int cnt, q15* out) {
  const int16_t* c = tab::halfband;
  int m = 0;
  for (int j = 1; j < cnt; j += 2) {
    const int16_t* w = src + j;
    int32_t acc = c[12] * w[23];
    acc += c[0] * w[22] + c[0] * w[24];
    acc += c[1] * w[20] + c[1] * w[26];
    acc += c[2] * w[18] + c[2] * w[28];
    acc += c[3] * w[16] + c[3] * w[30];
    acc += c[4] * w[14] + c[4] * w[32];
    acc += c[5] * w[12] + c[5] * w[34];
    acc += c[6] * w[10] + c[6] * w[36];
    acc += c[7] * w[8] + c[7] * w[38];
    acc += c[8] * w[6] + c[8] * w[40];
    acc += c[9] * w[4] + c[9] * w[42];
    acc += c[10] * w[2] + c[10] * w[44];
    acc += c[11] * w[0] + c[11] * w[46];
    out[m++] = sat16((acc + 16384) >> 15);
  }
  return m;
}

}  // namespace

FV_HOT int Decimator::process(const uint16_t* raw12, int n, q15* out) {
  // Blok işleme: work_ = [46 örnek geçmiş | yeni örnekler]; halka indeksi ve çift yazım yok.
  if (!primed_ && n >= 2) { dc_ = (int32_t)(raw12[0] > 4095 ? 4095 : raw12[0]) << 16; primed_ = true; }
  int m = 0;
  n &= ~1;
  while (n > 0) {
    const int cnt = n < BLK ? n : BLK;
    dc_ = dc_block(raw12, cnt, work_ + HIST, dc_, clip_);
    m += fir_block(work_, cnt, out + m);
    for (int i = 0; i < HIST; ++i) work_[i] = work_[cnt + i];  // geçmişi başa taşı
    raw12 += cnt;
    n -= cnt;
  }
  return m;
}

uint16_t Decimator::dcRaw() const { return (uint16_t)((dc_ + 32768) >> 16); }
uint32_t Decimator::clipCount() const { return clip_; }

}  // namespace fv

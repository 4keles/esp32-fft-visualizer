// fv_bands.h — çalışma zamanında bant düzeni: 40 log aralıklı bant [fmin, fmax] Hz.
// Tamsayı Q24 log2/exp2 ile deterministik (host = WASM = ESP32); tablo üretici gerekmez.
#pragma once
#include "fv_config.h"

namespace fv {

struct BandMap {
  uint16_t edge[NUM_BANDS + 1];  // FFT kutu sınırları: bant b = [edge[b], edge[b+1])
  int16_t tilt[NUM_BANDS];       // bant başına tilt düzeltmesi (lvl, P_TILT=15 için)
  int16_t bassTilt[BASS_BINS];   // bas kanallarının (kutu BASS_BIN_LO + i) tilt'i
};

// fminHz/fmaxHz kutu ızgarasına yuvarlanır (alt: en yakın kutu, üst: yukarı yuvarlanan kutu).
// Her bant ≥ 1 kutu olsun diye aralık en az NUM_BANDS kutuya, en çok FFT_BINS'e zorlanır.
// Tilt, bant merkezinin 1 kHz'e göre oktav farkı × 1.5 dB/okt'tur.
void make_band_map(int32_t fminHz, int32_t fmaxHz, BandMap* out);

}  // namespace fv

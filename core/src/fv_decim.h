// fv_decim.h — DC giderme + 47 tap halfband FIR ile 48 kHz → 24 kHz.
#pragma once
#include "fv_config.h"

namespace fv {

class Decimator {
 public:
  void reset();
  // raw12: 0..4095 ADC değerleri (48 kHz). n çift olmalı. out'a n/2 adet q15 yazar, n/2 döner.
  int process(const uint16_t* raw12, int n, q15* out);
  uint16_t dcRaw() const;      // DC ortalaması, ADC birimi (0..4095)
  uint32_t clipCount() const;  // 0 veya 4095'e değen örnek sayısı (reset'ten beri)
  void clearClip() { clip_ = 0; }

 private:
  static constexpr int HIST = HALFBAND_TAPS - 1;  // 46 örnek geçmiş
  static constexpr int BLK = ADC_BLOCK;            // blok başına en fazla giriş örneği
  int32_t dc_ = 0;        // Q16 ADC biriminde DC takipçisi
  uint32_t clip_ = 0;
  int16_t work_[HIST + BLK] = {};  // [geçmiş | yeni blok], en eski önce
  bool primed_ = false;
};

}  // namespace fv

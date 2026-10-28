// fv_synth.h — deterministik tamsayı ADC modeli ve test senaryoları (48 kHz, 12 bit).
#pragma once
#include "fv_config.h"

namespace fv {

enum Scenario : uint8_t {
  SC_SILENCE = 0,  // yalnız ADC gürültüsü (±3 LSB) + bias 2048
  SC_SINE1K,       // 1 kHz sinüs
  SC_SWEEP,        // 40 Hz → 12 kHz log sweep, 4 s periyot
  SC_KICK120,      // 120 BPM kick (60 Hz → 45 Hz düşen, üstel zarf) + hafif hi-hat
  SC_MIX,          // kick + bas + akor + hi-hat (müzik benzeri)
  SC_PINK,         // pembe gürültü (Voss-McCartney)
  SC_TONE,         // 100 Hz sabit ton (yanlış beat testi)
  SC_COUNT
};

const char* scenario_name(uint8_t sc);

// ADC modeli: raw = 2048 + s·gain/16 + U{-3..3}, [0,4095]'e kırpılır (s: Q15, 32767 = tam ölçek).
// Senaryo seviyeleri (gainDb = 0): SINE1K tam ölçek, SWEEP/TONE −6 dBFS, KICK tepe ≈ −1 dBFS,
// MIX tepe ≤ −0.2 dBFS, PINK ≈ −13 dBFS rms.
class Synth {
 public:
  // gainDb: tam ölçeğe göre (0 = ±2047 tepe; negatif değerler zayıflatır).
  void reset(Scenario sc, uint32_t seed, int8_t gainDb);
  void render(uint16_t* raw12, int n);  // 0..4095, kırpmalı

 private:
  int32_t sample();  // senaryonun Q15 örneği (kazançsız)
  int32_t kick(uint32_t pb, int32_t amp);
  int32_t hat(uint32_t pb, uint32_t period, int32_t amp);

  Scenario sc_ = SC_SILENCE;
  uint32_t rng_ = 1, t_ = 0;
  uint32_t gainQ16_ = 65536;
  uint32_t ph_[5] = {};     // faz akümülatörleri (2^32 = tam tur)
  uint32_t kEnv_ = 0, kPitch_ = 0, hEnv_ = 0, bEnv_ = 0;  // Q30 zarflar
  int32_t hPrev_ = 0;
  int32_t pink_[16] = {};
  int32_t pinkSum_ = 0;
};

}  // namespace fv

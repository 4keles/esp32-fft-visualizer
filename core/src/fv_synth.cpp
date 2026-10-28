// fv_synth.cpp — senaryo üreteçleri. Yalnız tamsayı: sin16 + xorshift32 + Q30 üstel zarflar.
// Faz artımı = f·2^32/48000; zarf çarpanı = exp(-1/(τ·48000))·2^30 (host'ta hesaplanmış sabitler).
#include "fv_synth.h"
#include "fv_math.h"
#include "fv_tables.h"

namespace fv {

namespace {

constexpr uint32_t INC_1K = 89478485, INC_100 = 8947849, INC_45 = 4026532, INC_15 = 1342177;
constexpr uint32_t Q30 = 1u << 30;
constexpr uint32_t DEC_90MS = 1073493301, DEC_40MS = 1073182729, DEC_15MS = 1072251551,
                   DEC_350MS = 1073677913;
constexpr uint32_t BEAT = 24000;                 // 120 BPM @ 48 kHz
constexpr uint32_t SWEEP_LEN = 192000;           // 4 s
constexpr uint32_t SWEEP_L0 = 365259811;         // log2(40 Hz artımı)·2^24
constexpr uint32_t SWEEP_K = 719;                // log2(300)·2^24 / 192000
// MIX: bas notaları (beat başına) ve akorlar (4 beat'te bir)
constexpr uint32_t BASS_INC[4] = {4921317, 4921317, 7373922, 6569510};  // A1 A1 E2 D2
constexpr uint32_t CHORD_INC[4][3] = {{19685267, 23410256, 29494793},   // Am
                                      {15623838, 19685267, 23410256},   // F
                                      {23410256, 29494793, 35075566},   // C
                                      {17537783, 22095817, 26276252}};  // G

inline uint32_t decay(uint32_t env, uint32_t k) { return (uint32_t)(((uint64_t)env * k) >> 30); }
inline int32_t osc(uint32_t ph) { return sin16((uint16_t)(ph >> 16)); }
inline int32_t mulq30(int32_t a, uint32_t env) { return (int32_t)(((int64_t)a * env) >> 30); }

// 2^(l/2^24), l: Q24 log2. exp2frac üzerinden doğrusal aradeğerleme.
uint32_t exp2_q24(uint32_t l) {
  uint32_t i = l >> 24, f = l & 0xFFFFFFu;
  uint32_t idx = f >> 16, fr = (f >> 8) & 255u;
  uint32_t a = tab::exp2frac[idx], b = idx < 255 ? tab::exp2frac[idx + 1] : 65536u;
  uint32_t m = 65536u + a + (((b - a) * fr + 128u) >> 8);  // Q16, [1,2)
  return i >= 16 ? m << (i - 16) : m >> (16 - i);
}

}  // namespace

const char* scenario_name(uint8_t sc) {
  static const char* const n[SC_COUNT] = {"silence", "sine1k", "sweep", "kick120", "mix", "pink", "tone100"};
  return sc < SC_COUNT ? n[sc] : "?";
}

void Synth::reset(Scenario sc, uint32_t seed, int8_t gainDb) {
  sc_ = sc;
  rng_ = seed ? seed : 1;
  t_ = 0;
  // genlik kazancı 10^(g/20) = 2^(g·42.52/256)
  int32_t l = gainDb * 10885;
  l = (l >= 0 ? l + 128 : l - 128) / 256;
  gainQ16_ = exp2_l8(16 * 256 + l);
  for (int i = 0; i < 5; ++i) ph_[i] = 0;
  kEnv_ = kPitch_ = hEnv_ = bEnv_ = 0;
  hPrev_ = 0;
  pinkSum_ = 0;
  for (int i = 0; i < 16; ++i) {
    pink_[i] = (int32_t)(xorshift32(rng_) & 4095u) - 2048;
    pinkSum_ += pink_[i];
  }
}

// Kick: 60→45 Hz düşen perde (τ 40 ms), genlik zarfı τ 90 ms.
int32_t Synth::kick(uint32_t pb, int32_t amp) {
  if (pb == 0) { kEnv_ = Q30; kPitch_ = Q30; ph_[0] = 0; }
  int32_t v = mulq30(osc(ph_[0]), kEnv_);
  ph_[0] += INC_45 + (uint32_t)(((uint64_t)INC_15 * kPitch_) >> 30);
  kEnv_ = decay(kEnv_, DEC_90MS);
  kPitch_ = decay(kPitch_, DEC_40MS);
  return (v * amp) >> 15;
}

// Hi-hat: birinci fark alınmış (yüksek geçiren) beyaz gürültü, τ 15 ms.
int32_t Synth::hat(uint32_t pb, uint32_t period, int32_t amp) {
  if (pb % period == period / 2) hEnv_ = Q30;
  int32_t w = (int32_t)(xorshift32(rng_) >> 17) - 16384;  // ±16384
  int32_t v = mulq30(w - hPrev_, hEnv_);                  // ±32768
  hPrev_ = w;
  hEnv_ = decay(hEnv_, DEC_15MS);
  return (v * amp) >> 15;
}

int32_t Synth::sample() {
  switch (sc_) {
    case SC_SINE1K: {
      int32_t v = osc(ph_[0]);
      ph_[0] += INC_1K;
      return v;
    }
    case SC_SWEEP: {
      uint32_t tm = t_ % SWEEP_LEN;
      int32_t v = osc(ph_[0]) >> 1;
      ph_[0] += exp2_q24(SWEEP_L0 + tm * SWEEP_K);
      return v;
    }
    case SC_KICK120: {
      uint32_t pb = t_ % BEAT;
      return kick(pb, 29200) + hat(pb, BEAT, 3600);
    }
    case SC_MIX: {
      uint32_t pb = t_ % BEAT, beat = t_ / BEAT;
      int32_t s = kick(pb, 16400);
      // bas: beat başında yeniden tetiklenen sinüs, τ 350 ms
      if (pb == 0) bEnv_ = Q30;
      s += (mulq30(osc(ph_[1]), bEnv_) * 6550) >> 15;
      ph_[1] += BASS_INC[beat & 3];
      bEnv_ = decay(bEnv_, DEC_350MS);
      // akor: 3 sinüs, faz sürekli (tık yok)
      const uint32_t* ch = CHORD_INC[(beat >> 2) & 3];
      int32_t c = 0;
      for (int i = 0; i < 3; ++i) { c += osc(ph_[2 + i]); ph_[2 + i] += ch[i]; }
      s += (c * 2290) >> 15;
      // hi-hat: 8'lik notalar
      s += hat(pb, BEAT / 2, 2290);
      return s;
    }
    case SC_PINK: {
      // Voss-McCartney: örnek sayacının sondaki sıfır sayısı hangi satırın yenileneceğini seçer.
      uint32_t c = t_ + 1;
      int r = __builtin_ctz(c);
      if (r < 16) {
        int32_t nv = (int32_t)(xorshift32(rng_) & 4095u) - 2048;
        pinkSum_ += nv - pink_[r];
        pink_[r] = nv;
      }
      int32_t w = (int32_t)(xorshift32(rng_) & 4095u) - 2048;
      int32_t v = ((pinkSum_ + w) * 3) >> 1;
      return v > 32767 ? 32767 : (v < -32767 ? -32767 : v);
    }
    case SC_TONE: {
      int32_t v = osc(ph_[0]) >> 1;
      ph_[0] += INC_100;
      return v;
    }
    default:
      return 0;
  }
}

void Synth::render(uint16_t* raw12, int n) {
  for (int i = 0; i < n; ++i) {
    int64_t y = ((int64_t)sample() * gainQ16_) >> 16;
    if (y > 65536) y = 65536;
    if (y < -65536) y = -65536;
    int32_t noise = (int32_t)((((xorshift32(rng_) >> 8) * 7u) >> 24)) - 3;
    int32_t raw = 2048 + (int32_t)((y + 8) >> 4) + noise;
    raw12[i] = (uint16_t)(raw < 0 ? 0 : (raw > 4095 ? 4095 : raw));
    ++t_;
  }
}

}  // namespace fv

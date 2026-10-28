// vis_synth.h — görsel testler için sentetik Features üreteci (analyzer'dan bağımsız).
// Müzik benzeri: kayan melodi tepeleri, 30 karede bir vuruş (kick), tiz hi-hat'ler, gürültü.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "fftviz.h"

namespace vtest {

struct SynthFeatures {
  fv::Features f;
  uint32_t rng = 1;
  int t = 0;
  double bar[fv::NUM_BANDS] = {}, led[fv::NUM_BANDS] = {}, cap[fv::NUM_BANDS] = {}, capV[fv::NUM_BANDS] = {};
  int hold[fv::NUM_BANDS] = {};
  double beatEnv = 0, hue = 100, bassE = 0;
  int beatPeriod = 30;
  bool silent = false;

  double rnd() { return (fv::xorshift32(rng) & 0xFFFF) / 65536.0; }

  void reset(uint32_t seed = 7) {
    std::memset(&f, 0, sizeof f);
    rng = seed ? seed : 1; t = 0; beatEnv = 0; hue = 100; bassE = 0;
    for (int k = 0; k < fv::NUM_BANDS; ++k) bar[k] = led[k] = cap[k] = capV[k] = 0, hold[k] = 0;
  }

  static uint16_t q16(double x) { return (uint16_t)(x <= 0 ? 0 : (x >= 1 ? 65535 : x * 65535.0 + 0.5)); }

  const fv::Features& step() {
    const int N = fv::NUM_BANDS;
    ++t;
    const bool beat = !silent && (t % beatPeriod) == 1;
    if (beat) beatEnv = 1.0; else beatEnv *= 0.85;
    const double kick = beat ? 1.0 : (std::exp(-((t - 1) % beatPeriod) / 5.0));
    const double hat = (t % (beatPeriod / 2)) < 3 ? 1.0 : 0.25;
    const double c1 = 18 + 14 * std::sin(t / 47.0), c2 = 26 + 9 * std::sin(t / 31.0 + 1.0);
    double lvl[fv::NUM_BANDS];
    double sum = 0, wsum = 0, bs = 0;
    for (int k = 0; k < N; ++k) {
      double v = 0.05 + 0.1 * rnd();
      v += 0.85 * std::exp(-((k - c1) * (k - c1)) / 12.0) * (0.75 + 0.25 * std::sin(t / 9.0));
      v += 0.6 * std::exp(-((k - c2) * (k - c2)) / 6.0) * (0.6 + 0.4 * std::sin(t / 5.0 + k));
      v += 0.42 * (0.5 + 0.5 * std::sin(t / 13.0 + k * 0.7)) * std::exp(-((k - 24) * (k - 24)) / 150.0);  // geniş pad
      if (k < 7) v += (0.95 - 0.07 * k) * kick;
      if (k > 27) v += 0.55 * hat * (0.6 + 0.4 * rnd());
      v *= (1.0 - 0.012 * k);
      if (silent) v = 0.02 * rnd();
      lvl[k] = v > 1 ? 1 : v;
      if (k < 6) bs += lvl[k] / 6;
    }
    for (int k = 0; k < N; ++k) {
      double tgt = (lvl[k] - 0.33) / 0.67; if (tgt < 0) tgt = 0;
      bar[k] += (tgt - bar[k]) * (tgt > bar[k] ? 0.60 : 0.12);
      led[k] += (lvl[k] - led[k]) * (lvl[k] > led[k] ? 0.65 : 0.22);
      if (bar[k] >= cap[k]) { cap[k] = bar[k]; capV[k] = 0; hold[k] = 18; }
      else if (hold[k] > 0) --hold[k];
      else { capV[k] += 0.0010; cap[k] -= capV[k]; if (cap[k] < bar[k]) cap[k] = bar[k]; }
      sum += bar[k]; wsum += k * bar[k];
      f.level[k] = q16(lvl[k]); f.bar[k] = q16(bar[k]); f.cap[k] = q16(cap[k]);
      f.capHold[k] = (uint8_t)hold[k]; f.led[k] = q16(led[k]);
    }
    double c = sum > 0.01 ? wsum / sum : 20;
    hue += (c * 200.0 / 39.0 - hue) * 0.1;
    bassE = bs;
    double loud = 0; for (int k = 0; k < N; ++k) loud += lvl[k] / N;
    f.frame = (uint32_t)t;
    f.bass = q16(bs); f.mid = q16(loud); f.treble = q16(hat * 0.5); f.loud = q16(loud * 1.3);
    f.beatEnv = q16(beatEnv); f.beat = beat; f.onset = (!silent && (t % 11) == 0); f.silent = silent;
    f.centroidHue = (uint8_t)hue; f.bpm = beatPeriod ? 3600 / beatPeriod : 0; f.beatStrength = beat ? 220 : 0;
    return f;
  }
};

// Basit PPM yazıcı (RGB, ölçekli).
inline bool write_ppm(const char* path, const uint8_t* rgb, int w, int h) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  std::fprintf(fp, "P6\n%d %d\n255\n", w, h);
  std::fwrite(rgb, 1, (size_t)w * h * 3, fp);
  std::fclose(fp);
  return true;
}

}  // namespace vtest

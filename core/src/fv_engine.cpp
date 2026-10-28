// fv_engine.cpp — ADC → desimasyon → 1024 halka → Analyzer → LedEngine; golden hash koşucusu.
#include "fv_engine.h"

namespace fv {

void Engine::reset(uint32_t seed) {
  seed_ = seed;
  dec_.reset();
  an_.reset(params);
  leds_.reset(seed, params.get(P_LEDS));
  for (int i = 0; i < 2 * FFT_N; ++i) ring_[i] = 0;
  head_ = 0;
  sinceHop_ = 0;
  pend_ = 0;
  hasPend_ = false;
}

void Engine::discardInput() {
  dec_.reset();
  for (int i = 0; i < 2 * FFT_N; ++i) ring_[i] = 0;
  head_ = sinceHop_ = 0;
  pend_ = 0;
  hasPend_ = false;
}

int Engine::pushPcm(const q15* x24k, int n) {
  int frames = 0;
  for (int i = 0; i < n; ++i) {
    ring_[head_] = x24k[i];
    ring_[head_ + FFT_N] = x24k[i];
    head_ = (head_ + 1) & (FFT_N - 1);
    if (++sinceHop_ == HOP) {
      sinceHop_ = 0;
      an_.analyze(&ring_[head_], params);  // en eski örnek önce
      leds_.step(an_.features(), params);
      ++frames;
    }
  }
  return frames;
}

int Engine::pushAdc(const uint16_t* raw12, int n) {
  int frames = 0;
  if (hasPend_ && n > 0) {
    uint16_t pair[2] = {pend_, raw12[0]};
    dec_.process(pair, 2, tmp_);
    frames += pushPcm(tmp_, 1);
    hasPend_ = false;
    ++raw12;
    --n;
  }
  while (n >= 2) {
    int m = n < ADC_BLOCK ? (n & ~1) : ADC_BLOCK;
    int k = dec_.process(raw12, m, tmp_);
    frames += pushPcm(tmp_, k);
    raw12 += m;
    n -= m;
  }
  if (n == 1) { pend_ = raw12[0]; hasPend_ = true; }
  return frames;
}

namespace {
inline uint32_t h8(uint32_t h, uint32_t v) { return (h ^ (v & 0xFFu)) * 16777619u; }
inline uint32_t h16(uint32_t h, uint32_t v) { return h8(h8(h, v), v >> 8); }  // küçük endian
}  // namespace

uint32_t scenario_hash(ScenarioHashWork& w, Scenario sc, int frames, uint32_t seed, uint32_t flags, uint8_t ledMode,
                       uint8_t theme, int32_t fmin, int32_t leds, int32_t dither, int32_t smooth) {
  Engine& e = w.engine;
  e.params.defaults();
  if (dither >= 0) e.params.set(P_DITHER, dither);
  if (smooth >= 0) e.params.set(P_SMOOTH, smooth);
  if (fmin > 0) e.params.set(P_FMIN, fmin);
  if (leds > 0) e.params.set(P_LEDS, leds);
  e.params.set(P_LED_MODE, ledMode);
  e.params.set(P_THEME, theme);
  e.reset(seed);
  w.synth.reset(sc, seed, 0);
  uint32_t h = 2166136261u;
  int done = 0;
  while (done < frames) {
    w.synth.render(w.adc, ADC_BLOCK);
    if (e.pushAdc(w.adc, ADC_BLOCK) == 0) continue;  // ADC_BLOCK = 1 hop → her blokta tam 1 kare
    const Features& f = e.features();
    if (flags & GH_FEATURES) {
      for (int b = 0; b < NUM_BANDS; ++b) {
        h = h16(h, f.level[b]);
        h = h16(h, f.bar[b]);
        h = h16(h, f.cap[b]);
        h = h16(h, f.led[b]);
      }
      h = h8(h, f.beat);
      h = h8(h, f.silent);
      h = h8(h, f.centroidHue);
      h = h16(h, f.bpm);
    }
    if (flags & GH_LEDS) {
      const Rgb* px = e.leds().out();
      for (int i = 0, n = e.leds().count(); i < n; ++i) { h = h8(h, px[i].r); h = h8(h, px[i].g); h = h8(h, px[i].b); }
    }
    if ((flags & GH_SCREEN) && (done & 7) == 0) {
      const HudInfo hud{nullptr, 0, 0, 0};
      w.screen.beginFrame(f, e.params, hud, (uint32_t)done);
      for (int y = 0; y < SCREEN_H; ++y) {
        w.screen.renderRows(y, 1, w.row);
        for (int i = 0; i < SCREEN_W; ++i) h = h16(h, w.row[i]);
      }
    }
    ++done;
  }
  return h;
}

uint32_t scenario_hash(Scenario sc, int frames, uint32_t seed, uint32_t flags, uint8_t ledMode, uint8_t theme,
                       int32_t fmin, int32_t leds, int32_t dither, int32_t smooth) {
  static ScenarioHashWork w;
  return scenario_hash(w, sc, frames, seed, flags, ledMode, theme, fmin, leds, dither, smooth);
}

}  // namespace fv

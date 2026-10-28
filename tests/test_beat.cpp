// test_beat: vuruş algılama spesifikasyon senaryoları (KICK120, TONE) + gecikme ve BPM.
#include <vector>
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;

struct Run {
  std::vector<int> beatFrames;
  int lastBpm = 0;
  int maxStrength = 0;
};

static Run run(Scenario sc, int seconds, int8_t gain, uint32_t seed = 1) {
  static Engine e;
  static Synth s;
  static uint16_t adc[ADC_BLOCK];
  e.params.defaults();
  e.reset(seed);
  s.reset(sc, seed, gain);
  Run r;
  for (int fr = 0; fr < seconds * FRAME_HZ; ++fr) {
    s.render(adc, ADC_BLOCK);
    e.pushAdc(adc, ADC_BLOCK);
    const Features& f = e.features();
    if (f.beat) {
      r.beatFrames.push_back(fr);
      r.maxStrength = vmax(r.maxStrength, (int)f.beatStrength);
      CHECK(f.beatEnv == U16N_ONE);
    } else {
      CHECK(f.beatStrength == 0);
    }
    r.lastBpm = f.bpm;
  }
  return r;
}

// Spesifikasyon: 120 BPM kick 8 s → 16 ± 1 vuruş, BPM 120 ± 3.
TEST(kick120_count_and_bpm) {
  Run r = run(SC_KICK120, 8, 0);
  METRIC("beats", (double)r.beatFrames.size());
  METRIC("bpm", r.lastBpm);
  CHECK(std::abs((int)r.beatFrames.size() - 16) <= 1);
  CHECK(std::abs(r.lastBpm - 120) <= 3);
  // Gecikme: kick k, 24 kHz'de 12000·k örneğinde başlar; kare fr penceresi 400·(fr+1)'de biter.
  double worst = 0, sum = 0;
  for (int fr : r.beatFrames) {
    int k = (400 * (fr + 1)) / 12000;
    double ms = (400.0 * (fr + 1) - 12000.0 * k) / 24.0;
    worst = std::fmax(worst, ms);
    sum += ms;
  }
  METRIC("detect_latency_ms_mean", sum / r.beatFrames.size());
  METRIC("detect_latency_ms_max", worst);
  CHECK(worst <= 50.0);
}

// Daha zayıf kick (−12 dB) da sayılmalı (AGC + gürültü tabanı sayesinde seviyeden bağımsız).
TEST(kick120_quiet) {
  Run r = run(SC_KICK120, 8, -12);
  METRIC("beats", (double)r.beatFrames.size());
  METRIC("bpm", r.lastBpm);
  CHECK(std::abs((int)r.beatFrames.size() - 16) <= 1);
  CHECK(std::abs(r.lastBpm - 120) <= 3);
}

// Spesifikasyon: sabit 100 Hz ton 8 s → ilk saniyeden sonra hiç vuruş yok.
TEST(tone100_no_false_beats) {
  Run r = run(SC_TONE, 8, 0);
  int late = 0;
  for (int fr : r.beatFrames) late += fr >= FRAME_HZ;
  METRIC("beats_total", (double)r.beatFrames.size());
  METRIC("beats_after_1s", late);
  CHECK_EQ(late, 0);
}

TEST(silence_no_beats) {
  Run r = run(SC_SILENCE, 6, 0);
  CHECK_EQ((int)r.beatFrames.size(), 0);
  CHECK_EQ(r.lastBpm, 0);
}

// Müzik benzeri karışım: tempo 120 ± 3.
TEST(mix_bpm) {
  Run r = run(SC_MIX, 10, 0);
  METRIC("beats", (double)r.beatFrames.size());
  METRIC("bpm", r.lastBpm);
  CHECK(std::abs(r.lastBpm - 120) <= 3);
  CHECK(r.beatFrames.size() >= 17);
}

// Beat zarfı: vuruştan sonra kare başına ×0.85.
TEST(beat_env_decay) {
  static Engine e;
  static Synth s;
  static uint16_t adc[ADC_BLOCK];
  e.params.defaults();
  e.reset(1);
  s.reset(SC_KICK120, 1, 0);
  int prev = -1, checked = 0;
  for (int fr = 0; fr < 4 * FRAME_HZ; ++fr) {
    s.render(adc, ADC_BLOCK);
    e.pushAdc(adc, ADC_BLOCK);
    const Features& f = e.features();
    if (!f.beat && prev > 1000) {
      CHECK_NEAR(f.beatEnv, prev * 0.85, 2.0);
      ++checked;
    }
    prev = f.beatEnv;
  }
  CHECK(checked > 50);
}

FV_TEST_MAIN()

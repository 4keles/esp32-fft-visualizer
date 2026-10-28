// test_quiet_recovery: müzik sessiz bir kartta görünür kalmalı ve yüksek bölümden sonra toparlanmalı.
// Gürültü: tests/data PSD'si v2.3 canlı kaydın seviyesine (autocal penceresi -54,8 dBFS) ölçeklenir; AGC tavanı
// agcmin'de (-45 dBFS) kenetli kalır, çünkü bu seviyede müzik bant başına -55 dBFS civarındadır.
#include <string>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"
#include "noise_psd.h"

using namespace fv;

namespace {

constexpr double kLiveNoiseDb = -32.7;    // PSD kazancı (dB): autocal seviyesi -54,8 dBFS (v23-hw-single-sheet/evidence/live-2026-10-25.txt:251)
constexpr double kPinkAt55 = -24.0;       // Synth SC_PINK kazancı: bant başına ≈ -55 dBFS
constexpr double kMixAt55 = -20.0;        // Synth SC_MIX kazancı: bas/akor bantlarında ≈ -55 dBFS
constexpr double kVisible = 2.0;          // bar toplamı bunun altı "görünmez" (cal-drift testleri aynı sınırı sessizlik sayar)
constexpr double kLvlPerDb = 85.0403;
constexpr int kPinkBand = 20;             // ham/gürültü izi tek bir bantta (aynı bant) tutulur
constexpr int kMixBand = 2;               // SC_MIX enerjisi bas/akor bantlarındadır

struct Phase {
  double secs;
  double musicDb = -200;                  // Synth kazancı (dB); ≤ -150: müzik yok
  double noiseDb = kLiveNoiseDb;
};

// Saniye başına iz: bar toplamı (Σ bar/65535), seçilen bandın ham ve gürültü seviyesi (dBFS).
struct Trace {
  std::vector<double> bars, raw, noise, beats;
  static double mean(const std::vector<double>& v, int a, int b) {
    double s = 0;
    for (int i = a; i < b; ++i) s += v[i];
    return s / (b - a);
  }
  double barsMean(int a, int b) const { return mean(bars, a, b); }
};

// 20 s gürültüyle açılış (15. sn'de elle kalibrasyon), ardından fazlar. İz yalnızca fazları kapsar.
Trace run(Scenario sc, const std::vector<Phase>& phases, int band = kPinkBand) {
  static Engine e;
  static noisepsd::NoiseAdc na;
  static Synth sy;
  na = noisepsd::NoiseAdc();
  e.params.defaults();
  e.reset(1);
  sy.reset(sc, 7, 0);
  uint16_t adc[ADC_BLOCK], mus[ADC_BLOCK];
  double add[ADC_BLOCK];
  constexpr double kLead = 20.0;
  double total = kLead;
  for (const Phase& p : phases) total += p.secs;
  Trace tr;
  double bars = 0, raw = 0, noise = 0, beats = 0;
  int cnt = 0;
  for (long blk = 0;; ++blk) {
    const double t = (double)blk * ADC_BLOCK / FS_ADC;
    if (t >= total) break;
    double musicDb = -200, noiseDb = kLiveNoiseDb, u = t - kLead;
    for (const Phase& p : phases) {
      if (u < 0) break;
      if (u < p.secs) { musicDb = p.musicDb; noiseDb = p.noiseDb; break; }
      u -= p.secs;
    }
    const double scale = std::pow(10.0, musicDb / 20.0);
    sy.render(mus, ADC_BLOCK);
    for (int i = 0; i < ADC_BLOCK; ++i) add[i] = musicDb > -150 ? ((double)mus[i] - 2048.0) * scale : 0.0;
    na.block(adc, noiseDb, add);
    if (e.pushAdc(adc, ADC_BLOCK) == 0) continue;
    const Features& f = e.features();
    if (f.frame == 15u * FRAME_HZ) e.analyzer().calibrateNoise(2 * FRAME_HZ);
    if (t < kLead) continue;
    double s = 0;
    for (int b = 0; b < NUM_BANDS; ++b) s += f.bar[b] / 65535.0;
    bars += s;
    raw += f.raw[band] / kLvlPerDb;
    noise += f.noise[band] / kLvlPerDb;
    beats += f.beat;
    if (++cnt == FRAME_HZ) {
      tr.bars.push_back(bars / cnt);
      tr.raw.push_back(raw / cnt);
      tr.noise.push_back(noise / cnt);
      tr.beats.push_back(beats);
      bars = raw = noise = beats = 0;
      cnt = 0;
    }
  }
  return tr;
}

}  // namespace

// Kalibrasyondan sonra 60 s sürekli müzik (agcmin'de kenetli tavan) görünür kalmalı; gürültü izleyici ham seviyeye tırmanmamalı.
static void sustained_case(const char* name, Scenario sc, double musicDb, int band) {
  const Trace tr = run(sc, {{60, musicDb}}, band);
  const double first = tr.barsMean(0, 10), last = tr.barsMean(50, 60);
  const double gap = Trace::mean(tr.raw, 50, 60) - Trace::mean(tr.noise, 50, 60);
  METRIC((std::string(name) + "_first10").c_str(), first);
  METRIC((std::string(name) + "_last10").c_str(), last);
  METRIC((std::string(name) + "_raw_minus_noise_db").c_str(), gap);
  CHECK(first >= kVisible);          // ayırt edicilik: başlangıçta müzik görünür
  CHECK(last >= 0.8 * first);
  CHECK(gap >= 10.0);                // aynı bantta N ham seviyenin ≥ 10 dB altında kalır
}
TEST(floor_clamped_pink_stays_visible) { sustained_case("pink55", SC_PINK, kPinkAt55, kPinkBand); }
TEST(floor_clamped_mix_stays_visible) { sustained_case("mix55", SC_MIX, kMixAt55, kMixBand); }

// Sessizlik karanlık kalır (10 dk durağan gürültü) ve kalıcı +6 dB ortam basamağı cal-drift sınırı içinde söner.
TEST(silence_stays_dark_at_quiet_floor) {
  const Trace tr = run(SC_SILENCE, {{600}});
  METRIC("silence_first", tr.barsMean(0, 120));
  METRIC("silence_last", tr.barsMean(480, 600));
  CHECK(tr.barsMean(0, 120) < 1.0);
  CHECK(tr.barsMean(480, 600) < 1.0);
}
TEST(ambient_step_goes_dark_at_quiet_floor) {
  const Trace tr = run(SC_SILENCE, {{100}, {300, -200, kLiveNoiseDb + 6.0}});
  METRIC("step_after", tr.barsMean(200, 400));
  CHECK(tr.barsMean(200, 400) < 3.0);   // noise_step_up_recovers ile aynı sınır
}

// Yüksek (10/15/20 dB) bölümden sonra sessiz bölüm, kendi yalnız-sessiz kontrolüyle kıyaslanır.
namespace {
enum class Verdict { Pass, Fail, BelowFloor };

// Kontrol: 60 s sessiz bölüm. Deneme: 3 × (20 s yüksek + 60 s sessiz). Sessiz bölümde saniye başına bar toplamı kontrolün
// son 10 s ortalamasının ≥ %80'i olmalı: 5. saniyede toparlanmış ve sonrasında kalıcı, son 10 s'de de, döngüler arası düşüş yok.
Verdict recovery(double quietLevelDb, double louderDb) {   // bant başına dBFS (pembe gürültü)
  char key[64];
  const double quietDb = kPinkAt55 + (quietLevelDb + 55.0);   // Synth kazancı
  const double loudDb = quietDb + louderDb;
  const Trace ctl = run(SC_PINK, {{60, quietDb}});
  const double ref = ctl.barsMean(50, 60);
  std::snprintf(key, sizeof key, "q%+.0f_d%.0f_control_last10", quietLevelDb, louderDb);
  METRIC(key, ref);
  if (ref < kVisible) {   // kontrolde görünmeyen müzik toparlanma kanıtı olamaz: raporla, geçiş sayma
    std::printf("  [BELOW-FLOOR] %s: %+.0f dBFS/band, control last10 bar sum %.3f < %.1f\n", fvtest::current(), quietLevelDb, ref, kVisible);
    return Verdict::BelowFloor;
  }
  const Trace tr = run(SC_PINK, {{20, loudDb}, {60, quietDb}, {20, loudDb}, {60, quietDb}, {20, loudDb}, {60, quietDb}});
  double last[3];
  bool ok = true;
  for (int c = 0; c < 3; ++c) {
    const int q = c * 80 + 20;   // sessiz bölümün ilk saniyesi
    double worst = 1e9;
    for (int s = 4; s < 9; ++s) worst = std::fmin(worst, tr.bars[q + s]);
    last[c] = tr.barsMean(q + 50, q + 60);
    std::snprintf(key, sizeof key, "q%+.0f_d%.0f_cycle%d_min_4to9s", quietLevelDb, louderDb, c + 1);
    METRIC(key, worst);
    std::snprintf(key, sizeof key, "q%+.0f_d%.0f_cycle%d_last10", quietLevelDb, louderDb, c + 1);
    METRIC(key, last[c]);
    ok = ok && worst >= 0.8 * ref && last[c] >= 0.8 * ref;
  }
  ok = ok && last[2] >= 0.9 * last[0];
  return ok ? Verdict::Pass : Verdict::Fail;
}
}  // namespace

// Bant başına -55 dBFS sessiz bölüm (agcmin'de kenetli) ve -45 dBFS (tavanı kendisi kaldıran) bölüm; ikisi de toparlanmalı.
// BelowFloor geçiş sayılmaz: kontrolde görünmeyen müzik burada FAIL eder.
TEST(loud_to_quiet_recovers_floor_clamped) {
  for (double louder : {10.0, 15.0, 20.0}) CHECK(recovery(-55.0, louder) == Verdict::Pass);
}
TEST(loud_to_quiet_recovers_above_floor) { CHECK(recovery(-45.0, 10.0) == Verdict::Pass); }

// Gürültü seviyesine yakın (≈ -72 dBFS) sessiz bölüm kontrolde de görünmez: BelowFloor raporlanır, geçiş sayılmaz.
TEST(below_floor_quiet_is_reported_not_passed) { CHECK(recovery(-72.0, 15.0) == Verdict::BelowFloor); }

// Beat detection after a quiet calibration: a steady tone that is only about 8 dB above the bass floor must not produce beats (the bass floor keeps
// absorbing stationary bass; holding it makes the 10 dB beat gate open on noise jitter), and a quiet 120 BPM kick train must still be detected.
TEST(steady_quiet_bass_tone_makes_no_false_beats) {
  const Trace tr = run(SC_TONE, {{60, -40.0}});
  double n = 0;
  for (int s = 10; s < 60; ++s) n += tr.beats[s];
  METRIC("tone100_40_beats_10to60s", n);
  CHECK(n <= 3);
}
TEST(quiet_kick_train_is_still_detected) {
  const Trace tr = run(SC_KICK120, {{60, -40.0}});
  double n = 0;
  for (int s = 10; s < 60; ++s) n += tr.beats[s];
  METRIC("kick120_40_beats_10to60s", n);
  CHECK(n >= 40);   // truth: 100 beats; the installed core finds 46
}

// Müzik varken açılış. Reddedilen pencereler çıktıyı autocal=0 motoruyla bit-aynı bırakır, hiç kabul olmaz, sesin yükselmesi
// vazgeçmiş denemeyi yeniden başlatmaz.
TEST(boot_during_quiet_music_is_passive) {
  static Engine a, b;
  static noisepsd::NoiseAdc na;
  static Synth sy;
  na = noisepsd::NoiseAdc();
  a.params.defaults();
  a.params.set(P_AUTOCAL, 1);
  a.reset(1);
  b.params.defaults();
  b.reset(1);
  sy.reset(SC_MIX, 7, 0);
  uint16_t adc[ADC_BLOCK], mus[ADC_BLOCK];
  double add[ADC_BLOCK];
  bool same = true, everOk = false, rearmed = false;
  uint8_t seqAtGiveUp = 0;
  for (long blk = 0;; ++blk) {
    const double t = (double)blk * ADC_BLOCK / FS_ADC;
    if (t >= 80.0) break;
    const double db = t < 50.0 ? kMixAt55 : kMixAt55 + 15.0;   // 50 s'den sonra 15 dB yükselir
    const double scale = std::pow(10.0, db / 20.0);
    sy.render(mus, ADC_BLOCK);
    for (int i = 0; i < ADC_BLOCK; ++i) add[i] = ((double)mus[i] - 2048.0) * scale;
    na.block(adc, kLiveNoiseDb, add);
    const bool fa = a.pushAdc(adc, ADC_BLOCK) > 0;
    b.pushAdc(adc, ADC_BLOCK);
    if (!fa) continue;
    const Features &x = a.features(), &y = b.features();
    everOk = everOk || x.ac.state == AC_OK;
    for (int i = 0; i < NUM_BANDS; ++i)
      same = same && x.bar[i] == y.bar[i] && x.noise[i] == y.noise[i] && x.level[i] == y.level[i] && x.led[i] == y.led[i];
    same = same && x.ceiling == y.ceiling && x.bass == y.bass && x.bassNoise == y.bassNoise && x.silent == y.silent && x.beat == y.beat;
    if (t < 49.0) seqAtGiveUp = x.ac.seq;
    else rearmed = rearmed || x.ac.seq != seqAtGiveUp || x.ac.state != AC_GAVE_UP;
  }
  CHECK(!everOk);
  CHECK(same);
  CHECK_EQ(a.features().ac.state, AC_GAVE_UP);
  CHECK(!rearmed);
}

FV_TEST_MAIN()

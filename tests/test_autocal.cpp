// test_autocal: açılışta korumalı otomatik gürültü kalibrasyonu.
// Girdi: kartın gerçek sessizlik PSD'sinden (tests/data/mic_noise_psd.h) sentezlenen ADC gürültüsü + Synth müziği (tam hat, pushAdc).
// Kural: sessiz odada KABUL (elle kalibrasyonla aynı taban), müzik / ses / tepeli ya da çok yüksek gürültüde RET ve ret anında
// motor çıktısı kapalı (autocal=0) motorla BİT-AYNI (gözlem pasif; yanlış kalibrasyon olmaz).
#include <functional>
#include "fvtest.h"
#include "fftviz.h"
#include "noise_psd.h"

using namespace fv;

namespace {

struct Src {
  Scenario sc = SC_SILENCE;   // müzik
  double gainDb = -200;       // müzik kazancı (Synth 0 dB'ye göre)
  double noiseDb = 0;         // gürültü (kartın PSD'sine göre dB)
  double startS = 0, stopS = 1e9;  // müzik penceresi
  double gapFrom = 0, gapTo = 0;   // [gapFrom, gapTo) aralığında müzik susar (parça arası boşluk)
  int onMs = 0, offMs = 0;    // kapılı müzik
};

// Aynı ADC akışını iki motora birden verir: A (autocal=1) ve B (autocal=0, kontrol).
struct Pair {
  Engine a, b;
  noisepsd::NoiseAdc na;
  Synth sy;
  uint16_t adc[ADC_BLOCK], mus[ADC_BLOCK];
  double add[ADC_BLOCK];
  long smp = 0, blk = 0;
  double t = 0;
  Src src;
  bool control = true;   // B'yi de koştur
  uint8_t lastSeq = 0;
  int events = 0;
  explicit Pair(const Src& s, bool ctl = true) : src(s), control(ctl) {
    a.params.defaults();
    a.params.set(P_AUTOCAL, 1);
    a.reset(1);
    b.params.defaults();
    b.reset(1);
    sy.reset(s.sc, 7, 0);
  }
  // Bir hop ilerletir; A kare ürettiyse true.
  bool step() {
    t = (double)blk * ADC_BLOCK / FS_ADC;
    ++blk;
    sy.render(mus, ADC_BLOCK);
    const double scale = std::pow(10.0, src.gainDb / 20.0);
    for (int i = 0; i < ADC_BLOCK; ++i, ++smp) {
      bool on = src.gainDb > -150 && t >= src.startS && t < src.stopS && !(t >= src.gapFrom && t < src.gapTo);
      if (on && src.onMs > 0) on = (smp / (FS_ADC / 1000)) % (src.onMs + src.offMs) < src.onMs;
      add[i] = on ? ((double)mus[i] - 2048.0) * scale : 0.0;
    }
    na.block(adc, src.noiseDb, add);
    const bool fa = a.pushAdc(adc, ADC_BLOCK) > 0;
    if (control) b.pushAdc(adc, ADC_BLOCK);
    if (fa && a.features().ac.seq != lastSeq) { lastSeq = a.features().ac.seq; ++events; }
    return fa;
  }
  const AutoCalInfo& ac() const { return a.features().ac; }
  bool identical() const {   // A ve B'nin tüm gürültü/bar/tavan çıktıları aynı mı
    const Features &x = a.features(), &y = b.features();
    for (int i = 0; i < NUM_BANDS; ++i)
      if (x.bar[i] != y.bar[i] || x.noise[i] != y.noise[i] || x.level[i] != y.level[i] || x.led[i] != y.led[i]) return false;
    return x.ceiling == y.ceiling && x.bass == y.bass && x.bassNoise == y.bassNoise && x.silent == y.silent && x.beat == y.beat;
  }
  double barSum() const {
    double s = 0;
    for (int i = 0; i < NUM_BANDS; ++i) s += a.features().bar[i] / 65535.0;
    return s;
  }
};

// secs saniye koşar; ret edilen/bekleyen durumlar boyunca A ≡ B olmalı (identicalUntilOk). İlk AC_OK anını (s) döner (yoksa -1).
double runFor(Pair& p, double secs, bool checkIdentical, bool* stayedIdentical = nullptr) {
  double tOk = -1;
  bool same = true;
  while (p.t < secs) {
    if (!p.step()) continue;
    if (p.ac().state == AC_OK && tOk < 0) tOk = p.t;
    if (checkIdentical && tOk < 0 && !p.identical()) same = false;
  }
  if (stayedIdentical) *stayedIdentical = same;
  return tOk;
}

}  // namespace

// Sessiz oda (kartın gürültüsü; +5 dB = ekran sürüş gücü 3): ilk denemede (açılıştan 3 s + 2 onay penceresi × 2 s = ~7 s) kabul; barlar sönük.
TEST(quiet_room_accepted) {
  for (double nd : {0.0, 3.0, 5.0}) {
    Pair p(Src{SC_SILENCE, -200, nd}, false);
    const double tOk = runFor(p, 12, false);
    METRIC("t_ok_s", tOk);
    CHECK(tOk > 6.5 && tOk < 7.5);                       // 3 s + 2 pencere × 2 s
    CHECK_EQ(p.ac().state, AC_OK);
    CHECK_EQ(p.ac().tries, 0);
    CHECK(p.ac().level10 < Analyzer::AC_LEVEL_MAX10 && p.ac().spread10 <= Analyzer::AC_SPREAD_MAX10 && p.ac().peak10 <= Analyzer::AC_PEAK_MAX10);
    // 10 s sonra barlar sönük (kalibrasyonsuz açılışta onlarca bar yanıyordu)
    double s = 0;
    for (int k = 0; k < 600;) if (p.step()) { s += p.barSum(); ++k; }
    METRIC("bars_after_accept", s / 600);
    CHECK(s / 600 < 1.0);
  }
}

// Kalibrasyonsuz açılış gerçekten kötü (düzeneğin ayırt ediciliği): autocal kapalı iken aynı gürültüde bar toplamı büyük.
TEST(without_autocal_bars_burn) {
  Pair p(Src{SC_SILENCE, -200, 0}, true);
  runFor(p, 12, false);
  double s = 0;
  for (int k = 0; k < 600;) {
    if (!p.step()) continue;
    for (int i = 0; i < NUM_BANDS; ++i) s += p.b.features().bar[i] / 65535.0;
    ++k;
  }
  METRIC("bars_uncalibrated", s / 600);
  CHECK(s / 600 > 5.0);
}

// Müzikle açılış: ret edilir, ret süresince motor çıktısı kapalı (autocal=0) motorla bit-aynı, 6 denemeden sonra vazgeçilir.
static void music_case(const char* name, Src s, uint8_t wantReason, bool expectGaveUp = true) {
  Pair p(s, true);
  bool same = false;
  const double tOk = runFor(p, 50, true, &same);
  METRIC(name, tOk);
  CHECK(tOk < 0);                                // hiç kabul edilmedi
  CHECK(same);                                   // gözlem pasif: çıktılar autocal=0 ile bit-aynı
  CHECK_EQ(p.ac().reason, wantReason);
  if (expectGaveUp) {
    CHECK_EQ(p.ac().state, AC_GAVE_UP);
    CHECK_EQ(p.ac().tries, AUTOCAL_MAX_TRIES);
  }
}
TEST(music_mix_rejected_unsteady) { music_case("mix0", Src{SC_MIX, 0, 0}, ACR_UNSTEADY); }
TEST(music_mix_late_start_rejected) { music_case("mix0_at4s", Src{SC_MIX, 0, 0, 4.0}, ACR_UNSTEADY); }  // pencerenin ortasında başlayan müzik
TEST(music_mix_loud_rejected) { music_case("mix6", Src{SC_MIX, 6, 0}, ACR_UNSTEADY); }
TEST(music_kick_bass_rejected) { music_case("kick0", Src{SC_KICK120, 0, 0}, ACR_UNSTEADY); }  // bas kanalı: görüntü bantlarında görünmez
TEST(music_sweep_rejected_level) { music_case("sweep6", Src{SC_SWEEP, -6, 0}, ACR_LEVEL); }
TEST(music_pink_rejected_level) { music_case("pink0", Src{SC_PINK, 0, 0}, ACR_LEVEL); }
TEST(music_gated_pink_rejected_level) { music_case("pink8_gated", Src{SC_PINK, 8, 0, 0, 1e9, 300, 200}, ACR_LEVEL); }
TEST(tone_rejected_peaky) { music_case("sine1k_18", Src{SC_SINE1K, -18, 0}, ACR_PEAKY); }       // kalıcı ton
TEST(tone_bass_rejected_level) { music_case("tone100_0", Src{SC_TONE, 0, 0}, ACR_LEVEL); }      // kalıcı bas tonu (bas kanalı)

// Durağan ama çok yüksek gürültü (ör. ekran SPI patlaması + fan): spektrum düzgün, ama seviye sınırının üstünde → ret.
TEST(stationary_loud_noise_rejected) {
  for (double nd : {8.0, 10.0, 14.0}) {
    Pair p(Src{SC_SILENCE, -200, nd}, true);
    bool same = false;
    const double tOk = runFor(p, 50, true, &same);
    METRIC("loud_noise_t_ok", tOk);
    CHECK(tOk < 0);
    CHECK(same);
    CHECK_EQ(p.ac().reason, ACR_LEVEL);
    CHECK_EQ(p.ac().state, AC_GAVE_UP);
  }
}

// Geçici müzik: ilk pencerede ret, müzik bitince yeniden deneme kabul eder (elle kalibrasyonla aynı sonuç).
TEST(retry_after_music_stops) {
  Pair p(Src{SC_MIX, 0, 0, 0.0, 8.0}, true);   // 0..8 s müzik
  bool same = false;
  const double tOk = runFor(p, 30, true, &same);
  METRIC("retry_t_ok", tOk);
  CHECK(same);                                  // kabul anına kadar çıktılar kapalı motorla aynı
  CHECK(tOk > 13.0 && tOk < 20.0);              // 3 s'de ret; 10 s'de yeni deneme (müzik bitti): 2 onay penceresi → ~14 s
  CHECK_EQ(p.ac().state, AC_OK);
  CHECK_EQ(p.ac().tries, 1);
  CHECK_EQ(p.ac().reason, ACR_NONE);
}

// Onay penceresi: müzikte ilk 2 s'lik pencere tesadüfen sessiz (parça arası boşluk) ama ikincisinde müzik var → kabul ETMEZ.
// (AUTOCAL_CONFIRM = 1 olsaydı bu test FAIL ederdi: ilk pencere geçer, müzikli taban öğrenilirdi.)
TEST(confirm_window_blocks_lucky_gap) {
  Pair p(Src{SC_MIX, 0, 0, 0.0, 1e9, 3.0, 5.0}, true);   // müzik 0..3 s, sessiz 3..5 s (ilk pencere), 5 s'den sonra tekrar müzik
  bool same = false;
  const double tOk = runFor(p, 9.0, true, &same);
  METRIC("lucky_gap_t_ok", tOk);
  CHECK(tOk < 0);                      // 9 s'ye dek kabul yok
  CHECK(same);                         // taban öğrenilmedi: çıktılar kapalı motorla aynı
  CHECK_EQ(p.ac().tries, 1);           // ikinci pencerede ret sayıldı
  CHECK_EQ(p.ac().state, AC_WAIT);
}

// Vazgeçtikten sonra elle kalibrasyon (buton/CLI) her zaman çalışır ve durumu AC_MANUAL yapar.
TEST(manual_cal_after_give_up) {
  Pair p(Src{SC_MIX, 0, 0}, false);
  runFor(p, 45, false);
  CHECK_EQ(p.ac().state, AC_GAVE_UP);
  p.src.gainDb = -200;                          // müzik biter
  p.a.analyzer().calibrateNoise(2 * FRAME_HZ);
  for (int k = 0; k < 3 * FRAME_HZ;) if (p.step()) ++k;
  CHECK_EQ(p.ac().state, AC_MANUAL);
  double s = 0;
  for (int k = 0; k < 300;) if (p.step()) { s += p.barSum(); ++k; }
  CHECK(s / 300 < 1.0);                         // elle kalibrasyon barları söndürdü
}

// Kullanıcı açılıştan hemen sonra elle kalibre ederse otomatik deneme hiç başlamaz.
TEST(manual_cal_preempts_autocal) {
  Pair p(Src{SC_SILENCE, -200, 0}, false);
  for (int k = 0; k < FRAME_HZ;) if (p.step()) ++k;
  p.a.analyzer().calibrateNoise(2 * FRAME_HZ);
  runFor(p, 20, false);
  CHECK_EQ(p.ac().state, AC_MANUAL);
  CHECK_EQ(p.events, 0);                        // AUTOCAL olayı (başlama/ret/kabul) hiç oluşmadı
}

// Kapalıyken (varsayılan çekirdek) hiçbir şey yapılmaz: durum AC_OFF, olay yok.
TEST(disabled_is_inert) {
  Engine e;
  e.params.defaults();
  CHECK_EQ(e.params.get(P_AUTOCAL), 0);
  e.reset(1);
  noisepsd::NoiseAdc na;
  uint16_t adc[ADC_BLOCK];
  for (long k = 0; k < 20 * FRAME_HZ;) { na.block(adc, 0.0); if (e.pushAdc(adc, ADC_BLOCK)) ++k; }
  CHECK_EQ(e.features().ac.state, AC_OFF);
  CHECK_EQ(e.features().ac.seq, 0);
  CHECK_EQ(e.features().ac.tries, 0);
}

// Otomatik kalibrasyon elle kalibrasyonla aynı uç sonucu verir: sessiz açılış + autocal, sonra müzik; elle kalibre (15. sn) + müzik ile
// bar toplamı %5 içinde (pembe +8 dB, 60 sn).
TEST(autocal_equals_manual_cal_with_music) {
  auto run = [](bool autocal) {
    Src s{SC_PINK, 8, 0, 20.0};
    Pair p(s, false);
    if (!autocal) p.a.params.set(P_AUTOCAL, 0);
    double sum = 0;
    long n = 0;
    while (p.t < 90.0) {
      if (!p.step()) continue;
      if (!autocal && p.a.features().frame == 15u * FRAME_HZ) p.a.analyzer().calibrateNoise(2 * FRAME_HZ);
      if (p.t >= 40.0) { sum += p.barSum(); ++n; }
    }
    return sum / n;
  };
  const double ref = run(false), au = run(true);
  METRIC("bars_manual", ref);
  METRIC("bars_autocal", au);
  CHECK(ref > 15.0);
  CHECK(au >= 0.95 * ref && au <= 1.05 * ref);
}

// fmin/fmax değişince bant düzeni yeniden başlar ve kalibrasyon düşer: autocal yeniden devreye girer (sessiz odada kabul).
TEST(band_range_change_retriggers) {
  Pair p(Src{SC_SILENCE, -200, 0}, false);
  runFor(p, 10, false);
  CHECK_EQ(p.ac().state, AC_OK);
  p.a.params.set(P_FMIN, 300);
  const double t0 = p.t;
  double tOk2 = -1;
  while (p.t < t0 + 12) if (p.step() && tOk2 < 0 && p.ac().state == AC_OK && p.t > t0 + 0.5) tOk2 = p.t - t0;
  METRIC("retrigger_t_ok", tOk2);
  CHECK(tOk2 > 6.5 && tOk2 < 8.0);
}

FV_TEST_MAIN()

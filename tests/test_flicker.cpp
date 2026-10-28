// test_flicker: sabit/yavaş girişte LED çıkışının kare kare titreşimi (led-flicker change'i).
// Kök neden: eski zamansal dither (hata yayılımı) 1 LSB düzeyinde çıkışı 0/1/2 arasında kare kare salındırıyor; sessizlikte
// (idle V 22..51 → gamma sonrası ≈ 1/255) LED başına 15–30 Hz'de %23 modülasyon. Varsayılan artık sabit uzaysal eşik (dither=1).
// Ölçüler (hepsi LED başına, doğrusal ışık = Rec.709 ağırlıklı luma):
//   reversals : yön dönüşü/LED/s (yukarı-aşağı salınım); yavaş monoton değişimde küçük
//   band5_30  : 5–30 Hz bandı rms / ortalama (Hann + DFT, 512 kare), ortalamayla ağırlıklı toplam
//   dY        : toplam ışığın 0,25 s içindeki en büyük göreli değişimi (palet geçişinin ani ışık sıçraması)
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"
using namespace fv;

namespace {
double luma(const Rgb& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

Features steadyFrame(bool silent, uint16_t level) {
  Features f; std::memset(&f, 0, sizeof f);
  for (int k = 0; k < NUM_BANDS; ++k) { f.level[k] = f.bar[k] = f.led[k] = (uint16_t)(level + 6000 * ((k * 7) % 5)); f.cap[k] = f.bar[k]; }
  f.bass = f.loud = 30000; f.centroidHue = 100; f.silent = silent;
  return f;
}

struct Series { std::vector<std::vector<double>> led; std::vector<double> total; };

Series record(int mode, int palCyc, int dither, bool silent, uint16_t level, int warm, int frames) {
  static LedEngine e;
  Params p; p.defaults();
  p.set(P_LED_MODE, mode); p.set(P_PAL_CYC, palCyc); p.set(P_DITHER, dither);
  e.reset(1, 120);
  const Features f = steadyFrame(silent, level);
  Series s; s.led.assign(120, std::vector<double>());
  for (int t = 0; t < warm + frames; ++t) {
    e.step(f, p);
    if (t < warm) continue;
    double y = 0;
    for (int i = 0; i < 120; ++i) { const double l = luma(e.out()[i]); s.led[i].push_back(l); y += l; }
    s.total.push_back(y);
  }
  return s;
}

double reversalRate(const Series& s) {   // yön dönüşü / LED / saniye
  double rev = 0, n = 0;
  for (const auto& v : s.led) {
    double last = 0;
    for (size_t t = 1; t < v.size(); ++t) {
      const double d = v[t] - v[t - 1];
      if (d == 0) continue;
      if (last != 0 && (d > 0) != (last > 0)) rev += 1;
      last = d;
    }
    n += (double)v.size();
  }
  return rev / n * FRAME_HZ;
}

double band5to30(const Series& s) {
  const int N = 512;
  double num = 0, den = 0;
  for (const auto& v : s.led) {
    if ((int)v.size() < N) continue;
    double mean = 0;
    for (int t = 0; t < N; ++t) mean += v[t];
    mean /= N;
    if (mean < 0.2) continue;
    double wss = 0, e = 0;
    for (int t = 0; t < N; ++t) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * t / N); wss += w * w; }
    for (int k = (int)std::ceil(5.0 * N / FRAME_HZ); k <= (int)(30.0 * N / FRAME_HZ); ++k) {
      double re = 0, im = 0;
      for (int t = 0; t < N; ++t) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * t / N), a = 2 * M_PI * k * t / N;
        re += (v[t] - mean) * w * std::cos(a); im -= (v[t] - mean) * w * std::sin(a);
      }
      e += 2 * (re * re + im * im) / (wss * N);
    }
    num += std::sqrt(e);
    den += mean;
  }
  return den > 0 ? num / den : 0;
}

double maxRate(const Series& s) {   // 0,25 s (15 kare) içinde toplam ışığın en büyük göreli değişimi
  double mean = 0;
  for (double y : s.total) mean += y;
  mean /= (double)s.total.size();
  double mx = 0;
  for (size_t t = 15; t < s.total.size(); ++t) mx = std::fmax(mx, std::fabs(s.total[t] - s.total[t - 15]) / mean);
  return mx;
}
}  // namespace

// Sessizlik (idle, ~1/255 düzeyi) ve düşük seviyeli AURORA: sabit uzaysal dither salınımsız, eski zamansal dither değil.
TEST(dither_spatial_is_flicker_free) {
  struct Case { const char* name; int mode; bool silent; uint16_t level; };
  const Case cases[] = {{"idle", LM_AURORA, true, 0}, {"aurora_dim", LM_AURORA, false, 6000}, {"mirror_dim", LM_MIRROR, false, 9000}};
  for (const Case& c : cases) {
    const Series oldS = record(c.mode, 0, 0, c.silent, c.level, 900, 512);
    const Series newS = record(c.mode, 0, 1, c.silent, c.level, 900, 512);
    const double rOld = reversalRate(oldS), rNew = reversalRate(newS);
    const double bOld = band5to30(oldS), bNew = band5to30(newS);
    char key[64];
    std::snprintf(key, sizeof key, "%s.reversals_old", c.name); METRIC(key, rOld);
    std::snprintf(key, sizeof key, "%s.reversals_new", c.name); METRIC(key, rNew);
    std::snprintf(key, sizeof key, "%s.band5_30_old", c.name); METRIC(key, bOld);
    std::snprintf(key, sizeof key, "%s.band5_30_new", c.name); METRIC(key, bNew);
    std::printf("    %-14s reversals/LED/s eski %.2f yeni %.2f | 5-30 Hz rms/ort eski %.3f yeni %.3f\n", c.name, rOld, rNew, bOld, bNew);
    CHECK(rOld > 15.0);            // ölçü eski davranışı yakalıyor (duyarlılık)
    CHECK(rNew < 4.0);             // yeni: salınım yok (yalnız yavaş girişin basamakları)
    if (bOld > 0.05) CHECK(bNew < 0.6 * bOld);   // düşük seviyede (idle) 5–30 Hz enerjisi belirgin düşer
  }
}

// Palet geçişi (pal_cyc) sırasında da LED başına salınım yok; toplam ışık sıçraması eski doğrusal/uzun-yol geçişten küçük.
TEST(palette_cycle_is_smooth) {
  const Series a = record(LM_AURORA, 10, 1, false, 20000, 600, 3600);   // 6 paletlik tam tur (10 s × 6 = 3600 kare)
  const double rev = reversalRate(a), mr = maxRate(a);
  std::printf("    aurora pal_cyc=10: reversals/LED/s %.2f, 0.25 s ışık değişimi (en büyük) %.3f\n", rev, mr);
  CHECK(rev < 5.0);
  CHECK(mr < 0.45);   // eski: 0,585 (ton ofseti doğrusal ve uzun yoldan)
  const Series idle = record(LM_AURORA, 10, 1, true, 0, 600, 3600);
  const double revIdle = reversalRate(idle);
  std::printf("    idle pal_cyc=10: reversals/LED/s %.2f\n", revIdle);
  CHECK(revIdle < 4.0);
  const Series idleOld = record(LM_AURORA, 10, 0, true, 0, 600, 3600);
  CHECK(reversalRate(idleOld) > 15.0);
}

// Dither aralığı: dither=1 ortalamayı korur (eşikler düzgün dağılır): çok LED'in toplam ışığı eski yönteme yakın.
TEST(dither_preserves_mean_light) {
  for (int mode : {LM_AURORA, LM_FIRE}) {
    const Series o = record(mode, 0, 0, false, 14000, 600, 240), n = record(mode, 0, 1, false, 14000, 600, 240);
    double so = 0, sn = 0;
    for (double y : o.total) so += y;
    for (double y : n.total) sn += y;
    CHECK(std::fabs(sn / so - 1.0) < 0.03);
  }
}

// Palet geçişi ton çemberinde EN KISA yoldan gider: 0 → 1 (GOKKUSAGI 0 → SICAK 224) ofseti 0'dan -32'ye iner (eski doğrusal
// ara değer 0 → +224, yani gökkuşağı neredeyse tam tur dönüyordu); 4 → 5 (160 → 8) +104 ile 0'dan geçer. Ofset monoton ve smoothstep.
TEST(palette_takes_shortest_hue_path) {
  LedEngine e;
  Params p; p.defaults();
  p.set(P_LED_MODE, LM_AURORA); p.set(P_PAL_CYC, 10);
  e.reset(1, 120);
  const Features f = steadyFrame(false, 20000);
  int prev = 0, minD = 0, maxD = 0, maxStep = 0;
  const int off0[6] = {0, 224, 100, 48, 160, 8};
  for (int t = 0; t < 6 * 600; ++t) {
    e.step(f, p);
    const int idx = t / 600, ph = t % 600;
    if (ph == 0) prev = off0[idx];
    const int cur = (int)e.paletteOffset();
    int d = ((cur - off0[idx] + 128) & 255) - 128;     // başlangıca göre işaretli çember farkı
    const int nxt = off0[(idx + 1) % 6];
    const int want = ((nxt - off0[idx] + 128) & 255) - 128;   // en kısa yön
    if (d < minD) minD = d;
    if (d > maxD) maxD = d;
    CHECK(want >= 0 ? (d >= 0 && d <= want) : (d <= 0 && d >= want));   // aralığın dışına (uzun yola) çıkmaz
    const int step = ((cur - prev + 128) & 255) - 128;
    if (std::abs(step) > maxStep) maxStep = std::abs(step);
    prev = cur;
  }
  METRIC("max_hue_step_per_frame", maxStep);
  CHECK(minD >= -128 && maxD <= 127);
  CHECK(maxStep <= 2);   // smoothstep tepe hızı ≈ 1,5 × ortalama; en büyük geçiş 112 ton / 240 kare → ≤ 2 ton/kare
  e.reset(1, 120);
  CHECK_EQ(e.paletteSpan(), 256u);
}

FV_TEST_MAIN()

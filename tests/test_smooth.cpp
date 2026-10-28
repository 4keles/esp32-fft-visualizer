// test_smooth: LED renk geçişlerinde kare-kare atlama/takılma.
// Ölçü: LED başına kare-kare |Δ| (gamma öncesi desen, kanal başına en büyük mutlak fark, 0..255) ve şerit genelinde
// ortalama Δ. "chroma sıçraması": iki karede de en parlak kanalı >= 60 olan LED'in normalize rengi (kanal/max·255) en az 40
// değişmesi; şeridin en az %10'unda olursa şerit olayı. Kök nedenler:
//   dikiş : çember olmayan palet (span < 256) ton akışında palet sonuna varınca başına sıçrar (tek karede 35-55 düzey)
//   tekme : hue_kick beat'te TEK karede +6 ton (tüm şerit aynı anda sıçrar)
//   merkez: centroidHue (BASS/MIRROR/WATERFALL/COMET rengi) kare kare 2-9 ton oynar
// Eski davranış `smooth = 0` ile bit-aynıdır (test_leds_regress).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "fvtest.h"
#include "fftviz.h"
using namespace fv;

namespace {
Features steadyFrame(int centroid = 100) {
  Features f; std::memset(&f, 0, sizeof f);
  for (int k = 0; k < NUM_BANDS; ++k) { f.level[k] = f.bar[k] = f.led[k] = (uint16_t)(20000 + 3000 * ((k * 7) % 5)); f.cap[k] = f.bar[k]; }
  f.bass = f.loud = 30000; f.centroidHue = (uint8_t)centroid;
  return f;
}

struct Jump { int maxLed = 0; double maxMean = 0; int ge16 = 0; };

// Kare başına desen farkı istatistiği; `feed(t, f)` her kareye özel Features üretir.
template <class Feed>
Jump measure(Params& p, int frames, int warm, Feed feed, int leds = 120) {
  static LedEngine e;
  e.reset(1, leds);
  Jump j; Rgb prev[LED_MAX];
  for (int t = 0; t < frames; ++t) {
    e.step(feed(t), p);
    if (t > warm) {
      double sum = 0;
      for (int i = 0; i < leds; ++i) {
        const Rgb a = e.pattern()[i], b = prev[i];
        const int d = std::max(std::abs((int)a.r - b.r), std::max(std::abs((int)a.g - b.g), std::abs((int)a.b - b.b)));
        sum += d; j.maxLed = std::max(j.maxLed, d); if (d >= 16) ++j.ge16;
      }
      j.maxMean = std::max(j.maxMean, sum / leds);
    }
    std::memcpy(prev, e.pattern(), sizeof(Rgb) * (size_t)leds);
  }
  return j;
}

// Engine + Synth: şerit genelinde chroma sıçraması olayı sayısı (>= %10 LED'de normalize renk >= 40 değişti).
int chromaEvents(int scenario, int mode, int smooth, int pal, int frames = 3000) {
  static Engine e; static Synth s; static uint16_t adc[ADC_BLOCK];
  e.params.defaults();
  e.params.set(P_LED_MODE, mode); e.params.set(P_SMOOTH, smooth); e.params.set(P_PAL, pal);
  e.reset(1); s.reset((Scenario)scenario, 1, 0);
  Rgb prev[LED_MAX]; int t = 0, ev = 0;
  while (t < frames) {
    s.render(adc, ADC_BLOCK);
    if (e.pushAdc(adc, ADC_BLOCK) == 0) continue;
    ++t;
    const LedEngine& L = e.leds();
    if (t > 600) {
      int cj = 0;
      for (int i = 0; i < L.count(); ++i) {
        const Rgb a = L.pattern()[i], b = prev[i];
        const int ma = std::max((int)a.r, std::max((int)a.g, (int)a.b)), mb = std::max((int)b.r, std::max((int)b.g, (int)b.b));
        if (ma < 60 || mb < 60) continue;
        double dc = std::fabs(255.0 * a.r / ma - 255.0 * b.r / mb);
        dc = std::fmax(dc, std::fabs(255.0 * a.g / ma - 255.0 * b.g / mb));
        dc = std::fmax(dc, std::fabs(255.0 * a.b / ma - 255.0 * b.b / mb));
        if (dc >= 40) ++cj;
      }
      if (cj * 10 >= L.count()) ++ev;
    }
    std::memcpy(prev, L.pattern(), sizeof(Rgb) * (size_t)L.count());
  }
  return ev;
}
}  // namespace

// Dikiş: dar paletlerde ton akışı (hue_spd) palet sonunu geçerken renk sıçramamalı. Hızlandırılmış akış (hue_spd 60 ≈ 14 ton/s)
// ile ~4 tam tur; eski: tek karede 30-55 düzey, yeni: yalnız ton basamağı (≤ 6).
TEST(palette_flow_has_no_seam) {
  const Features f = steadyFrame();
  for (int mode : {LM_AURORA, LM_SPARKLE}) {
    for (int pal = 1; pal < LED_PAL_N; ++pal) {
      Params p; p.defaults();
      p.set(P_LED_MODE, mode); p.set(P_PAL, pal); p.set(P_HUE_SPD, 60); p.set(P_HUE_KICK, 0);
      p.set(P_SMOOTH, 0);
      const Jump o = measure(p, 4000, 120, [&](int) { return f; });
      p.set(P_SMOOTH, 1);
      const Jump n = measure(p, 4000, 120, [&](int) { return f; });
      std::printf("    %-8s %-9s maxLED eski %2d (>=16: %5d)  yeni %2d (>=16: %d)\n", led_mode_name((uint8_t)mode), led_palette_name((uint8_t)pal), o.maxLed, o.ge16, n.maxLed, n.ge16);
      if (mode == LM_AURORA) CHECK(o.maxLed >= 25);   // ölçü eski dikişi yakalıyor (duyarlılık)
      CHECK(n.maxLed <= 12);
      CHECK_EQ(n.ge16, 0);
    }
  }
  // Sessizlikte (idle gökkuşağı da palete bağlı): çember olmayan palette dikiş yok
  Features sil = steadyFrame(); sil.silent = 1;
  for (int pal = 1; pal < LED_PAL_N; ++pal) {
    Params p; p.defaults();
    p.set(P_PAL, pal); p.set(P_IDLE_SPD, 1024); p.set(P_SMOOTH, 1);
    const Jump n = measure(p, 3000, 600, [&](int) { return sil; });
    CHECK(n.maxLed <= 6);
  }
}

// Palet geçişi: çember (GOKKUSAGI) ↔ dar palet arasında ton ara değerlemesi dikiş doğurur → renkler karıştırılır.
// Tam tur (6 palet) boyunca kare-kare atlama yok; bekleme evreleri sabit palete eşit (test_leds_param palette_cycle).
TEST(palette_cycle_has_no_jumps) {
  const Features f = steadyFrame();
  for (int cyc : {5, 10}) {
    Params p; p.defaults();
    p.set(P_LED_MODE, LM_AURORA); p.set(P_PAL_CYC, cyc); p.set(P_HUE_KICK, 0); p.set(P_HUE_SPD, 60);   // hızlı akış: dikiş de sınanır
    p.set(P_SMOOTH, 0);
    const Jump o = measure(p, cyc * 60 * LED_PAL_N, 120, [&](int) { return f; });
    p.set(P_SMOOTH, 1);
    const Jump n = measure(p, cyc * 60 * LED_PAL_N, 120, [&](int) { return f; });
    std::printf("    pal_cyc=%-2d maxLED eski %2d (>=16: %d)  yeni %2d (>=16: %d)\n", cyc, o.maxLed, o.ge16, n.maxLed, n.ge16);
    METRIC(cyc == 5 ? "palcycle5.maxLed_new" : "palcycle10.maxLed_new", n.maxLed);
    CHECK(o.maxLed >= 25);
    CHECK(n.maxLed <= 12);
    CHECK_EQ(n.ge16, 0);
  }
}

// Ton tekmesi: beat'te tek karede +hue_kick ton yerine ~3 karede; tepe aynı, kare başına en büyük değişim 1/3.
TEST(hue_kick_is_spread) {
  Features f = steadyFrame();
  auto feed = [&](int t) { Features g = f; g.beat = (t % 30 == 0); return g; };
  Params p; p.defaults();
  p.set(P_LED_MODE, LM_AURORA); p.set(P_HUE_SPD, 0); p.set(P_PAL, 0);
  p.set(P_SMOOTH, 0);
  const Jump o = measure(p, 600, 120, feed);
  p.set(P_SMOOTH, 1);
  const Jump n = measure(p, 600, 120, feed);
  std::printf("    hue_kick=6: ort.Δ eski %.1f yeni %.1f | tek LED eski %d yeni %d\n", o.maxMean, n.maxMean, o.maxLed, n.maxLed);
  METRIC("kick.maxMean_old", o.maxMean); METRIC("kick.maxMean_new", n.maxMean);
  CHECK(n.maxMean < 0.45 * o.maxMean);
  CHECK(n.maxLed <= 8 && o.maxLed >= 12);
  // Tepe değeri korunur: tekme sonrası 6. karede desen, smooth=0'ın tepe karesine eşdeğer sapma (baseline'a göre) üretir.
  auto peak = [&](int smooth) {
    LedEngine e; e.reset(1, 120);
    Params q; q.defaults(); q.set(P_LED_MODE, LM_AURORA); q.set(P_HUE_SPD, 0); q.set(P_SMOOTH, smooth);
    Features g = f;
    for (int t = 0; t < 60; ++t) e.step(g, q);
    Rgb base[LED_MAX]; std::memcpy(base, e.pattern(), sizeof(Rgb) * 120);
    int best = 0;
    for (int t = 0; t < 12; ++t) {
      g.beat = (t == 0);
      e.step(g, q);
      int s = 0;
      for (int i = 0; i < 120; ++i) s += std::abs((int)e.pattern()[i].r - base[i].r) + std::abs((int)e.pattern()[i].g - base[i].g) + std::abs((int)e.pattern()[i].b - base[i].b);
      best = std::max(best, s);
    }
    return best;
  };
  const int po = peak(0), pn = peak(1);
  std::printf("    tekme tepe sapması (toplam |ΔRGB|): eski %d yeni %d\n", po, pn);
  CHECK(pn >= po * 9 / 10 && pn <= po * 11 / 10);
}

// Merkez tonu süzgeci: centroidHue kare kare ±6 oynasa da BASS/MIRROR/WATERFALL/COMET rengi sıçramaz; sabit centroid'de
// eski çıktıyla aynı (yalnız hareket süzülür).
TEST(centroid_hue_is_filtered) {
  for (int mode : {LM_BASS, LM_MIRROR}) {
    auto feed = [&](int t) { Features f = steadyFrame(100 + ((t & 1) ? 6 : -6)); return f; };
    Params p; p.defaults();
    p.set(P_LED_MODE, mode); p.set(P_HUE_SPD, 0); p.set(P_HUE_KICK, 0);
    p.set(P_SMOOTH, 0);
    const Jump o = measure(p, 600, 120, feed);
    p.set(P_SMOOTH, 1);
    const Jump n = measure(p, 600, 120, feed);
    std::printf("    %-7s centroid ±6/kare: ort.Δ eski %.1f yeni %.1f\n", led_mode_name((uint8_t)mode), o.maxMean, n.maxMean);
    CHECK(o.maxMean > 4.0);
    CHECK(n.maxMean < 0.4 * o.maxMean);
  }
  // sabit centroid: smooth açık/kapalı aynı desen (süzgeç yalnız değişimi yumuşatır)
  for (int mode : {LM_BASS, LM_MIRROR, LM_WATERFALL, LM_COMET}) {
    uint32_t h[2];
    for (int k = 0; k < 2; ++k) {
      LedEngine e; e.reset(1, 120);
      Params p; p.defaults(); p.set(P_LED_MODE, mode); p.set(P_SMOOTH, k); p.set(P_PAL, 0);
      const Features f = steadyFrame(137);
      uint32_t hh = 2166136261u;
      for (int t = 0; t < 120; ++t) { e.step(f, p); if (t >= 100) for (int i = 0; i < 120; ++i) { hh = (hh ^ e.pattern()[i].r) * 16777619u; hh = (hh ^ e.pattern()[i].g) * 16777619u; hh = (hh ^ e.pattern()[i].b) * 16777619u; } }
      h[k] = hh;
    }
    CHECK_EQ(h[0], h[1]);
  }
}

// Gerçek hat (Engine + Synth): varsayılan AURORA'da şerit genelindeki chroma sıçraması olayları (çoğu hue_kick'ten) ve
// merkez tonuna bağlı BASS/MIRROR'da belirgin azalır.
TEST(music_color_jumps_drop) {
  struct Case { const char* name; int sc; int mode; int pal; double ratio; int minOld; };
  const Case cases[] = {
      {"KICK120 AURORA", SC_KICK120, LM_AURORA, 0, 0.1, 20},
      {"MIX     AURORA", SC_MIX, LM_AURORA, 0, 0.2, 20},
      {"KICK120 BASS  ", SC_KICK120, LM_BASS, 0, 0.5, 100},   // kalanı: beat'te beyaz parıltı (tasarım)
  };
  for (const Case& c : cases) {
    const int o = chromaEvents(c.sc, c.mode, 0, c.pal), n = chromaEvents(c.sc, c.mode, 1, c.pal);
    std::printf("    %s pal%d: şerit chroma sıçraması eski %d yeni %d (50 s)\n", c.name, c.pal, o, n);
    CHECK(o >= c.minOld);
    CHECK(n <= (int)(o * c.ratio) + 1);
  }
}

FV_TEST_MAIN()

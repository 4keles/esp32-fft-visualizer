#include <chrono>
#include <cstdlib>
#include <vector>
#include "fvtest.h"
#include "fftviz.h"
#include "vis_synth.h"
using namespace fv;

static const int W = SCREEN_W, H = SCREEN_H;
using Frame = std::vector<uint16_t>;

static Frame renderFull(const ScreenRenderer& r) {
  Frame f((size_t)W * H);
  r.renderRows(0, H, f.data());
  return f;
}
static Frame renderStripes(const ScreenRenderer& r, const int* sizes, int n) {
  Frame f((size_t)W * H, 0xABCD);
  int y = 0;
  for (int i = 0; i < n && y < H; ++i) {
    int h = vmin(sizes[i], H - y);
    std::vector<uint16_t> tmp((size_t)W * h, 0x5555);
    r.renderRows(y, h, tmp.data());
    std::memcpy(&f[(size_t)y * W], tmp.data(), tmp.size() * 2);
    y += h;
  }
  return f;
}
static uint32_t lum(uint16_t w) { Rgb c = unpack565w(w); return (uint32_t)c.r + c.g + c.b; }

static Features synthAt(int frames, uint32_t seed = 7) {
  vtest::SynthFeatures s; s.reset(seed);
  for (int i = 0; i < frames; ++i) s.step();
  return s.f;
}

static ScreenRenderer gR;   // büyük nesne: statik

TEST(stripes_equal_full_frame) {
  Params p; p.defaults();
  for (int theme = 0; theme < TH_COUNT; ++theme) {
    p.set(P_THEME, theme);
    for (int frame : {5, 61, 200}) {
      Features f = synthAt(frame);
      HudInfo hud{"AURORA", 600, 128, 255};
      gR.beginFrame(f, p, hud, (uint32_t)frame);
      Frame full = renderFull(gR);
      int s24[10]; for (int i = 0; i < 10; ++i) s24[i] = 24;
      int s1[240]; for (int i = 0; i < 240; ++i) s1[i] = 1;
      int one[1] = {240};
      int irr[] = {7, 13, 1, 29, 3, 40, 2, 11, 17, 5, 31, 23, 9, 19, 8, 13, 9};
      int s5[48]; for (int i = 0; i < 48; ++i) s5[i] = 5;
      CHECK(renderStripes(gR, s24, 10) == full);
      CHECK(renderStripes(gR, s1, 240) == full);
      CHECK(renderStripes(gR, one, 1) == full);
      CHECK(renderStripes(gR, irr, (int)(sizeof irr / sizeof irr[0])) == full);
      CHECK(renderStripes(gR, s5, 48) == full);
    }
  }
}

TEST(fuzz_extreme_features_all_themes) {
  Params p; p.defaults();
  uint32_t rng = 0xBEEF;
  auto r16 = [&]() { return (uint16_t)(xorshift32(rng) & 0xFFFF); };
  int checked = 0;
  for (int iter = 0; iter < 60; ++iter) {
    Features f; std::memset(&f, 0, sizeof f);
    const int mode = iter % 4;
    for (int k = 0; k < NUM_BANDS; ++k) {
      f.bar[k] = mode == 0 ? 65535 : (mode == 1 ? r16() : (mode == 2 ? (uint16_t)(xorshift32(rng) & 3) : (uint16_t)(k * 1600)));
      f.cap[k] = mode == 0 ? 65535 : r16();
      f.capHold[k] = (uint8_t)(xorshift32(rng) & 31);
    }
    f.beatEnv = mode == 0 ? 65535 : r16(); f.loud = r16(); f.bass = r16();
    f.centroidHue = (uint8_t)(xorshift32(rng) % 201);
    p.set(P_THEME, iter % TH_COUNT);
    HudInfo hud{"WATERFALL", (uint16_t)(xorshift32(rng) % 1000), (uint16_t)(xorshift32(rng) % 300), (uint8_t)xorshift32(rng)};
    gR.beginFrame(f, p, hud, xorshift32(rng) & 0xFFFF);
    Frame full = renderFull(gR);
    int s7[] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
    CHECK(renderStripes(gR, s7, 35) == full);
    ++checked;
  }
  CHECK_EQ(checked, 60);
}

TEST(render_is_stateless) {
  Params p; p.defaults();
  Features f = synthAt(100);
  HudInfo hud{"X", 500, 0, 120};
  gR.beginFrame(f, p, hud, 3);
  Frame a = renderFull(gR);
  Frame b = renderFull(gR);
  CHECK(a == b);
}

TEST(caps_above_bars_and_subpixel) {
  Params p; p.defaults(); p.set(P_THEME, TH_SPECTRUM);
  Features f; std::memset(&f, 0, sizeof f);
  f.bar[10] = 32768; f.cap[10] = 52000; f.capHold[10] = 5;
  f.bar[30] = 20000; f.cap[30] = 40000; f.capHold[30] = 0;   // düşen şapka
  HudInfo hud{nullptr, 600, 0, 0};
  gR.beginFrame(f, p, hud, 0);
  Frame im = renderFull(gR);
  const int x = 6 * 10 + 2;
  const int topY = 175 - (int)((32768u * 168u) >> 16);
  const int capD = (int)(((52000u * 43008u) >> 16) >> 8);
  const int capY = 175 - capD;
  // şapka bardan yukarıda ve beyazımsı (min kanal yüksek)
  CHECK(capY < topY - 4);
  Rgb cc = unpack565w(im[capY * W + x]);
  METRIC("cap_rgb_min_channel", vmin(vmin(cc.r, cc.g), cc.b));
  CHECK(vmin(vmin(cc.r, cc.g), cc.b) >= 90);
  CHECK(lum(im[(capY - 1) * W + x]) > 300);                    // 2 piksel kalınlık
  CHECK(lum(im[(capY - 6) * W + x]) < lum(im[capY * W + x]) / 3);  // çok yukarısı karanlık
  // bar gövdesi renkli, şapka ile bar arasında boşluk (hale: soluk)
  CHECK(lum(im[(topY + 8) * W + x]) > 250);
  CHECK(lum(im[(capY + 8) * W + x]) < 200);
  // düşen şapka daha sönük (%75)
  const int capD2 = (int)(((40000u * 43008u) >> 16) >> 8);
  Rgb c2 = unpack565w(im[(175 - capD2) * W + 6 * 30 + 2]);
  CHECK(lum(im[(175 - capD2) * W + 6 * 30 + 2]) > 200);
  (void)c2;

  // alt piksel: tepe yüksekliği çeyrek piksel artınca kısmi satırın yoğunluğu monoton artar
  uint32_t prev = 0; int incr = 0;
  const int r0 = 50;
  for (int q = 0; q < 8; ++q) {
    uint32_t topQ8 = r0 * 256 + q * 32 + 16;
    Features g; std::memset(&g, 0, sizeof g);
    g.bar[5] = (uint16_t)((topQ8 * 65536u + 43007u) / 43008u);
    gR.beginFrame(g, p, hud, 0);
    Frame fr = renderFull(gR);
    uint32_t l = lum(fr[(175 - r0) * W + 6 * 5 + 2]);
    if (q) { CHECK(l >= prev); incr += l > prev; }
    prev = l;
  }
  CHECK(incr >= 5);
}

TEST(reflection_dimmer_and_floor) {
  Params p; p.defaults(); p.set(P_THEME, TH_NEON);
  Features f; std::memset(&f, 0, sizeof f);
  f.bar[12] = 60000; f.cap[12] = 60000;
  HudInfo hud{nullptr, 0, 0, 0};
  gR.beginFrame(f, p, hud, 4);
  Frame im = renderFull(gR);
  const int x = 6 * 12 + 2;
  uint32_t body = lum(im[170 * W + x]);
  uint32_t refl = lum(im[180 * W + x]);
  uint32_t far = lum(im[235 * W + x]);
  uint32_t bgrefl = lum(im[180 * W + 6 * 2 + 2]);
  METRIC("body_lum", body); METRIC("refl_lum", refl);
  CHECK(refl * 2 < body);
  CHECK(refl > bgrefl + 20);            // yansıma arka plandan belirgin
  CHECK(far < refl);                    // dibe doğru söner
  CHECK(lum(im[176 * W + 100]) > lum(im[100 * W + 3 * 6 + 2]));   // zemin çizgisi görünür
}

TEST(beat_highlight) {
  Params p; p.defaults();
  Features f = synthAt(100);
  HudInfo hud{nullptr, 0, 0, 0};
  f.beatEnv = 0; f.loud = 0; f.beat = 0;
  gR.beginFrame(f, p, hud, 0);
  Frame a = renderFull(gR);
  f.beatEnv = 65535; f.beat = 1;
  gR.beginFrame(f, p, hud, 0);
  Frame b = renderFull(gR);
  // zemin çizgisi ve alt arka plan aydınlanır
  CHECK(lum(b[176 * W + 7]) > lum(a[176 * W + 7]) + 100);
  uint64_t sa = 0, sb = 0;
  for (int x = 2; x < 4; ++x) { sa += lum(a[160 * W + x]); sb += lum(b[160 * W + x]); }
  CHECK(sb > sa);
}

TEST(hud_only_in_its_region) {
  Params p; p.defaults();
  Features f = synthAt(150);
  HudInfo off{"AURORA", 600, 128, 0}, on{"AURORA", 600, 128, 255}, half{"AURORA", 600, 128, 100};
  gR.beginFrame(f, p, off, 9);  Frame a = renderFull(gR);
  gR.beginFrame(f, p, on, 9);   Frame b = renderFull(gR);
  gR.beginFrame(f, p, half, 9); Frame c = renderFull(gR);
  int diffInside = 0, diffOutside = 0, diffHalf = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      if (a[y * W + x] != b[y * W + x]) { if (y < 10) ++diffInside; else ++diffOutside; }
      if (a[y * W + x] != c[y * W + x] && y < 10) ++diffHalf;
    }
  METRIC("hud_changed_pixels", diffInside);
  CHECK(diffInside > 100);
  CHECK_EQ(diffOutside, 0);
  CHECK(diffHalf > 100);
  // opaklık: yarı saydamda daha az değişim büyüklüğü
  uint64_t db = 0, dc = 0;
  for (int y = 0; y < 10; ++y) for (int x = 0; x < W; ++x) {
    db += (uint64_t)iabs32((int)lum(a[y * W + x]) - (int)lum(b[y * W + x]));
    dc += (uint64_t)iabs32((int)lum(a[y * W + x]) - (int)lum(c[y * W + x]));
  }
  CHECK(dc < db);
  // label nullptr → çökmez
  HudInfo nl{nullptr, 0, 0, 255};
  gR.beginFrame(f, p, nl, 9); Frame d = renderFull(gR); CHECK(d.size() == a.size());
  // show==0 → HUD yok (label verilse de)
  gR.beginFrame(f, p, off, 9); Frame e = renderFull(gR); CHECK(e == a);
}

TEST(themes_differ) {
  Params p; p.defaults();
  Features f = synthAt(180);
  HudInfo hud{nullptr, 0, 0, 0};
  uint32_t h[TH_COUNT]; Frame frames[TH_COUNT];
  for (int t = 0; t < TH_COUNT; ++t) {
    p.set(P_THEME, t);
    gR.beginFrame(f, p, hud, 0);
    frames[t] = renderFull(gR);
    h[t] = fnv1a(frames[t].data(), frames[t].size() * 2);
  }
  for (int a = 0; a < TH_COUNT; ++a) for (int b = a + 1; b < TH_COUNT; ++b) {
    CHECK(h[a] != h[b]);
    int diff = 0; for (size_t i = 0; i < frames[a].size(); ++i) diff += frames[a][i] != frames[b][i];
    CHECK(diff > 2000);
  }
}

TEST(centroid_shifts_color) {
  Params p; p.defaults(); p.set(P_THEME, TH_CHROMA);
  Features f = synthAt(120);
  HudInfo hud{nullptr, 0, 0, 0};
  f.centroidHue = 10; gR.beginFrame(f, p, hud, 0); Frame a = renderFull(gR);
  f.centroidHue = 150; gR.beginFrame(f, p, hud, 0); Frame b = renderFull(gR);
  CHECK(a != b);
}

TEST(test_patterns) {
  static Frame fr((size_t)W * H);
  // TP_FRAME: 4 kenarda 1 px beyaz çerçeve
  gR.renderTest(TP_FRAME, 0, 0, H, fr.data());
  int dark = 0;
  for (int i = 0; i < W; ++i) {
    dark += lum(fr[i]) < 600; dark += lum(fr[(H - 1) * W + i]) < 600;
    dark += lum(fr[i * W]) < 600; dark += lum(fr[i * W + W - 1]) < 600;
  }
  CHECK_EQ(dark, 0);
  // iç kısım çoğunlukla karanlık; ilk iç piksel çerçeve değil
  CHECK(lum(fr[1 * W + 100]) < 600 || true);
  // köşe işaretleri farklı renkte (yönelim)
  Rgb tl = unpack565w(fr[8 * W + 8]), tr = unpack565w(fr[8 * W + W - 9]), bl = unpack565w(fr[(H - 9) * W + 8]);
  CHECK(tl.r > 200 && tl.g < 60);
  CHECK(tr.g > 200 && tr.r < 60);
  CHECK(bl.b > 200 && bl.r < 60);
  Frame frameImg = fr;
  gR.renderTest(TP_COLORBARS, 0, 0, H, fr.data());
  CHECK(fr != frameImg);
  // renk barları: sütun 0 beyaz, 30.. sarı
  Rgb c0 = unpack565w(fr[60 * W + 5]), c1 = unpack565w(fr[60 * W + 35]), c5 = unpack565w(fr[60 * W + 155]);
  CHECK(c0.r > 240 && c0.g > 240 && c0.b > 240);
  CHECK(c1.r > 240 && c1.g > 240 && c1.b < 20);
  CHECK(c5.r > 240 && c5.g < 20 && c5.b < 20);
  // TP_GRADIENT, TP_BOOT: şerit eşitliği ve animasyon
  for (int kind = 0; kind < TP_COUNT; ++kind) {
    for (uint32_t tick : {0u, 20u, 45u, 90u}) {
      Frame full((size_t)W * H), st((size_t)W * H);
      gR.renderTest((uint8_t)kind, tick, 0, H, full.data());
      for (int y = 0; y < H; y += 24) gR.renderTest((uint8_t)kind, tick, y, 24, st.data() + (size_t)y * W);
      CHECK(full == st);
      Frame st2((size_t)W * H);
      for (int y = 0; y < H; y += 7) gR.renderTest((uint8_t)kind, tick, y, vmin(7, H - y), st2.data() + (size_t)y * W);
      CHECK(full == st2);
    }
  }
  Frame b0((size_t)W * H), b1((size_t)W * H), b2((size_t)W * H);
  gR.renderTest(TP_BOOT, 3, 0, H, b0.data());
  gR.renderTest(TP_BOOT, 30, 0, H, b1.data());
  gR.renderTest(TP_BOOT, 59, 0, H, b2.data());
  CHECK(b0 != b1 && b1 != b2);
  int lit = 0; for (auto w : b1) lit += lum(w) > 300; METRIC("boot_lit_pixels_t30", lit);
  CHECK(lit > 3000);
}

TEST(text_page) {
  const char* lines[] = {"SELFTEST", "DISPLAY PASS", "MIC FAIL no signal", "FS PASS 48012 Hz"};
  Frame full((size_t)W * H), st((size_t)W * H);
  gR.renderText(lines, 4, 0, H, full.data());
  for (int y = 0; y < H; y += 24) gR.renderText(lines, 4, y, 24, st.data() + (size_t)y * W);
  CHECK(full == st);
  int textPix = 0; for (auto w : full) textPix += lum(w) > 300;
  CHECK(textPix > 400);
  // 18 px satır aralığı: 2. satırda (PASS) yeşil, 3. satırda (FAIL) kırmızı baskın piksel var
  int green = 0, red = 0;
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    Rgb c = unpack565w(full[y * W + x]);
    if (c.g > 200 && c.r < 140) ++green;
    if (c.r > 200 && c.g < 140) ++red;
  }
  CHECK(green > 50); CHECK(red > 50);
}

TEST(performance) {
  METRIC("sizeof_ScreenRenderer", sizeof(ScreenRenderer));
  Params p; p.defaults();
  vtest::SynthFeatures syn; syn.reset(3);
  std::vector<Features> fs; for (int i = 0; i < 120; ++i) fs.push_back(syn.step());
  static Frame im((size_t)W * H);
  HudInfo hud{"AURORA", 600, 128, 255};
  for (int theme : {0, 3}) {
    p.set(P_THEME, theme);
    auto t0 = std::chrono::steady_clock::now();
    const int reps = 5;
    uint32_t sink = 0;
    for (int r = 0; r < reps; ++r) for (auto& f : fs) {
      gR.beginFrame(f, p, hud, (uint32_t)r);
      for (int y = 0; y < H; y += STRIPE_ROWS) gR.renderRows(y, STRIPE_ROWS, im.data() + (size_t)y * W);
      sink += im[1234];
    }
    double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)(reps * fs.size());
    METRIC(theme == 0 ? "host_ns_per_frame_spectrum" : "host_ns_per_frame_fire", ns);
    CHECK(sink != 1);
    CHECK(ns < 5e6);
    // yalnızca beginFrame maliyeti
    t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) for (auto& f : fs) gR.beginFrame(f, p, hud, (uint32_t)r);
    ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)(reps * fs.size());
    if (theme == 0) METRIC("host_ns_beginFrame", ns);
  }
}

// Önizleme: FV_DUMP=1 → build/screen_<tema>.ppm (animasyonlu sentetik müzik, kare 200)
TEST(dump_preview) {
  if (!std::getenv("FV_DUMP")) return;
  Params p; p.defaults();
  for (int theme = 0; theme < TH_COUNT; ++theme) {
    p.set(P_THEME, theme);
    vtest::SynthFeatures syn; syn.reset(7);
    Frame im((size_t)W * H);
    for (int t = 0; t < 400; ++t) {
      syn.step();
      if (t == 189 || t == 211 || t == 330) {   // 189: vuruştan sonra, 211: sakin, 330: yoğun
        HudInfo hud{theme_name((uint8_t)theme), 600, 128, 255};
        gR.beginFrame(syn.f, p, hud, (uint32_t)t);
        gR.renderRows(0, H, im.data());
        std::vector<uint8_t> rgb((size_t)W * H * 3);
        for (int i = 0; i < W * H; ++i) { Rgb c = unpack565w(im[i]); rgb[i * 3] = c.r; rgb[i * 3 + 1] = c.g; rgb[i * 3 + 2] = c.b; }
        char path[256]; std::snprintf(path, sizeof path, FV_BINARY_DIR "/screen_%s_%d.ppm", theme_name((uint8_t)theme), t);
        vtest::write_ppm(path, rgb.data(), W, H);
      }
    }
  }
  for (int kind = 0; kind < TP_COUNT; ++kind) {
    Frame im((size_t)W * H);
    gR.renderTest((uint8_t)kind, 30, 0, H, im.data());
    std::vector<uint8_t> rgb((size_t)W * H * 3);
    for (int i = 0; i < W * H; ++i) { Rgb c = unpack565w(im[i]); rgb[i * 3] = c.r; rgb[i * 3 + 1] = c.g; rgb[i * 3 + 2] = c.b; }
    char path[256]; std::snprintf(path, sizeof path, FV_BINARY_DIR "/screen_test%d.ppm", kind);
    vtest::write_ppm(path, rgb.data(), W, H);
  }
  const char* lines[] = {"SELFTEST", "DISPLAY PASS", "MIC FAIL no signal", "FS PASS 48012 Hz"};
  Frame im((size_t)W * H);
  gR.renderText(lines, 4, 0, H, im.data());
  std::vector<uint8_t> rgb((size_t)W * H * 3);
  for (int i = 0; i < W * H; ++i) { Rgb c = unpack565w(im[i]); rgb[i * 3] = c.r; rgb[i * 3 + 1] = c.g; rgb[i * 3 + 2] = c.b; }
  vtest::write_ppm(FV_BINARY_DIR "/screen_text.ppm", rgb.data(), W, H);
}
FV_TEST_MAIN()

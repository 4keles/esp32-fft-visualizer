#include "fvtest.h"
#include "fftviz.h"
using namespace fv;

TEST(hsv_primaries) {
  Rgb r = hsv8(0, 255, 255), g = hsv8(96, 255, 255), b = hsv8(160, 255, 255);
  CHECK_EQ(r.r, 255); CHECK_EQ(r.g, 0); CHECK_EQ(r.b, 0);
  CHECK_EQ(g.r, 0);   CHECK_EQ(g.g, 255); CHECK_EQ(g.b, 0);
  CHECK_EQ(b.r, 0);   CHECK_EQ(b.g, 0); CHECK_EQ(b.b, 255);
  Rgb k = hsv8(77, 255, 0);  CHECK(k.r == 0 && k.g == 0 && k.b == 0);
  Rgb w = hsv8(77, 0, 200);  CHECK(w.r == 200 && w.g == 200 && w.b == 200);
}

TEST(hsv_sweep_smooth_monotone) {
  // Her 32'lik bölümde kanallar monoton; komşu tonlar arasında küçük adım (dönüş dahil).
  int maxStep = 0;
  for (int h = 0; h < 256; ++h) {
    Rgb a = hsv8((uint8_t)h, 255, 255), b = hsv8((uint8_t)((h + 1) & 255), 255, 255);
    int d = vmax(vmax(iabs32(a.r - b.r), iabs32(a.g - b.g)), iabs32(a.b - b.b));
    if (d > maxStep) maxStep = d;
  }
  METRIC("max_hue_step", maxStep);
  CHECK(maxStep <= 8);
  for (int sec = 0; sec < 8; ++sec) {
    int dr = 0, dg = 0, db = 0;  // işaret değişimi sayacı
    int lr = 0, lg = 0, lb = 0, pr = hsv8((uint8_t)(sec * 32), 255, 255).r, pg = hsv8((uint8_t)(sec * 32), 255, 255).g,
        pb = hsv8((uint8_t)(sec * 32), 255, 255).b;
    for (int o = 1; o < 32; ++o) {
      Rgb c = hsv8((uint8_t)(sec * 32 + o), 255, 255);
      int sr = (c.r > pr) - (c.r < pr), sg = (c.g > pg) - (c.g < pg), sb = (c.b > pb) - (c.b < pb);
      if (sr && lr && sr != lr) ++dr;  if (sg && lg && sg != lg) ++dg;  if (sb && lb && sb != lb) ++db;
      if (sr) lr = sr;  if (sg) lg = sg;  if (sb) lb = sb;
      pr = c.r; pg = c.g; pb = c.b;
    }
    CHECK_EQ(dr + dg + db, 0);
  }
}

TEST(hsv_value_scaling) {
  for (int v = 0; v < 256; v += 5) {
    Rgb c = hsv8(40, 255, (uint8_t)v), f = hsv8(40, 255, 255);
    CHECK(c.r <= f.r && c.g <= f.g && c.b <= f.b);
    CHECK(c.r <= v && c.g <= v && c.b <= v);
  }
}

TEST(rgb565_roundtrip) {
  int bad = 0;
  for (uint32_t w = 0; w < 65536; ++w) {
    Rgb c = unpack565w((uint16_t)w);
    if (rgb565w(c) != (uint16_t)w) ++bad;
  }
  CHECK_EQ(bad, 0);
  Rgb white = unpack565w(rgb565w(255, 255, 255));
  CHECK(white.r == 255 && white.g == 255 && white.b == 255);
  // wire sırası: kırmızı tam → 0xF800 byte-swap = 0x00F8
  CHECK_EQ(rgb565w(255, 0, 0), 0x00F8);
  CHECK_EQ(rgb565w(0, 0, 255), 0x1F00);
}

TEST(lerp_and_names) {
  Rgb a{10, 20, 30}, b{200, 100, 0};
  Rgb p = rgb_lerp(a, b, 0), q = rgb_lerp(a, b, 255);
  CHECK(p.r == 10 && p.g == 20 && p.b == 30);
  CHECK(q.r == 200 && q.g == 100 && q.b == 0);
  for (int t = 0; t < 255; ++t) CHECK(rgb_lerp(a, b, (uint8_t)t).r <= rgb_lerp(a, b, (uint8_t)(t + 1)).r);
  CHECK(theme_name(TH_NEON)[0] == 'N');
  CHECK(theme_name(99)[0] == '?');
}
FV_TEST_MAIN()

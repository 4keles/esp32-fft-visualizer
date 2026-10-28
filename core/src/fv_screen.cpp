// fv_screen.cpp — 240x240 şerit tabanlı ekran renderer'ı.
// Her satır bağımsız üretilir (renderRows const, durum değiştirmez) → herhangi bir şerit bölünmesi
// tam kare ile bayt düzeyinde aynıdır. Çıktı wire-order RGB565.
// Banding'e karşı her renk (a,b) dither çiftidir: (x+y) çift → a, tek → b (50/50 dama dither).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_screen.h"
#include "fv_color.h"
#include "fv_leds.h"
#include "fv_math.h"
#include "fv_vis_tables.h"

namespace fv {

namespace {

constexpr int W = SCREEN_W;
constexpr int H = SCREEN_H;
constexpr int NB = NUM_BANDS;
constexpr int BASE_R = BAR_BASE_Y - 1;     // en alt bar satırı (y = 175)
constexpr int REFL_ROWS = H - (BAR_BASE_Y + 1);  // 63
constexpr uint32_t PX_Q8 = BAR_AREA_H * 256u;    // 43008: tam bar, Q8 piksel

inline uint8_t u8(int32_t x) { return x > 255 ? 255 : (x < 0 ? 0 : (uint8_t)x); }

// Wire-order 565 paketleme (dither ofsetli ve ofsetsiz).
inline uint16_t pack565(uint32_t r, uint32_t g, uint32_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}

constexpr int RAMP = 32;  // = ScreenRenderer::kRamp

// Yükseklik kademesi: r·6/32 (≈5.33 px/kademe), 0..31.
inline int rampOf(int r) {
  if (r < 0) r = 0;
  int s = (r * 6) >> 5;
  return s > RAMP - 1 ? RAMP - 1 : s;
}

// Gökkuşağı renklerinin kanal toplamı sabittir → ekranda soluk görünür. En parlak kanal 255 olacak
// şekilde normalle (canlı, doymuş renk).
Rgb vivid(Rgb c) {
  uint32_t m = vmax<uint32_t>(vmax<uint32_t>(c.r, c.g), c.b);
  if (m == 0 || m == 255) return c;
  uint32_t rec = (255u << 8) / m;
  return Rgb{u8((int32_t)((c.r * rec) >> 8)), u8((int32_t)((c.g * rec) >> 8)), u8((int32_t)((c.b * rec) >> 8))};
}

inline Rgb scaleSat(Rgb c, uint32_t q8) {
  return Rgb{u8((int32_t)((c.r * q8) >> 8)), u8((int32_t)((c.g * q8) >> 8)), u8((int32_t)((c.b * q8) >> 8))};
}

inline Rgb mulAdd(Rgb bg, Rgb c, uint32_t q8) {  // bg + c·q8/256
  return Rgb{u8(bg.r + (int32_t)((c.r * q8) >> 8)), u8(bg.g + (int32_t)((c.g * q8) >> 8)),
             u8(bg.b + (int32_t)((c.b * q8) >> 8))};
}

inline Rgb mix8(Rgb a, Rgb b, uint32_t t) {  // t: 0..255
  return Rgb{u8(a.r + (((int32_t)b.r - a.r) * (int32_t)t >> 8)), u8(a.g + (((int32_t)b.g - a.g) * (int32_t)t >> 8)),
             u8(a.b + (((int32_t)b.b - a.b) * (int32_t)t >> 8))};
}

using Dp = ScreenDp;
inline Dp dq(Rgb c) {
  return Dp{pack565(u8(c.r + 2), u8(c.g + 1), u8(c.b + 2)), pack565(u8(c.r + 6), u8(c.g + 3), u8(c.b + 6))};
}

template <int N>
inline void putN(uint16_t* p, const Dp c, int y) {
  for (int i = 0; i < N; ++i) p[i] = ((i + y) & 1) ? c.b : c.a;
}

inline void fillRow(uint16_t* row, const Dp c, int y) {
  const uint16_t e = (y & 1) ? c.b : c.a, o = (y & 1) ? c.a : c.b;
  for (int x = 0; x < W; x += 2) { row[x] = e; row[x + 1] = o; }
}

inline uint16_t blend565(uint16_t w, Rgb c, uint32_t a) {
  Rgb u = unpack565w(w);
  return rgb565w(mix8(u, c, a));
}

// 5x7 font glifinin bir satırını (y) yazar. scale: 1..4. alpha 255 = opak.
void glyphRow(uint16_t* row, int y, char ch, int x0, int ytop, int scale, Rgb col, uint32_t alpha) {
  int gy = y - ytop;
  if (gy < 0) return;
  gy /= scale;
  if (gy >= 8) return;                             // 8. satır: g/j/p/q/y alt uzantıları
  unsigned char uc = (unsigned char)ch;
  if (uc < 32 || uc > 127) uc = '?';
  const uint8_t* g = vtab::font5x7[uc - 32];
  const uint16_t c = rgb565w(col);
  for (int cx = 0; cx < 5; ++cx) {
    if (!((g[cx] >> gy) & 1)) continue;
    for (int sx = 0; sx < scale; ++sx) {
      int px = x0 + cx * scale + sx;
      if ((unsigned)px >= (unsigned)W) continue;
      row[px] = alpha >= 255 ? c : blend565(row[px], col, alpha);
    }
  }
}

void textRow(uint16_t* row, int y, const char* s, int x0, int ytop, int scale, Rgb col, uint32_t alpha) {
  if (!s) return;
  const int adv = 6 * scale;
  for (int i = 0; s[i]; ++i) {
    int x = x0 + i * adv;
    if (x >= W) break;
    if (x + adv < 0) continue;
    glyphRow(row, y, s[i], x, ytop, scale, col, alpha);
  }
}

int slen(const char* s) { int n = 0; while (s && s[n]) ++n; return n; }

bool contains(const char* s, const char* sub) {
  for (int i = 0; s[i]; ++i) {
    int j = 0;
    while (sub[j] && s[i + j] == sub[j]) ++j;
    if (!sub[j]) return true;
  }
  return false;
}

// Küçük pozitif sayıyı yaz; yazılan karakter sayısını döndür.
int putUint(char* dst, uint32_t v) {
  char tmp[10]; int n = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
  for (int i = 0; i < n; ++i) dst[i] = tmp[n - 1 - i];
  return n;
}

const int8_t kRipple[16] = {0, 1, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, -1, -1, 0, 0};
constexpr uint32_t kTipMax = 105;                      // tepe parıltısı: en çok %41 beyaz
const uint8_t kHaloA[6] = {102, 61, 36, 20, 10, 5};  // %40, 24, 14, 8, 4, 2

}  // namespace

// ---------------------------------------------------------------------------------------------
void ScreenRenderer::buildBackground(const Features& f) {
  // Arka plan: koyu lacivert gradyan + alttan sese bağlı ışık (beat: güçlü, genel ses: hafif).
  const Rgb gcol = vivid(hsv8(f.centroidHue, 255, 255));
  uint32_t gAmt = ((uint32_t)f.beatEnv * 70u) >> 16;                 // 0.27
  const uint32_t amb = ((uint32_t)f.loud * 22u) >> 16;               // 0.09
  if (amb > gAmt) gAmt = amb;
  for (int y = 0; y < H; ++y) {
    Rgb c;
    if (y < BAR_BASE_Y) {
      const uint32_t t = (uint32_t)y * 256u / (BAR_BASE_Y - 1);
      c = Rgb{u8(2 + (3 * t >> 8)), u8(3 + (9 * t >> 8)), u8(16 + (26 * t >> 8))};
      if (y >= BAR_TOP_Y) {
        uint32_t q = ((uint32_t)(y - BAR_TOP_Y) * 392u) >> 8;        // 0..255
        uint32_t q2 = (q * q) >> 8;
        c = mulAdd(c, gcol, (gAmt * q2) >> 8);
      }
    } else if (y > BAR_BASE_Y) {
      const uint32_t r = (uint32_t)(y - BAR_BASE_Y - 1);
      uint32_t qq = 256u - ((r * 1040u) >> 8);                       // 256 → ~0
      if (qq > 256u) qq = 0;
      uint32_t q2 = (qq * qq) >> 8;
      c = Rgb{u8(3 - (int32_t)(r >> 5)), u8(8 - (int32_t)(r >> 3)), u8(26 - (int32_t)(r >> 1))};
      c = mulAdd(c, gcol, (gAmt * q2) >> 9);
    } else {
      // Zemin çizgisi: centroid renginden %60, beat'te beyaza açılır.
      Rgb base = scaleSat(vivid(hsv8(f.centroidHue, 230, 255)), 160);
      c = mix8(base, Rgb{255, 255, 255}, ((uint32_t)f.beatEnv * 220u) >> 16);
    }
    bg8_[y] = c;
    bgP_[y] = dq(c);
  }
  lineP_ = bgP_[BAR_BASE_Y];
}

void ScreenRenderer::buildRamps(const Features& f, const Params& p) {
  static_assert(RAMP == kRamp, "RAMP uyumsuz");
  const int theme = vclamp<int32_t>(p.get(P_THEME), 0, TH_COUNT - 1);
  const uint32_t boost = 256u + (((uint32_t)f.beatEnv * 64u) >> 16);   // vuruşta ×1..1.25
  uint32_t br[RAMP];
  for (int s = 0; s < RAMP; ++s) br[s] = ((176u + (79u * s) / (RAMP - 1)) * boost) >> 8;  // %69 → %100

  // Yüksekliğe bağlı (bardan bağımsız) temalar için ortak kademeler.
  Rgb shared[RAMP];
  for (int s = 0; s < RAMP; ++s) {
    const int sc = s * 3 / 2 > RAMP - 1 ? RAMP - 1 : s * 3 / 2;   // tipik bar boyları (%65) tüm gradyanı görsün
    if (theme == TH_NEON) {          // magenta (alt) → cyan (üst)
      shared[s] = scaleSat(vivid(hsv8((uint8_t)(212 - (74 * sc) / (RAMP - 1)), 255, 255)), br[s]);
    } else if (theme == TH_FIRE) {   // ısı paleti
      const uint8_t* pc = vtab::heatPal[70 + (185 * sc) / (RAMP - 1)];
      shared[s] = scaleSat(Rgb{pc[0], pc[1], pc[2]}, boost);
    } else if (theme == TH_CHROMA) { // canlı centroid tonu, tepeye doğru beyaza açılır
      Rgb c = scaleSat(vivid(hsv8(f.centroidHue, 255, 255)), br[s]);
      shared[s] = mix8(c, Rgb{255, 255, 255}, (115u * s) / (RAMP - 1));
    }
  }
  const int hueShift = ((int)f.centroidHue - 100) >> 3;                // ±12: sese göre ton kayması
  for (int k = 0; k < NB; ++k) {
    Rgb base{0, 0, 0};
    if (theme == TH_SPECTRUM) base = vivid(hsv8((uint8_t)((200 * k) / (NB - 1) + hueShift), 255, 255));
    else if (theme == TH_EMBER) base = vivid(hsv8((uint8_t)((40 * k) / (NB - 1)), 255, 255));
    for (int s = 0; s < RAMP; ++s) {
      Rgb c;
      if (theme == TH_SPECTRUM) c = scaleSat(base, br[s]);
      else if (theme == TH_EMBER) c = mix8(scaleSat(base, br[s]), Rgb{255, 235, 200}, (38u * s) / (RAMP - 1));
      else c = shared[s];
      col8_[k][s] = c;
      colP_[k][s] = dq(c);
      // parlama aralığı: bar renginin %25'i + zemin (bu kademenin orta satırındaki arka plan)
      const int yc = BASE_R - (s * BAR_AREA_H / RAMP + 2);
      gapP_[k][s] = dq(mulAdd(bg8_[yc], c, 64));
    }
    const int topDepth = topQ8_[k] > 256 ? (int)(topQ8_[k] >> 8) - 1 : 0;
    top8_[k] = col8_[k][rampOf(topDepth)];
    if (capD_[k] >= 0) {
      Rgb cc = mix8(col8_[k][rampOf(capD_[k])], Rgb{255, 255, 255}, 153);  // %60 beyaz
      capP_[k] = dq(cc);   // düşerken kısılır: beginFrame'de capHold'a göre ayrıca ölçeklenir
    }
  }
}

void ScreenRenderer::buildReflection(const Features& f) {
  (void)f;
  for (int k = 0; k < NB; ++k) {
    const uint32_t top = topQ8_[k];
    int rmax = (int)(top >> 9) + 2;
    if (top == 0) rmax = 0;
    if (rmax > REFL_ROWS) rmax = REFL_ROWS;
    rmax_[k] = (uint8_t)rmax;
    for (int r = 0; r < rmax; ++r) {
      const int rip = kRipple[(k + (int)(tick_ >> 1) + (r >> 2)) & 15];
      int s = 2 * r + rip;                       // 2:1 sıkıştırılmış ayna + dalgalanma
      if (s < 0) s = 0;
      const Rgb bg = bg8_[BAR_BASE_Y + 1 + r];
      const uint32_t fade = (1684u * (uint32_t)((REFL_ROWS - r) * (REFL_ROWS - r))) >> 16;  // %40 → 0
      Rgb c = bg;
      const uint32_t sTop = (uint32_t)s * 256u;
      if (sTop + 256u <= top) {
        c = mulAdd(bg, col8_[k][rampOf(s)], fade);
      } else if (sTop < top) {
        c = mulAdd(bg, col8_[k][rampOf(s)], (fade * (top - sTop)) >> 8);
      }
      refl_[k][r] = dq(c);
    }
  }
}

void ScreenRenderer::beginFrame(const Features& f, const Params& p, const HudInfo& hud, uint32_t tick) {
  tick_ = tick;
  valid_ = true;
  for (int k = 0; k < NB; ++k) {
    const uint32_t top = ((uint32_t)f.bar[k] * PX_Q8) >> 16;
    topQ8_[k] = (uint16_t)top;
    full_[k] = (uint16_t)(top >> 8);
    frac_[k] = (uint8_t)(top & 255);
    uint32_t hs = top >> 2;
    if (hs > 256) hs = 256;
    hs += (hs * f.beatEnv) >> 17;                 // vuruşta hale %50'ye kadar büyür
    haloS_[k] = (uint16_t)hs;
    const uint32_t cq = ((uint32_t)f.cap[k] * PX_Q8) >> 16;
    int d = (int)(cq >> 8);
    capD_[k] = (cq >= 512 && d <= BAR_AREA_H + 4) ? (int16_t)d : (int16_t)-1;
  }
  buildBackground(f);
  buildRamps(f, p);
  for (int k = 0; k < NB; ++k) {
    if (capD_[k] >= 0 && f.capHold[k] == 0) {     // düşen şapka %75
      Rgb cc = mix8(col8_[k][rampOf(capD_[k])], Rgb{255, 255, 255}, 153);
      capP_[k] = dq(scaleSat(cc, 192));
    }
  }
  buildReflection(f);

  // HUD metinleri
  hudShow_ = hud.show;
  int n = 0;
  if (hud.label) for (; hud.label[n] && n < 23; ++n) hudLabel_[n] = hud.label[n];
  hudLabel_[n] = 0;
  int m = 0;
  if (hud.bpm > 0) {
    m += putUint(hudRight_ + m, hud.bpm);
    for (const char* s = " BPM  "; *s; ++s) hudRight_[m++] = *s;
  }
  m += putUint(hudRight_ + m, (uint32_t)(hud.fps10 + 5) / 10u);
  for (const char* s = " FPS"; *s; ++s) hudRight_[m++] = *s;
  hudRight_[m] = 0;
  hudRightX_ = (int16_t)(W - 3 - 6 * m);
}

void ScreenRenderer::hudRow(int y, uint16_t* row) const {
  if (!hudShow_ || y > 9) return;
  const uint32_t a = hudShow_, sa = (a * 200u) >> 8;
  const Rgb black{0, 0, 0}, white{240, 244, 255}, dim{170, 200, 230};
  textRow(row, y, hudLabel_, 4, 2, 1, black, sa);
  textRow(row, y, hudRight_, hudRightX_ + 1, 2, 1, black, sa);
  textRow(row, y, hudLabel_, 3, 1, 1, white, a);
  textRow(row, y, hudRight_, hudRightX_, 1, 1, dim, a);
}

void ScreenRenderer::renderRow(int y, uint16_t* row) const {
  if (!valid_) { for (int x = 0; x < W; ++x) row[x] = 0; return; }
  if (y < BAR_BASE_Y) {
    fillRow(row, bgP_[y], y);
    const int r = BASE_R - y;                        // taban satırından derinlik
    const int s = rampOf(r);
    for (int k = 0; k < NB; ++k) {
      uint16_t* p = row + 6 * k;
      const int full = full_[k];
      const int fr = frac_[k];
      if (r < full) {
        const int32_t x = (int32_t)topQ8_[k] - (r + 1) * 256;      // tepe yüzeyinden bu satırın altına mesafe (Q8)
        if (x < 512) {                                               // tepe parıltısı: son ~2 satır beyaza açılır
          const Rgb c = mix8(col8_[k][s], Rgb{255, 255, 255}, (uint32_t)((512 - x) * kTipMax) >> 9);
          putN<5>(p, dq(c), y);
          const Dp g = dq(mulAdd(bg8_[y], c, 64));
          p[5] = ((5 + y) & 1) ? g.b : g.a;
        } else {
          putN<5>(p, colP_[k][s], y);
          p[5] = ((5 + y) & 1) ? gapP_[k][s].b : gapP_[k][s].a;
        }
      } else if (r == full && fr) {                  // kısmi tepe satırı (alt piksel)
        const Rgb bg = bg8_[y];
        const Rgb tip = mix8(col8_[k][s], Rgb{255, 255, 255}, kTipMax);
        const Rgb c = mix8(bg, tip, (uint32_t)fr);
        putN<5>(p, dq(c), y);
        const Dp g = dq(mulAdd(bg, tip, (64u * (uint32_t)fr) >> 8));
        p[5] = ((5 + y) & 1) ? g.b : g.a;
      } else if (haloS_[k]) {                        // tepe üstü hale (6 satır)
        const int j = r - (full + (fr ? 1 : 0));
        if ((unsigned)j < 6u) {
          const uint32_t al = ((uint32_t)kHaloA[j] * haloS_[k]) >> 8;
          putN<6>(p, dq(mulAdd(bg8_[y], top8_[k], al)), y);
        }
      }
      if ((unsigned)(r - capD_[k]) < 2u && capD_[k] >= 0) putN<5>(p, capP_[k], y);
    }
  } else if (y == BAR_BASE_Y) {
    fillRow(row, lineP_, y);
  } else {
    fillRow(row, bgP_[y], y);
    const int r = y - (BAR_BASE_Y + 1);
    for (int k = 0; k < NB; ++k)
      if (r < rmax_[k]) putN<5>(row + 6 * k, refl_[k][r], y);
  }
  hudRow(y, row);
}

void ScreenRenderer::renderRows(int y0, int nrows, uint16_t* dst) const {
  for (int i = 0; i < nrows; ++i) {
    const int y = y0 + i;
    if ((unsigned)y >= (unsigned)H) { for (int x = 0; x < W; ++x) dst[i * W + x] = 0; continue; }
    renderRow(y, dst + i * W);
  }
}

// ---------------------------------------------------------------------------------------------
// Test desenleri
namespace {

const Rgb kBars[8] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                      {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0}};

void testColorBars(int y, uint16_t* row) {
  for (int x = 0; x < W; ++x) {
    Rgb c;
    if (y < 150) c = kBars[x / 30];
    else if (y < 190) { uint8_t g = (uint8_t)((x * 16 / W) * 17); c = Rgb{g, g, g}; }       // 16 kademe gri
    else {
      const uint32_t v = (uint32_t)x * 255u / (W - 1);
      const int band = (y - 190) / 17;                                                       // R, G, B
      c = band == 0 ? Rgb{(uint8_t)v, 0, 0} : (band == 1 ? Rgb{0, (uint8_t)v, 0} : Rgb{0, 0, (uint8_t)v});
    }
    row[x] = rgb565w(c);
  }
  // Etiket plakası + metin
  const char* t = "COLOR BARS";
  const int x0 = (W - 6 * 2 * slen(t)) / 2;
  if (y >= 11 && y < 37) for (int x = x0 - 6; x < x0 + 12 * slen(t) + 6; ++x) row[x] = rgb565w(Rgb{0, 0, 0});
  if (y >= 96 && y < 112) for (int x = 88; x < 152; ++x) row[x] = rgb565w(Rgb{0, 0, 0});
  textRow(row, y, t, x0 + 2, 16 + 2, 2, Rgb{0, 0, 0}, 255);
  textRow(row, y, t, x0, 16, 2, Rgb{255, 255, 255}, 255);
  const char* t2 = "240x240";
  textRow(row, y, t2, (W - 6 * slen(t2)) / 2, 100, 1, Rgb{255, 255, 255}, 255);
}

void testFrame(int y, uint16_t* row) {
  const Rgb white{255, 255, 255}, gray{110, 110, 110}, dgray{60, 60, 60};
  for (int x = 0; x < W; ++x) row[x] = 0;
  if (y == 119 || y == 120) for (int x = 0; x < W; ++x) row[x] = rgb565w(dgray);      // artı işareti
  row[119] = row[120] = rgb565w(dgray);
  if ((unsigned)y < 240u) { row[y] = rgb565w(gray); row[W - 1 - y] = rgb565w(gray); } // köşegenler
  if (y >= 1 && y <= 4) for (int x = 0; x < W; x += 20) row[x] = rgb565w(white);      // cetvel
  if (y % 20 == 0) for (int x = 1; x <= 4; ++x) row[x] = rgb565w(white);
  // köşe işaretleri (yönelim): sol-üst kırmızı, sağ-üst yeşil, sol-alt mavi, sağ-alt sarı
  const struct { int x, y; Rgb c; } cm[4] = {{3, 3, {255, 0, 0}}, {W - 15, 3, {0, 255, 0}},
                                              {3, H - 15, {0, 64, 255}}, {W - 15, H - 15, {255, 255, 0}}};
  for (int i = 0; i < 4; ++i)
    if (y >= cm[i].y && y < cm[i].y + 12) for (int x = cm[i].x; x < cm[i].x + 12; ++x) row[x] = rgb565w(cm[i].c);
  // etiketler
  textRow(row, y, "TOP", (W - 36) / 2, 18, 2, white, 255);
  textRow(row, y, "BOTTOM", (W - 72) / 2, 206, 2, white, 255);
  textRow(row, y, "L", 18, 112, 2, white, 255);
  textRow(row, y, "R", W - 18 - 12, 112, 2, white, 255);
  textRow(row, y, "240x240", (W - 42) / 2, 90, 1, white, 255);
  textRow(row, y, "FRAME TEST", (W - 60) / 2, 140, 1, Rgb{120, 220, 255}, 255);
  // 1 px beyaz çerçeve (4 kenar)
  if (y == 0 || y == H - 1) { for (int x = 0; x < W; ++x) row[x] = rgb565w(white); }
  else { row[0] = rgb565w(white); row[W - 1] = rgb565w(white); }
}

void testGradient(int y, uint16_t* row) {
  const int band = y / 48;
  for (int x = 0; x < W; ++x) {
    const uint32_t v = (uint32_t)x * 255u / (W - 1);
    Rgb c;
    switch (band) {
      case 0:  c = Rgb{(uint8_t)v, (uint8_t)v, (uint8_t)v}; break;
      case 1:  c = Rgb{(uint8_t)v, 0, 0}; break;
      case 2:  c = Rgb{0, (uint8_t)v, 0}; break;
      case 3:  c = Rgb{0, 0, (uint8_t)v}; break;
      default: c = hsv8((uint8_t)v, 255, 255); break;
    }
    row[x] = rgb565w(c);
  }
}

// TP_BOOT: ≈1 sn'lik açılış — soldan sağa gökkuşağı dalgası + "FFT VIZ" başlığı.
// tick ≈ 60 Hz kare sayacı. Çıkışta (tick ≥ 60) ekran sakin: yalnızca başlık.
void testBoot(uint32_t tick, int y, uint16_t* row) {
  const int t = tick > 1000 ? 1000 : (int)tick;
  Rgb bg{2, 4, (uint8_t)(14 + (y * 20) / H)};
  const uint16_t bgw = rgb565w(bg);
  for (int x = 0; x < W; ++x) row[x] = bgw;
  const int r = BASE_R - y;
  for (int k = 0; k < NB; ++k) {
    const int u = t - (k * 3) / 4;                         // çubuk yerel zamanı
    int32_t hq = 0;                                        // Q8 yükseklik
    if (u > 0 && u < 40) {
      const int x = u * 2 - 40;                            // -40..40
      hq = (int32_t)(PX_Q8 / 1) - (int32_t)((uint32_t)(x * x) * PX_Q8 / 1600u);   // parabol hump
      hq = hq * 3 / 4;
    }
    if (hq <= 0 || r < 0) continue;
    const int full = hq >> 8;
    if (r > full) continue;
    const uint32_t cov = r < full ? 255u : (uint32_t)(hq & 255);
    if (!cov) continue;
    Rgb c = vivid(hsv8((uint8_t)(k * 5 + t * 3), 255, 255));
    c = scaleSat(c, 140u + (115u * (uint32_t)(r < 168 ? r : 168)) / 168u);
    c = mix8(bg, c, cov);
    const uint16_t w = rgb565w(c);
    for (int i = 0; i < 5; ++i) row[6 * k + i] = w;
  }
  if (y == BAR_BASE_Y) for (int x = 0; x < W; ++x) row[x] = rgb565w(Rgb{90, 110, 160});
  // başlık plakası: çubukların üstünde okunur olsun
  if (y >= 90 && y < 130)
    for (int x = 12; x < W - 12; ++x) row[x] = blend565(row[x], Rgb{0, 0, 8}, 170);
  if (y >= 134 && y < 160)
    for (int x = 8; x < W - 8; ++x) row[x] = blend565(row[x], Rgb{0, 0, 8}, 150);
  // başlık
  const char* title = "FFT VIZ";
  const uint32_t al = t * 10 > 255 ? 255u : (uint32_t)t * 10u;
  for (int i = 0; title[i]; ++i) {
    const int x0 = 36 + i * 24;
    const char one[2] = {title[i], 0};
    textRow(row, y, one, x0 + 3, 96 + 3, 4, Rgb{0, 0, 0}, al * 3 / 4);
    textRow(row, y, one, x0, 96, 4, vivid(hsv8((uint8_t)(i * 28 + t * 2), 140, 255)), al);
  }
  const char* sub = "ESP32 REALTIME FFT";
  textRow(row, y, sub, (W - 12 * slen(sub)) / 2, 140, 2, Rgb{200, 215, 240}, al);
}

}  // namespace

void ScreenRenderer::renderTest(uint8_t kind, uint32_t tick, int y0, int nrows, uint16_t* dst) const {
  for (int i = 0; i < nrows; ++i) {
    const int y = y0 + i;
    uint16_t* row = dst + i * W;
    if ((unsigned)y >= (unsigned)H) { for (int x = 0; x < W; ++x) row[x] = 0; continue; }
    switch (kind) {
      case TP_COLORBARS: testColorBars(y, row); break;
      case TP_FRAME:     testFrame(y, row); break;
      case TP_GRADIENT:  testGradient(y, row); break;
      default:           testBoot(tick, y, row); break;
    }
  }
}

void ScreenRenderer::renderText(const char* const* lines, int n, int y0, int nrows, uint16_t* dst) const {
  for (int i = 0; i < nrows; ++i) {
    const int y = y0 + i;
    uint16_t* row = dst + i * W;
    const uint16_t bg = rgb565w(Rgb{6, 9, 22});
    for (int x = 0; x < W; ++x) row[x] = bg;
    if ((unsigned)y >= (unsigned)H) continue;
    const int li = (y - 4) / 18;                    // satır yüksekliği 18 px
    if (y < 4 || li >= n || !lines[li]) continue;
    const char* s = lines[li];
    Rgb col{225, 232, 245};
    if (contains(s, "FAIL")) col = Rgb{255, 110, 100};
    else if (contains(s, "PASS")) col = Rgb{110, 235, 140};
    textRow(row, y, s, 4, 4 + li * 18 + 2, 2, col, 255);
  }
}

}  // namespace fv

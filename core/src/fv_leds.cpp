// fv_leds.cpp — LED örüntü motoru; uzunluk `leds` parametresiyle 1..LED_MAX.
// Hat: mod örüntüsü → bloom → idle karışımı → mod crossfade → gamma(16 bit) → parlaklık
//      → otomatik pozlama → zamansal dither → güç limiter'ı. Tümü tam sayı, deterministik.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_leds.h"
#include "fv_color.h"
#include "fv_math.h"
#include "fv_vis_tables.h"

namespace fv {

const char* led_mode_name(uint8_t m) {
  static const char* const n[LM_COUNT] = {"AURORA", "SPARKLE", "MIRROR", "SPECTRUM", "BASS",
                                          "WATERFALL", "FIRE", "COMET", "DEMO"};
  return m < LM_COUNT ? n[m] : "?";
}

const char* led_palette_name(uint8_t pal) {
  static const char* const n[LED_PAL_N] = {"GOKKUSAGI", "SICAK", "OKYANUS", "ORMAN", "MOR", "ALTIN"};
  return pal < LED_PAL_N ? n[pal] : "?";
}

// Fisher–Yates, xorshift32 seed 0xC0DE (tools/gen_vis_tables.cpp ile aynı; n=120 → vtab::perm120).
void led_perm(uint16_t* out, int n) {
  for (int i = 0; i < n; ++i) out[i] = (uint16_t)i;
  uint32_t s = 0xC0DE;
  for (int i = n - 1; i > 0; --i) {
    const int j = (int)(xorshift32(s) % (uint32_t)(i + 1));
    const uint16_t t = out[i]; out[i] = out[j]; out[j] = t;
  }
}

PowerModel power_model(const Params& p) {
  return PowerModel{(uint32_t)vclamp<int32_t>(p.get(P_LED_MA), 1, 100), (uint32_t)vclamp<int32_t>(p.get(P_LED_IDLE), 0, 5000)};
}

uint32_t led_estimate_ma(const Rgb* px, int n, const PowerModel& m) {
  uint32_t s = 0;
  for (int i = 0; i < n; ++i) s += (uint32_t)px[i].r + px[i].g + px[i].b;
  return (uint32_t)n * m.idleUa / 1000u + s * m.ledMa / 765u;
}

uint32_t led_estimate_ma(const Rgb* px, int n) { return led_estimate_ma(px, n, PowerModel{60, 1000}); }

uint32_t led_sum_allowed(int n, int32_t budgetMa, const PowerModel& m) {
  const uint32_t base = (uint32_t)n * m.idleUa / 1000u;
  if (budgetMa <= 0 || (uint32_t)budgetMa <= base) return 0u;
  return ((uint32_t)budgetMa - base) * 765u / m.ledMa;
}

int32_t led_budget_ma(const Params& p, BudgetSrc* src) {
  int32_t b = p.get(P_MA_BUDGET);
  BudgetSrc s = BS_MA;
  const int32_t psu = p.get(P_PSU_MA);
  if (psu > 0) {
    const int32_t cap = (int32_t)((uint32_t)psu * (uint32_t)vclamp<int32_t>(p.get(P_PSU_PCT), 1, 100) / 100u);
    if (cap < b) { b = cap; s = BS_PSU; }
  }
  if (p.usbPower && p.get(P_MA_USB) < b) { b = p.get(P_MA_USB); s = BS_USB; }
  if (src) *src = s;
  return b;
}

namespace {

constexpr int kMaxBandQ8 = (NUM_BANDS - 1) * 256;  // 9984
// Palet tablosu: ton h → ofs + h·span/256. Palet 0 özdeşlik (eski görünüm). Seçim `pal`, otomatik geçiş `pal_cyc`.
struct Pal { int16_t off; int16_t span; };
constexpr Pal kPal[LED_PAL_N] = {{0, 256}, {224, 90}, {100, 80}, {48, 72}, {160, 90}, {8, 48}};
// Otomatik pozlama (led_ae): kazanç Q16, 1×..24×; yukarı ~0,9 s, aşağı ~0,2 s zaman sabiti (Q12 oran).
constexpr uint32_t kAeMin = 1u << 16;
constexpr uint32_t kAeMax = 24u << 16;
constexpr int32_t kAeUp = 80;                      // ≈ 1/51 kare
constexpr int32_t kAeIdle = 27;                    // etkin değilken 1×'e sönüm (≈ 1/150 kare)
constexpr int32_t kAeDown = 340;                   // ≈ 1/12 kare
constexpr int kAeMinBands = 3;                     // pozlama için eşiği aşan en az bant
constexpr uint8_t kAeHold = 15;                    // ... ve en az bu kadar ardışık kare (açılış/gürültü sıçramalarını eler)

// Q8 koordinatla 40 bant üzerinde doğrusal interpolasyon.
inline uint32_t bandAt(const u16n* a, uint32_t x8) {
  uint32_t i = x8 >> 8;
  int32_t fr = (int32_t)(x8 & 255);
  int32_t lo = a[i];
  int32_t hi = a[i < NUM_BANDS - 1 ? i + 1 : NUM_BANDS - 1];
  return (uint32_t)(lo + (((hi - lo) * fr) >> 8));
}

// LED konumu (0..n-1) → bant ekseni Q8: (2i+1)·40/(2n) − ½ bant, [0, 39] kırpılı. n = 120 → (2i+1)·128/3 − 128.
inline int32_t bandX(int i, int n) {
  return vclamp<int32_t>(((2 * i + 1) * 5120) / n - 128, 0, kMaxBandQ8);
}

// RGB'yi renk çemberinde döndürür (FIRE paleti ton tabanlı değil). ang: 0..255 = tam tur. Yalnız ang != 0 iken çağrılır.
void rotateHue(Rgb* px, int n, uint32_t ang) {
  const int32_t c = sin16((uint16_t)((ang << 8) + 16384)), s = sin16((uint16_t)(ang << 8));  // Q15
  // Luma korumalı ton döndürme matrisi (Q15)
  const int32_t m[3][3] = {
      {6980 + ((c * 25789) >> 15) - ((s * 6980) >> 15), 23429 - ((c * 23429) >> 15) - ((s * 23429) >> 15), 2359 - ((c * 2359) >> 15) + ((s * 30409) >> 15)},
      {6980 - ((c * 6980) >> 15) + ((s * 4686) >> 15), 23429 + ((c * 9339) >> 15) + ((s * 4588) >> 15), 2359 - ((c * 2359) >> 15) - ((s * 9273) >> 15)},
      {6980 - ((c * 6980) >> 15) - ((s * 25789) >> 15), 23429 - ((c * 23429) >> 15) + ((s * 23429) >> 15), 2359 + ((c * 30409) >> 15) + ((s * 2359) >> 15)}};
  for (int i = 0; i < n; ++i) {
    const int32_t r = px[i].r, g = px[i].g, b = px[i].b;
    px[i].r = satu8((m[0][0] * r + m[0][1] * g + m[0][2] * b) >> 15);
    px[i].g = satu8((m[1][0] * r + m[1][1] * g + m[1][2] * b) >> 15);
    px[i].b = satu8((m[2][0] * r + m[2][1] * g + m[2][2] * b) >> 15);
  }
}

// T(a, th) = sat((a−th)/(1−th)), u16n; bölme yerine kare başına bir tersi.
struct Thresh {
  uint32_t th16, rec;
  explicit Thresh(int32_t thQ8) {
    uint32_t t = (uint32_t)vclamp<int32_t>(thQ8, 0, 255) * 257u;
    if (t > 61000u) t = 61000u;
    th16 = t;
    rec = (65535u << 12) / (65535u - t);
  }
  uint32_t operator()(uint32_t a) const {
    if (a <= th16) return 0;
    uint32_t e = ((a - th16) * rec) >> 12;
    return e > 65535u ? 65535u : e;
  }
};

// cp(e) LUT'unun e16 üzerinde doğrusal interpolasyonu (8 bit sonuç, Q8 ince).
inline uint32_t cpLerp(uint32_t e16) {
  uint32_t i = e16 >> 8, fr = e16 & 255;
  uint32_t a = vtab::cpBright[i], b = vtab::cpBright[i < 255 ? i + 1 : 255];
  return (a * (256 - fr) + b * fr) >> 8;
}

inline Rgb addSat(Rgb a, Rgb b) { return rgb_add(a, b); }
inline uint8_t u8(uint32_t x) { return x > 255 ? 255 : (uint8_t)x; }

}  // namespace

void LedEngine::reset(uint32_t seed, int leds) {
  rng_ = seed ? seed : 1;
  t_ = 0;
  ma_ = 0; demand_ = 0; budget_ = 0; lim_ = 255; limL_ = 256; aeG_ = kAeMin; aeM_ = 0; aeAct_ = 0;
  mode_ = 0; reqMode_ = 255; demoSub_ = 0; demoBeats_ = demoFrames_ = 0;
  idleN_ = 0; xfade_ = 0; xfadeN_ = 1;
  be_ = 0; hueRot_ = 0; hueKick_ = 0; kickTgt_ = 0; drift_ = 0; wfPh_ = 0; cometDir_ = 0;
  idlePh_ = 0; palT_ = 0; palOff_ = 0; palSpan_ = 256; palOffB_ = 0; palSpanB_ = 256; palW_ = 0;
  cenS_ = 100u << 8; cenHue_ = 100;
  layout(leds);
}

// Uzunluğa bağlı tabloları kurar ve LED başına tüm durumu sıfırlar (reset ve `leds` değişimi). Genel durum
// (rng, mod, pozlama kazancı, zarflar) korunur. n = 120'de eski reset() ile birebir aynı sonucu verir.
void LedEngine::layout(int n) {
  n = vclamp<int>(n, 1, LED_MAX);
  n_ = n;
  half_ = (n + 1) / 2;
  led_perm(bl_, n);  // bl_ (u16) geçici: karışık sıra; bloom her kare yeniden yazar
  for (int i = 0; i < LED_MAX; ++i) {
    pattern_[i] = out_[i] = cur_[i] = idle_[i] = from_[i] = trail_[i] = Rgb{0, 0, 0};
    spark_[i] = 0;
  }
  for (int i = 0; i < n; ++i) {
    // Her bant eşit LED payı alsın: LED merkezi (i+½)·40/n bant, bant merkezi tam sayı → x = (2i+1)·5120/n − 128 (Q8),
    // [0, 39] kırpılır; ton = 200·x/39
    posX_[0][i] = (uint16_t)bandX(i, n);  posH_[0][i] = (uint16_t)(posX_[0][i] * 200u / 39u);
    posX_[1][i] = (uint16_t)bandX(bl_[i], n); posH_[1][i] = (uint16_t)(posX_[1][i] * 200u / 39u);
    uint32_t d = (uint32_t)iabs32(2 * i - (n - 1));  // 1..n-1, merkez 1 (n çiftse)
    // Yarım şeritte n/2 LED → 40 bant: merkezden uzaklık d/2 LED → x = d·40/n − ½ bant (n = 120: d/3 − ½)
    int32_t xm = (int32_t)(d * 10240u / (uint32_t)n) - 128;
    mirX_[i] = (uint16_t)vclamp<int32_t>(xm, 0, kMaxBandQ8);
    mirH_[i] = (uint8_t)(n > 1 ? 96u * d / (uint32_t)(n - 1) : 0u);
  }
  for (int i = 0; i < LED_MAX * 3; ++i) { resid_[i] = (uint8_t)(i * 159u); bl_[i] = 0; }  // dither: kareler arası dağınık başlangıç
  for (int i = 0; i < LED_MAX / 2 + 2; ++i) heat_[0][i] = heat_[1][i] = 0;
  for (int i = 0; i < LED_MAX / 2 + 1; ++i) wf_[i] = Rgb{0, 0, 0};
  for (int i = 0; i < 6; ++i) comets_[i] = Comet{0, 0, 0, 0};
  xfade_ = 0;
  aeM_ = 0;
}

// Mod değişimi: görünen son örüntüyü crossfade kaynağı yap, durumlu modları sıfırla.
void LedEngine::switchMode(uint8_t m, int frames) {
  for (int i = 0; i < n_; ++i) from_[i] = pattern_[i];
  xfadeN_ = (uint8_t)frames; xfade_ = (uint8_t)frames;
  mode_ = m;
  for (int i = 0; i < n_; ++i) { trail_[i] = Rgb{0, 0, 0}; spark_[i] = 0; }
  for (int i = 0; i < half_; ++i) { heat_[0][i] = heat_[1][i] = 0; wf_[i] = Rgb{0, 0, 0}; }
  for (int i = 0; i < 6; ++i) comets_[i].on = 0;
  wfPh_ = 0;
}

// bloom(k): ileri f[i]=x[i]+k·f[i−1], geri b[i]=x[i]+k·b[i+1]; sonuç sat(f+b−x). Çekirdek k^|d|.
void LedEngine::bloom(uint32_t k) {
  if (k == 0) return;
  uint32_t pr = 0, pg = 0, pb = 0;
  for (int i = 0; i < n_; ++i) {
    pr = cur_[i].r + ((pr * k) >> 8);
    pg = cur_[i].g + ((pg * k) >> 8);
    pb = cur_[i].b + ((pb * k) >> 8);
    bl_[i * 3] = (uint16_t)pr; bl_[i * 3 + 1] = (uint16_t)pg; bl_[i * 3 + 2] = (uint16_t)pb;
  }
  uint32_t qr = 0, qg = 0, qb = 0;
  for (int i = n_ - 1; i >= 0; --i) {
    uint32_t xr = cur_[i].r, xg = cur_[i].g, xb = cur_[i].b;
    qr = xr + ((qr * k) >> 8);
    qg = xg + ((qg * k) >> 8);
    qb = xb + ((qb * k) >> 8);
    cur_[i].r = u8(bl_[i * 3] + qr - xr);
    cur_[i].g = u8(bl_[i * 3 + 1] + qg - xg);
    cur_[i].b = u8(bl_[i * 3 + 2] + qb - xb);
  }
}

// --- 0/1 AURORA & SPARKLE (CodePen: ton = frekans, parlaklık = eşiklenmiş bant, boyut² yasası) ---
// Köz: V = cp(e) + ember·(1−e); e=0 iken bile yanık kalır. Gamma 2.2 düşük V'yi yok ettiği için
// ember tabanı biraz yüksek tutulur (bloom ~2-3× kazanır). Köz değerleri bri 160→255 geçişinde sessizlik
// çıkışı artmasın diye ×0,75 azaltıldı (bkz. genIdle). Sıcak çekirdek: e>0.85 → S 255→128.
void LedEngine::genAurora(const Features& f, const Params& p, bool sparkle) {
  const Thresh T(p.get(P_LED_THRESH));
  const int pi = sparkle ? 1 : 0;
  const uint32_t emb = (uint32_t)p.get(P_EMBER);
  for (int i = 0; i < n_; ++i) {
    uint32_t e = T(bandAt(f.led, posX_[pi][i]));
    uint32_t v = cpLerp(e);
    uint32_t ember = sparkle ? emb * 5u / 9u + (sin8((uint8_t)(3 * t_ + 73 * i)) >> 5) : emb;  // 5..12 / 9 (vars.)
    v += (ember * (65535u - e)) >> 16;
    uint8_t s = 255;
    if (e > 55705u) s = (uint8_t)(255 - (((e - 55705u) * 847u) >> 16));          // → ~128
    uint8_t h = (uint8_t)(((posH_[pi][i] + hueRot_) >> 8) & 255);
    cur_[i] = pc(h, s, u8(v), true);
  }
  uint32_t k = ((sparkle ? 96u : 128u) * (uint32_t)p.get(P_BLOOM)) >> 7;
  bloom(k > 230 ? 230 : k);
}

// --- 2 MIRROR: bas ortada, tizler uçlarda; ton centroid'e bağlı ---
void LedEngine::genMirror(const Features& f, const Params& p) {
  const uint32_t emb = (uint32_t)p.get(P_EMBER) * 5u / 9u;
  const uint32_t dl = (uint32_t)(n_ / 5 > 0 ? n_ / 5 : 1);   // merkez beat parıltısı yarı genişliği (n = 120: 24)
  for (int i = 0; i < n_; ++i) {
    uint32_t b = bandAt(f.bar, mirX_[i]);
    uint32_t v = cpLerp(b) + ((emb * (65535u - b)) >> 16);
    uint32_t d = (uint32_t)iabs32(2 * i - (n_ - 1));
    if (d < dl) v += ((uint32_t)f.beatEnv * ((dl - d) * 72u / dl)) >> 16;
    uint8_t s = 255;
    if (b > 55705u) s = (uint8_t)(255 - (((b - 55705u) * 847u) >> 16));
    cur_[i] = pc((uint8_t)(cenHue_ + mirH_[i]), s, u8(v), true);
  }
  uint32_t k = (128u * (uint32_t)p.get(P_BLOOM)) >> 7;
  bloom(k > 230 ? 230 : k);
}

// --- 3 SPECTRUM: 8 segment (n=120 → 15 LED); tek numaralı segmentler ters yönde ---
void LedEngine::genSpectrum(const Features& f) {
  for (int i = 0; i < n_; ++i) cur_[i] = Rgb{0, 0, 0};
  for (int s = 0; s < 8; ++s) {
    const int a = s * n_ / 8, L = (s + 1) * n_ / 8 - a;   // segment [a, a+L)
    if (L <= 0) continue;
    uint32_t h = 0, cap = 0;
    for (int j = 0; j < 5; ++j) {
      h = vmax<uint32_t>(h, f.bar[s * 5 + j]);
      cap = vmax<uint32_t>(cap, f.cap[s * 5 + j]);
    }
    const uint32_t fill = (uint32_t)L * h;       // Q16 LED sayısı
    const uint32_t full = fill >> 16, part = fill & 0xFFFF;
    uint32_t capK = ((uint32_t)L * cap) >> 16;
    if (capK > (uint32_t)(L - 1)) capK = (uint32_t)(L - 1);
    const uint32_t den = L > 1 ? (uint32_t)(L - 1) : 1u;   // L = 15: ton 2k, parlaklık 11k
    for (int k = 0; k < L; ++k) {
      const int idx = a + ((s & 1) ? L - 1 - k : k);
      Rgb c{0, 0, 0};
      const uint8_t hue = (uint8_t)(32 * s + 28u * (uint32_t)k / den);
      const uint8_t val = (uint8_t)(90 + 154u * (uint32_t)k / den);
      if ((uint32_t)k < full) c = pc(hue, 255, val, false);
      else if ((uint32_t)k == full && part) c = pc(hue, 255, (uint8_t)(((uint32_t)val * part) >> 16), false);
      if ((uint32_t)k == capK && capK >= full && cap > 4096u) c = addSat(c, Rgb{180, 180, 180});
      cur_[idx] = c;
    }
  }
}

// --- 4 BASS PULSE --- (yarıçap ve kenar yumuşaklığı n/120 ile ölçeklenir)
void LedEngine::genBass(const Features& f) {
  const uint32_t be = be_;                        // Q16
  const uint32_t rq8 = (2048u + ((52u * 256u * be) >> 16)) * (uint32_t)n_ / 120u;   // r = (8 + 52·be)·n/120 LED (Q8)
  const uint32_t soft = vmax<uint32_t>(2048u * (uint32_t)n_ / 120u, 256u);           // kenar yumuşaklığı (n=120: 8 LED)
  for (int i = 0; i < n_; ++i) {
    // |i − (n−1)/2| Q8 = |2i−(n−1)|·128
    uint32_t dq8 = (uint32_t)iabs32(2 * i - (n_ - 1)) * 128u;
    int32_t t = (int32_t)(rq8 - dq8);
    uint32_t prof = t <= 0 ? 0u : ((uint32_t)t >= soft ? 65535u : (uint32_t)t * 65536u / soft);  // sat(.../8)
    uint32_t v = ((((be * prof) >> 16) * 255u) >> 16);
    if (v < 10) v = 10;
    Rgb c = pc(cenHue_, 240, u8(v), false);
    uint32_t w = ((((uint32_t)f.beatEnv * prof) >> 16) * 102u) >> 16;  // 0.4·beatEnv beyaz
    cur_[i] = addSat(c, Rgb{u8(w), u8(w), u8(w)});
  }
}

// --- 5 WATERFALL: merkezden dışa kayan geçmiş; kesirli lerp ile akıcı ---
void LedEngine::genWaterfall(const Features& f, const Params& p) {
  // loud tipik olarak 1'e ulaşmaz: ×1.8 genişlet; eğri (l + l²)/2 → V ≈ 255·loud² ama sönük müzikte de görünür.
  uint32_t l = (uint32_t)f.loud * 9u / 5u;
  if (l > 65535u) l = 65535u;
  uint32_t v = ((l + ((l * l) >> 16)) >> 1) * 255u >> 16;
  Rgb s = pc(cenHue_ + (hueRot_ >> 9), 255, u8(v), true);
  s = rgb_lerp(s, Rgb{255, 255, 255}, (uint8_t)(f.beatEnv >> 9));  // beat'te en çok %50 beyaz
  wfPh_ += (uint16_t)p.get(P_WF_SPD);             // vars. 192 = 0.75 LED/kare (Q8)
  const uint8_t dec = (uint8_t)p.get(P_WF_DEC);   // vars. 247 = ×0.97
  while (wfPh_ >= 256) {
    wfPh_ -= 256;
    for (int d = half_ - 1; d >= 1; --d) wf_[d] = rgb_scale(wf_[d - 1], dec);
    wf_[0] = s;
  }
  const uint8_t fr = (uint8_t)wfPh_;
  for (int i = 0; i < n_; ++i) {
    const int d = iabs32(2 * i - (n_ - 1)) / 2;   // merkezden uzaklık (n çift: d = (|2i−(n−1)|−1)/2)
    cur_[i] = rgb_lerp(wf_[d], d ? wf_[d - 1] : s, fr);
  }
}

// --- 6 FIRE: iki uçtan merkeze Fire2012 ---
void LedEngine::genFire(const Features& f, const Params& p) {
  const uint32_t loud8 = vmin<uint32_t>(((uint32_t)f.loud * 5u / 4u) >> 8, 255u);
  const uint32_t cooling = (uint32_t)p.get(P_FIRE_COOL) + ((90u * (255u - loud8)) >> 8);   // vars. 75 + 90(1−loud): alev merkeze kadar dolmasın diye kaydırıldı
  const uint32_t coolMax = (cooling * 10u) / (uint32_t)half_ + 2u;
  const uint32_t sparkP = ((uint32_t)p.get(P_FIRE_SPARK) + ((160u * be_) >> 16));
  for (int h = 0; h < 2; ++h) {
    uint8_t* heat = heat_[h];
    for (int i = 0; i < half_; ++i) {
      uint32_t c = xorshift32(rng_) % (coolMax + 1);
      heat[i] = (uint8_t)(heat[i] > c ? heat[i] - c : 0);
    }
    for (int k = half_ - 1; k >= 2; --k)
      heat[k] = (uint8_t)((heat[k - 1] + 2u * heat[k - 2]) / 3u);
    if ((xorshift32(rng_) & 255u) < sparkP) {
      uint32_t y = xorshift32(rng_) % 3u;
      heat[y] = u8((uint32_t)heat[y] + 160u + xorshift32(rng_) % 95u);
    }
    if (f.beat) { heat[0] = 255; heat[1] = 255; heat[2] = u8(200u + f.beatStrength / 5u); }
    for (int i = 0; i < half_; ++i) {
      const uint8_t* pc = vtab::heatPal[heat[i]];
      cur_[h ? n_ - 1 - i : i] = Rgb{pc[0], pc[1], pc[2]};
    }
  }
  // palet/ton kayması: ateş paleti RGB olduğundan döndürülür; çember ↔ dar palet geçişinde iki dönüşün renkleri karıştırılır
  if (palW_) {
    for (int i = 0; i < n_; ++i) alt_[i] = cur_[i];
    if (palOffB_ & 255u) rotateHue(alt_, n_, palOffB_ & 255u);
    if (palOff_ & 255u) rotateHue(cur_, n_, palOff_ & 255u);
    for (int i = 0; i < n_; ++i) cur_[i] = rgb_lerp(cur_[i], alt_[i], palW_);
  } else if (palOff_ & 255u) {
    rotateHue(cur_, n_, palOff_ & 255u);
  }
}

// --- 7 COMET: soluk AURORA zemini, beat'te fırlayan kuyruklu yıldızlar, tiz onset kıvılcımı ---
void LedEngine::genComet(const Features& f, const Params& p) {
  genAurora(f, p, false);
  const uint8_t trl = (uint8_t)p.get(P_COMET_TRL);      // vars. 204 = ×0.80
  for (int i = 0; i < n_; ++i) cur_[i] = rgb_scale(cur_[i], 38);  // ×0.15
  for (int i = 0; i < n_; ++i) trail_[i] = rgb_scale(trail_[i], trl);
  if (f.beat) {
    for (int c = 0; c < 6; ++c) {
      if (comets_[c].on) continue;
      const uint32_t v = ((256u + (640u * f.beatStrength) / 255u) * (uint32_t)p.get(P_COMET_SPD)) >> 8;  // 1 + 2.5·strength LED/kare × hız
      comets_[c].pos = (int32_t)((uint32_t)(n_ - 1) << 7);   // şerit merkezi (Q8)
      comets_[c].vel = (int16_t)(cometDir_ ? (int32_t)v : -(int32_t)v);
      comets_[c].hue = cenHue_;
      comets_[c].on = 1;
      cometDir_ ^= 1;
      break;
    }
  }
  for (int c = 0; c < 6; ++c) {
    Comet& m = comets_[c];
    if (!m.on) continue;
    const int32_t vel = m.vel, a = iabs32(vel);
    const int steps = (int)((a + 255) >> 8);            // kare başına alt adım (hız çarpanı 4× iken en çok ~14)
    Rgb col = pc(m.hue, 255, 255, false);
    Rgb head = rgb_lerp(col, Rgb{255, 255, 255}, 110);
    for (int j = 1; j <= steps; ++j) {
      const int32_t pos = m.pos + (vel * j) / steps;
      if (pos < 0 || pos > ((n_ - 1) << 8)) continue;
      const uint8_t age = (uint8_t)(255 - ((255 - trl) * (steps - j)) / steps);  // kare içi sönüm
      const int i0 = pos >> 8; const uint8_t fr = (uint8_t)(pos & 255);
      Rgb h = rgb_scale(head, age);
      trail_[i0] = addSat(trail_[i0], rgb_scale(h, (uint8_t)(255 - fr)));
      if (i0 + 1 < n_) trail_[i0 + 1] = addSat(trail_[i0 + 1], rgb_scale(h, fr));
    }
    m.pos += vel;
    if (m.pos < -(2 << 8) || m.pos > ((n_ + 1) << 8)) m.on = 0;
  }
  if (f.onset) spark_[xorshift32(rng_) % (uint32_t)n_] = 255;
  for (int i = 0; i < n_; ++i) {
    uint8_t w = spark_[i];
    spark_[i] = scale8(w, 192);                         // ×0.75
    Rgb c = addSat(cur_[i], trail_[i]);
    cur_[i] = addSat(c, Rgb{w, w, w});
  }
}

// --- IDLE: sessizlikte yavaş nefes alan gökkuşağı (V 22..51; gamma sonrası loş ama görünür) ---
// bri varsayılanı 160 → 255 olunca çıkış değişmesin diye V ×0,81 (= (161/256)^(1/2,2)).
void LedEngine::genIdle(const Params& p) {
  const uint8_t ph = (uint8_t)(idlePh_ >> 8);                         // vars.: t/2
  const uint32_t v = vmin<uint32_t>((uint32_t)p.get(P_IDLE_BRI) + (29u * sin8(ph)) / 255u, 255u);
  for (int i = 0; i < n_; ++i)
    idle_[i] = pc((uint32_t)((256 * i) / n_) + ph, 255, (uint8_t)v, true);
}

// Palet: seçili palet (pal) ya da pal_cyc > 0 iken paletler arasında kayarak geçiş + sabit ton ofseti (hue_ofs).
// Her periyodun son bölümünde (çeyrek; kısa periyotlarda en az 4 s, en çok yarısı) bir sonrakine kayılır: ton ofseti
// ÇEMBERDE EN KISA yoldan, genişlik doğrusal; ağırlık smoothstep (uçlarda yavaş, ortada hızlı değil: ani ışık sıçraması yok).
// smooth = 1: palet çember (span 256: GOKKUSAGI) ile dar palet arasındaki geçişte ton ara değerlemesi YAPILMAZ (çemberden
// palete sürekli bir ton eşlemesi yoktur; ara değerde şerit boyunca bir dikiş doğar): iki paletin renkleri smoothstep ile
// karıştırılır (palW_). İki dar palet arasında ton ara değerlemesi kalır (ikisi de katlanmış, sürekli).
void LedEngine::updatePalette(const Params& p) {
  const int pal = vclamp<int32_t>(p.get(P_PAL), 0, LED_PAL_N - 1);
  const uint32_t cyc = (uint32_t)p.get(P_PAL_CYC);
  int32_t off = kPal[pal].off, span = kPal[pal].span, offB = 0, spanB = 256;
  palW_ = 0;
  if (cyc == 0) {
    palT_ = 0;
  } else {
    const uint32_t per = cyc * (uint32_t)FRAME_HZ, total = per * LED_PAL_N;
    if (palT_ >= total) palT_ = 0;
    const uint32_t idx = palT_ / per, ph = palT_ % per;
    const uint32_t slide = vmax<uint32_t>(vmax<uint32_t>(per / 4u, vmin<uint32_t>(per / 2u, 4u * (uint32_t)FRAME_HZ)), 1u), hold = per - slide;
    const Pal& a = kPal[(pal + idx) % LED_PAL_N];
    off = a.off; span = a.span;
    if (ph >= hold) {
      const Pal& b = kPal[(pal + idx + 1) % LED_PAL_N];
      const int32_t w0 = (int32_t)((ph - hold + 1u) * 256u / slide);                    // 0..256 doğrusal
      const int32_t w = (int32_t)(((uint32_t)w0 * (uint32_t)w0 * (uint32_t)(768 - 2 * w0)) >> 16);   // smoothstep Q8
      if (sm_ && ((a.span >= 256) != (b.span >= 256))) {
        offB = b.off; spanB = b.span;                                                     // renk çapraz geçişi
        palW_ = (uint8_t)(w > 255 ? 255 : w);
      } else {
        const int32_t dOff = (((int32_t)b.off - a.off + 128) & 255) - 128;              // en kısa yön, -128..127
        off += (dOff * w) >> 8;
        span += (((int32_t)b.span - a.span) * w) >> 8;
      }
    }
    ++palT_;
  }
  palOff_ = (uint32_t)(off + p.get(P_HUE_OFS)) & 255u;
  palSpan_ = (uint32_t)span;
  palOffB_ = (uint32_t)(offB + p.get(P_HUE_OFS)) & 255u;
  palSpanB_ = (uint32_t)spanB;
}

void LedEngine::step(const Features& f, const Params& p) {
  ++t_;
  sm_ = p.get(P_SMOOTH) != 0;
  idlePh_ += (uint32_t)p.get(P_IDLE_SPD);
  {
    const int n = vclamp<int32_t>(p.get(P_LEDS), 1, LED_MAX);
    if (n != n_) layout(n);   // uzunluk değişti: tablolar yeniden, LED durumu sıfır (genel durum korunur)
  }
  updatePalette(p);
  // Spektral merkez tonu (BASS/MIRROR/WATERFALL/COMET rengi): smooth = 1 iken kare başına 1/4 oranında süzülür
  // (analizör EMA'sı sonrası kalan 2-9 tonluk kare kare oynama tüm şeridi aynı anda titretiyordu).
  {
    const int32_t tgt = (int32_t)f.centroidHue << 8;
    if (!sm_ || t_ == 1) cenS_ = (uint32_t)tgt;
    else cenS_ = (uint32_t)((int32_t)cenS_ + ((tgt - (int32_t)cenS_) >> 2));
    cenHue_ = sm_ ? (uint8_t)((cenS_ + 128u) >> 8) : f.centroidHue;
  }
  // --- mod seçimi (DEMO dahil) ---
  uint8_t req = (uint8_t)vclamp<int32_t>(p.get(P_LED_MODE), 0, LM_COUNT - 1);
  const bool first = reqMode_ == 255;
  if (req != reqMode_) {
    reqMode_ = req;
    demoSub_ = 0; demoBeats_ = demoFrames_ = 0;
    if (first) mode_ = (req == LM_DEMO) ? 0 : req;
    else switchMode(req == LM_DEMO ? 0 : req, p.get(P_FADE));
  }
  if (req == LM_DEMO) {
    ++demoFrames_;
    if (f.beat) ++demoBeats_;
    if (demoBeats_ >= (uint32_t)p.get(P_DEMO_BEATS) || demoFrames_ >= (uint32_t)p.get(P_DEMO_SEC) * FRAME_HZ) {
      demoSub_ = (uint8_t)((demoSub_ + 1) % LM_DEMO);
      demoBeats_ = demoFrames_ = 0;
      switchMode(demoSub_, p.get(P_DEMO_FADE));
    }
  }

  // --- ortak durum: bas zarfı (attack 0.7 / release 0.10), ton dönüşü ---
  {
    const uint32_t bs = f.bass;
    be_ = bs > be_ ? be_ + (((bs - be_) * (uint32_t)p.get(P_BASS_ATK)) >> 8) : be_ - (((be_ - bs) * (uint32_t)p.get(P_BASS_REL)) >> 8);
    // Ton dönüşü: yavaş sürekli akış (hue_spd, vars. ~1.4 ton/s) + beat vurgusu: +hue_kick ton, kare başına 1 ton geri kayar.
    // smooth = 1: tekme tek karede değil ~3 karede yükselir (kare başına en çok hue_kick/3 ton; tüm şerit aynı anda sıçramaz),
    // tepeye varınca eskisi gibi kare başına 1 ton geri kayar.
    const uint32_t kick = (uint32_t)p.get(P_HUE_KICK) << 8;
    if (sm_) {
      if (f.beat) kickTgt_ = vmin<uint32_t>(kickTgt_ + kick, 2u * kick);
      if (hueKick_ < kickTgt_) hueKick_ = vmin<uint32_t>(hueKick_ + vmax<uint32_t>(kick / 3u, 256u), kickTgt_);
      else { hueKick_ = hueKick_ > 256u ? hueKick_ - 256u : 0u; kickTgt_ = hueKick_; }
    } else {
      if (f.beat) hueKick_ = vmin<uint32_t>(hueKick_ + kick, 2u * kick);
      else hueKick_ = hueKick_ > 256u ? hueKick_ - 256u : 0u;
      kickTgt_ = hueKick_;
    }
    drift_ += (uint32_t)p.get(P_HUE_SPD);   // işaretli: uint32 sarmalı ton çemberiyle uyumlu
    hueRot_ = drift_ + hueKick_;
  }

  // --- mod örüntüsü ---
  switch (mode_) {
    case LM_AURORA:    genAurora(f, p, false); break;
    case LM_SPARKLE:   genAurora(f, p, true);  break;
    case LM_MIRROR:    genMirror(f, p);        break;
    case LM_SPECTRUM:  genSpectrum(f);         break;
    case LM_BASS:      genBass(f);             break;
    case LM_WATERFALL: genWaterfall(f, p);     break;
    case LM_FIRE:      genFire(f, p);          break;
    case LM_COMET:     genComet(f, p);         break;
    default:           genAurora(f, p, false); break;
  }

  // --- idle karışımı (18 kare) ---
  if (f.silent) { if (idleN_ < IDLE_XFADE) ++idleN_; }
  else if (idleN_) --idleN_;
  if (idleN_) {
    genIdle(p);
    const uint8_t w = (uint8_t)((idleN_ * 255) / IDLE_XFADE);
    for (int i = 0; i < n_; ++i) cur_[i] = rgb_lerp(cur_[i], idle_[i], w);
  }

  // --- mod crossfade (smoothstep) ---
  if (xfade_) {
    const uint32_t w = (uint32_t)(255u * (uint32_t)(xfadeN_ - xfade_ + 1)) / xfadeN_;
    const uint8_t ws = (uint8_t)((w * w * (765u - 2u * w)) / 65025u);
    for (int i = 0; i < n_; ++i) pattern_[i] = rgb_lerp(from_[i], cur_[i], ws);
    --xfade_;
  } else {
    for (int i = 0; i < n_; ++i) pattern_[i] = cur_[i];
  }

  // --- gamma (16 bit) → parlaklık → otomatik pozlama → zamansal dither ---
  const uint32_t bri1 = (uint32_t)vclamp<int32_t>(p.get(P_BRIGHTNESS), 0, 255) + 1u;
  const uint32_t ceil16 = (65535u * bri1) >> 8;       // bri tavanı: pozlama bunu aşamaz
  const uint8_t* src = &pattern_[0].r;
  uint32_t gq12 = 4096;                               // etkin pozlama kazancı Q12
  const uint32_t aeT = (uint32_t)vclamp<int32_t>(p.get(P_LED_AE), 0, 255);
  bool act = false;
  if (aeT == 0) {
    aeG_ = kAeMin; aeM_ = 0; aeAct_ = 0;
  } else {
    // Geri besleme: kazanç, bir önceki karelerin (yumuşatılmış) ÇIKIŞ ortalamasını tavanın aeT/256'sına
    // oturtur; sıkıştırma doyduğunda ölçüm yükselmediği için kazanç artmaya devam eder (en çok 24×).
    // Sessizlikte, eşiği aşan bant sayısı kAeMinBands'tan azsa veya sinyal kAeHold kareden kısa sürmüşse
    // kazanç ARTMAZ ve ölçüm donar; etkin değilken kazanç 1×'e söner.
    const uint32_t th16 = (uint32_t)vclamp<int32_t>(p.get(P_LED_THRESH), 0, 255) * 257u;
    int nAct = 0;
    for (int b = 0; b < NUM_BANDS; ++b) nAct += f.led[b] > th16;
    const bool sig = nAct >= kAeMinBands && !f.silent;
    if (sig) { if (aeAct_ < 2 * kAeHold) ++aeAct_; }
    else aeAct_ = aeAct_ > 3 ? aeAct_ - 3 : 0;
    act = sig && aeAct_ >= kAeHold;
    if (act) {
      const uint32_t mt = (aeT * ceil16) >> 8;
      uint32_t want = kAeMax;
      if (aeM_) {
        const uint32_t r = vmin<uint32_t>((mt << 12) / aeM_, 4u << 12);       // hedef/ölçüm, Q12
        want = vclamp<uint32_t>(((aeG_ >> 4) * r) >> 8, kAeMin, kAeMax);
      }
      const int32_t d = (int32_t)want - (int32_t)aeG_;
      aeG_ = (uint32_t)((int32_t)aeG_ + ((d * (d > 0 ? kAeUp : kAeDown)) >> 12));
      aeG_ = vclamp<uint32_t>(aeG_, kAeMin, kAeMax);
    } else if (aeG_ > kAeMin) {
      // etkin değil: kazanç artmaz, yavaşça 1×'e söner (köz/idle sessizlikte pozlamayla parlamasın)
      const uint32_t dec = vmax<uint32_t>(((aeG_ - kAeMin) * (uint32_t)kAeIdle) >> 12, 256u);  // üstel + en az 1/256 sabit adım
      aeG_ = aeG_ - kAeMin > dec ? aeG_ - dec : kAeMin;
    }
    // idle'a geçerken kazanç 1×'e karışır (idle parlaklığı pozlamadan etkilenmez); durum korunur.
    const uint32_t ge = aeG_ - ((aeG_ - kAeMin) * idleN_) / IDLE_XFADE;
    gq12 = ge >> 4;
  }
  uint32_t sum = 0, s16 = 0;
  const bool spatial = p.get(P_DITHER) != 0;
  {
    uint8_t* dst = &out_[0].r;
    for (int i = 0; i < n_; ++i) {
      uint32_t c[3];
      for (int k = 0; k < 3; ++k) c[k] = ((uint32_t)vtab::gamma22w[src[i * 3 + k]] * bri1) >> 8;
      if (gq12 > 4096) {
        uint32_t m = 0;
        // u32 taşmasın: kazanç Q10'a indirilir (65535·24576 < 2^32)
        for (int k = 0; k < 3; ++k) { c[k] = (c[k] * (gq12 >> 2)) >> 10; m = vmax(m, c[k]); }
        // tavanı aşan LED'de tonu koruyarak sıkıştır (kanal kırpması rengi bozar); c ≤ 24·65535
        if (m > ceil16) {
          const uint32_t sc = (ceil16 << 12) / m;   // Q12, < 1
          for (int k = 0; k < 3; ++k) c[k] = ((c[k] >> 2) * sc) >> 10;
        }
      }
      for (int k = 0; k < 3; ++k) {
        const int j = i * 3 + k;
        s16 += c[k];
        uint32_t o;
        if (spatial) {
          // Sabit uzaysal eşik: LED/kanal başına değişmeyen 0..255 ofset (altın oran dizisi). Sabit girişte çıkış kareden kareye
          // değişmez (zamansal titreşim yok); ortalama doğru kalır (ofsetler düzgün dağılır). Girdi yavaş değişirken her LED
          // kendi eşiğini geçince bir basamak atlar (tek yönlü, salınımsız).
          o = (c[k] + (uint8_t)(i * 159 + k * 85)) >> 8;
          if (o > 255) o = 255;
        } else {
          // Eski zamansal dither (hata yayılımı): 1 LSB düzeyinde 15–30 Hz titreme üretir (tests/test_flicker.cpp).
          uint32_t tot = c[k] + resid_[j];
          o = tot >> 8;
          if (o > 255) { o = 255; resid_[j] = 0; } else resid_[j] = (uint8_t)(tot & 255u);
        }
        dst[j] = (uint8_t)o;
        sum += o;
      }
    }
  }
  if (act) aeM_ = (uint32_t)((int32_t)aeM_ + (((int32_t)(s16 / (uint32_t)(n_ * 3)) - (int32_t)aeM_) >> 4));

  // --- güç limiter'ı: ma ≤ etkin bütçe garantisi (model tabanlı); aşağı anlık, yukarı +4/256 kare başına ---
  const PowerModel pm = power_model(p);
  {
    const int32_t B = led_budget_ma(p);
    const uint32_t smax = led_sum_allowed(n_, B, pm);
    budget_ = (uint16_t)vclamp<int32_t>(B, 0, 65535);
    demand_ = (uint16_t)vmin<uint32_t>((uint32_t)n_ * pm.idleUa / 1000u + sum * pm.ledMa / 765u, 65535u);
    uint32_t target = 256;
    if (sum > smax) target = (smax * 256u) / sum;      // < 256
    if (target < limL_) limL_ = (uint16_t)target;
    else limL_ = (uint16_t)vmin<uint32_t>(target, (uint32_t)limL_ + 4u);
    if (limL_ < 256) {
      uint8_t* dst = &out_[0].r;
      for (int i = 0; i < n_ * 3; ++i) dst[i] = (uint8_t)(((uint32_t)dst[i] * limL_) >> 8);
    }
    lim_ = limL_ >= 256 ? 255 : (uint8_t)limL_;
  }
  ma_ = (uint16_t)led_estimate_ma(out_, n_, pm);
}

}  // namespace fv

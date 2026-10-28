// fv_leds.h — LED örüntü motoru (CodePen mantığı + ek modlar), güç sınırlayıcı. Aktif uzunluk `leds`
// parametresidir (1..LED_MAX, vars. 120); tamponlar LED_MAX'a göre statik ayrılır.
#pragma once
#include "fv_config.h"
#include "fv_params.h"
#include "fv_analyzer.h"
#include "fv_color.h"

namespace fv {

enum LedMode : uint8_t {
  LM_AURORA = 0, LM_SPARKLE, LM_MIRROR, LM_SPECTRUM, LM_BASS,
  LM_WATERFALL, LM_FIRE, LM_COMET, LM_DEMO, LM_COUNT
};

const char* led_mode_name(uint8_t m);
const char* led_palette_name(uint8_t pal);

// SPARKLE'ın karışık LED sırası: Fisher–Yates (xorshift32, seed 0xC0DE) permütasyonu, n = 1..LED_MAX.
// n = 120 için vtab::perm120 tablosuyla birebir aynıdır (tests/test_leds_param.cpp).
void led_perm(uint16_t* out, int n);

// Akım modeli (tamsayı, deterministik; MODEL TABANLI koruma, gerçek akım ölçülmez):
//   mA = n·idle_µA/1000 + Σ(r+g+b)·led_ma/765      (765 = 3 kanal × 255)
// Varsayılan led_ma = 60, idle = 1000 µA eski modelle bit-aynıdır: n·1 + Σ·20/255 (60/765 = 20/255).
struct PowerModel { uint32_t ledMa; uint32_t idleUa; };
PowerModel power_model(const Params& p);
uint32_t led_estimate_ma(const Rgb* px, int n, const PowerModel& m);
uint32_t led_estimate_ma(const Rgb* px, int n);   // varsayılan model (60 mA/LED, 1 mA bekleme)
// Bütçeye uyan en büyük Σ(r+g+b): mA(Σ) ≤ budget. Bütçe bekleme akımına yetmiyorsa 0 (şerit siyah).
uint32_t led_sum_allowed(int n, int32_t budgetMa, const PowerModel& m);

// Etkin akım bütçesi (mA) = min(ma, psu_ma·psu_pct/100 [psu_ma > 0], ma_usb [usbPower]). `src`: sınırı hangisi koydu.
enum BudgetSrc : uint8_t { BS_MA = 0, BS_PSU, BS_USB };
int32_t led_budget_ma(const Params& p, BudgetSrc* src = nullptr);

class LedEngine {
 public:
  void reset(uint32_t seed, int leds = NUM_LEDS);
  // Kare başına bir kez: örüntü → idle/crossfade → gamma → parlaklık → otomatik pozlama → limiter.
  void step(const Features& f, const Params& p);
  int count() const { return n_; }                 // aktif LED sayısı (step() son parametreye göre günceller)
  const Rgb* pattern() const { return pattern_; }  // gamma öncesi (simülatör gösterimi)
  const Rgb* out() const { return out_; }          // wire değerleri, RGB sırası
  uint16_t milliamps() const { return ma_; }       // limiter sonrası tahmin
  uint8_t limiterQ8() const { return lim_; }       // 255 = sınırlama yok
  uint16_t demandMa() const { return demand_; }    // limiter ÖNCESİ tahmin (bütçe aşılıyorsa istenen akım)
  uint16_t budgetMa() const { return budget_; }    // bu karede uygulanan etkin bütçe
  uint16_t aeGainQ8() const { return (uint16_t)(aeG_ >> 8); }  // otomatik pozlama kazancı Q8 (256 = 1×)
  uint8_t activeMode() const { return mode_; }     // DEMO içindeyken gerçek alt mod
  uint8_t paletteOffset() const { return (uint8_t)palOff_; }   // etkin ton ofseti (palet + hue_ofs), test/teşhis
  uint32_t paletteSpan() const { return palSpan_; }            // etkin palet genişliği (256 = tam çember)

 private:
  struct Comet { int32_t pos; int16_t vel; uint8_t hue; uint8_t on; };  // pos: Q8 LED koordinatı

  void switchMode(uint8_t m, int frames);
  void layout(int n);                         // uzunluğa bağlı tabloları kur, LED başına durumu sıfırla
  void updatePalette(const Params& p);
  // Ton → palet. fold: h sarmalanabilen ton (AURORA/SPARKLE/MIRROR/WATERFALL/idle: konum + ton akışı); dar palet (span < 256)
  // çember olmadığından h ileri-geri katlanır (testere yerine üçgen): palet sonunda renk sıçraması (dikiş) olmaz.
  // fold = false: h zaten 0..255 içinde sarmalanmayan (centroid, SPECTRUM, comet) → doğrusal eşleme.
  static uint8_t hm(uint32_t h, uint32_t off, uint32_t span, bool fold) {
    h &= 255u;
    if (fold && span < 256u) h = h < 128u ? 2u * h : 2u * (255u - h);   // 0..254 üçgen
    return (uint8_t)(off + ((h * span) >> 8));
  }
  // Palet rengi; çember ↔ dar palet geçişinde (palW_ > 0) iki paletin RENKLERİ karıştırılır (ton ara değerlemesi çemberden
  // palete topolojik olarak sürekli yapılamaz: ara değerde dikiş doğar).
  Rgb pc(uint32_t h, uint8_t s, uint8_t v, bool fold) const {
    const Rgb a = hsv8(hm(h, palOff_, palSpan_, fold && sm_), s, v);
    if (!palW_) return a;
    return rgb_lerp(a, hsv8(hm(h, palOffB_, palSpanB_, fold && sm_), s, v), palW_);
  }
  void genAurora(const Features& f, const Params& p, bool sparkle);
  void genMirror(const Features& f, const Params& p);
  void genSpectrum(const Features& f);
  void genBass(const Features& f);
  void genWaterfall(const Features& f, const Params& p);
  void genFire(const Features& f, const Params& p);
  void genComet(const Features& f, const Params& p);
  void genIdle(const Params& p);
  void bloom(uint32_t k);

  Rgb pattern_[LED_MAX];
  Rgb out_[LED_MAX];
  uint16_t ma_ = 0;
  uint16_t demand_ = 0, budget_ = 0;
  uint8_t lim_ = 255;
  uint8_t mode_ = 0;
  // --- uygulamaya özel durum ---
  uint32_t rng_ = 1;
  uint32_t t_ = 0;
  Rgb cur_[LED_MAX];            // aktif modun ham çıktısı
  Rgb idle_[LED_MAX];           // idle animasyonu
  Rgb from_[LED_MAX];           // mod geçişi crossfade kaynağı (görünen son örüntü)
  uint16_t bl_[LED_MAX * 3];    // bloom ileri geçiş ara belleği
  uint8_t resid_[LED_MAX * 3];  // gamma sonrası zamansal dither kalıntısı
  uint16_t posX_[2][LED_MAX];   // bant konumu Q8 (0: sıralı, 1: karışık)
  uint16_t posH_[2][LED_MAX];   // Q8 ton (0..200)
  uint16_t mirX_[LED_MAX];      // MIRROR bant konumu Q8
  uint8_t mirH_[LED_MAX];       // MIRROR ton kayması
  Rgb trail_[LED_MAX];          // COMET iz tamponu
  uint8_t spark_[LED_MAX];      // COMET beyaz kıvılcımlar
  uint8_t heat_[2][LED_MAX / 2 + 2];  // FIRE (+2: kıvılcım y=0..2, çok kısa şeritte de taşmasın)
  Rgb wf_[LED_MAX / 2 + 1];         // WATERFALL (merkezden uzaklık)
  Comet comets_[6];
  uint32_t be_ = 0;              // yumuşatılmış bas (Q16)
  uint32_t hueRot_ = 0;          // Q8 ton dönüşü
  uint32_t hueKick_ = 0;         // beat tekmesi (Q8, kare başına 1 ton geri kayar)
  uint32_t drift_ = 0;           // yavaş ton akışı (Q8)
  uint32_t demoBeats_ = 0, demoFrames_ = 0;
  int n_ = NUM_LEDS;             // aktif LED sayısı
  int half_ = (NUM_LEDS + 1) / 2;  // merkezden uzaklık sayısı (WATERFALL/FIRE yarım şerit)
  uint32_t idlePh_ = 0;          // boşta animasyon fazı (Q8)
  uint32_t palT_ = 0;            // palet döngüsü sayacı (kare)
  uint32_t palOff_ = 0;          // etkin ton ofseti (palet + hue_ofs), 0..255
  uint32_t palSpan_ = 256;       // etkin palet genişliği (256 = tam renk çemberi)
  uint32_t palOffB_ = 0, palSpanB_ = 256;   // çapraz geçişte hedef palet (palW_ > 0 iken)
  uint8_t palW_ = 0;             // renk çapraz geçişi ağırlığı 0..255 (0 = yok)
  bool sm_ = true;               // P_SMOOTH
  uint32_t kickTgt_ = 0;         // ton tekmesi hedefi (Q8); hueKick_ ona sınırlı hızla yükselir
  uint32_t cenS_ = 100u << 8;    // süzülmüş spektral merkez tonu (Q8)
  uint8_t cenHue_ = 100;         // modlarda kullanılan merkez tonu (süzülmüş ya da ham)
  Rgb alt_[LED_MAX];             // FIRE palet çapraz geçişi için ikinci renk tamponu
  uint16_t wfPh_ = 0;
  uint16_t limL_ = 256;          // limiter çarpanı Q8 (256 = sınırlama yok)
  uint32_t aeG_ = 65536;         // otomatik pozlama kazancı Q16 (65536 = 1×)
  uint8_t aeAct_ = 0;            // pozlama etkinliği: ardışık "sinyal var" karesi
  uint32_t aeM_ = 0;             // pozlama ölçümü: yumuşatılmış çıkış ortalaması (u16 doğrusal, 0 = bilinmiyor)
  uint8_t reqMode_ = 255;        // son istenen mod (P_LED_MODE)
  uint8_t demoSub_ = 0;
  uint8_t idleN_ = 0;            // 0..IDLE_XFADE
  uint8_t xfade_ = 0, xfadeN_ = 1;
  uint8_t cometDir_ = 0;
};

}  // namespace fv

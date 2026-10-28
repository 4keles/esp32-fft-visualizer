#include "fv_params.h"

namespace fv {

// {ad, min, max, vars., grup, bayrak, açıklama}. Yeni parametrelerin varsayılanı eski gömülü sabittir
// (varsayılan görünüm bit-aynı kalır; tests/test_leds_regress.cpp).
static const ParamInfo kInfo[P_COUNT] = {
    {"mode", 0, 8, 0, PG_GENERAL, PF_NORAMP, "LED modu: 0 AURORA 1 SPARKLE 2 MIRROR 3 SPECTRUM 4 BASS 5 WATERFALL 6 FIRE 7 COMET 8 DEMO"},
    {"theme", 0, 4, 0, PG_GENERAL, PF_NORAMP, "ekran teması: 0 SPECTRUM 1 EMBER 2 NEON 3 FIRE 4 CHROMA"},
    {"bri", 0, 255, 255, PG_GENERAL, 0, "LED parlaklık tavanı"},
    {"ma", 100, 20000, 2500, PG_STRIP, PF_SETUP | PF_NORAMP, "LED akım bütçesi (mA, model tahmini); limiter bunu aşmaz"},
    {"gain", -128, 3, -128, PG_AUDIO, PF_SETUP | PF_NORAMP, "sabit AGC tavanı dBFS; -128 = otomatik"},
    {"agcmin", -60, -10, -45, PG_AUDIO, PF_SETUP, "AGC tavanının alt sınırı (dBFS)"},
    {"hud", 0, 2, 0, PG_GENERAL, PF_SETUP | PF_NORAMP, "ekran bilgi katmanı: 0 otomatik 1 sürekli 2 kapalı"},
    {"bar_atk", 1, 255, 154, PG_SCREEN, 0, "ekran bar yükselme hızı (Q8)"},
    {"bar_rel", 1, 255, 31, PG_SCREEN, 0, "ekran bar düşme hızı (Q8)"},
    {"led_atk", 1, 255, 166, PG_LEDREACT, 0, "LED bant yükselme hızı (Q8)"},
    {"led_rel", 1, 255, 56, PG_LEDREACT, 0, "LED bant düşme hızı (Q8)"},
    {"cap_hold", 0, 120, 18, PG_SCREEN, 0, "tepe işareti bekleme süresi (kare)"},
    {"cap_grav", 1, 1024, 66, PG_SCREEN, 0, "tepe işareti düşme ivmesi (Q16/kare²)"},
    {"bar_th", 0, 230, 84, PG_SCREEN, 0, "ekran bar eşiği (Q8); altı görünmez"},
    {"led_th", 0, 230, 50, PG_LEDREACT, 0, "LED eşiği (Q8); altı yalnız köz"},
    {"tilt", 0, 60, 15, PG_AUDIO, 0, "spektrum eğimi (0,1 dB/oktav)"},
    {"beat_sens", 128, 1024, 384, PG_AUDIO, 0, "vuruş duyarlılığı (Q8 sapma çarpanı; küçük = duyarlı)"},
    {"bloom", 0, 230, 128, PG_LEDREACT, 0, "LED parıltı yayılımı (Q8 çekirdek)"},
    {"led_ae", 0, 255, 48, PG_LEDREACT, 0, "otomatik pozlama hedefi (0 = kapalı)"},
    {"fmin", 40, 1500, 250, PG_AUDIO, PF_SETUP | PF_NORAMP, "gösterilen en düşük frekans (Hz); değişince cal gerekir"},
    {"fmax", 4500, 10000, 10000, PG_AUDIO, PF_SETUP | PF_NORAMP, "gösterilen en yüksek frekans (Hz)"},
    {"leds", 1, LED_MAX, 120, PG_STRIP, PF_SETUP | PF_NORAMP, "aktif LED sayısı (2 m = 120, 1 m = 60, 3 m = 180)"},
    {"hue_spd", -255, 255, 6, PG_COLOR, 0, "ton akış hızı (Q8 ton/kare; 6 ≈ 1,4 ton/s; eksi = ters yön)"},
    {"hue_ofs", 0, 255, 0, PG_COLOR, 0, "sabit ton kaydırma (0..255 = tam renk çemberi)"},
    {"hue_kick", 0, 30, 6, PG_COLOR, 0, "vuruşta ton tekmesi (ton)"},
    {"pal", 0, LED_PAL_N - 1, 0, PG_COLOR, 0, "LED paleti: 0 GOKKUSAGI 1 SICAK 2 OKYANUS 3 ORMAN 4 MOR 5 ALTIN"},
    {"pal_cyc", 0, 255, 0, PG_COLOR, 0, "paletler kayarak sırayla değişsin: periyot (s); 0 = kapalı"},
    {"wf_spd", 0, 512, 192, PG_PATTERN, 0, "WATERFALL kayma hızı (Q8 LED/kare; 192 = 0,75)"},
    {"wf_dec", 128, 255, 247, PG_PATTERN, 0, "WATERFALL iz kalıcılığı (Q8; büyük = uzun iz)"},
    {"fire_cool", 0, 200, 75, PG_PATTERN, 0, "FIRE soğuma tabanı (büyük = kısa alev)"},
    {"fire_spark", 0, 200, 40, PG_PATTERN, 0, "FIRE kıvılcım sıklığı tabanı (/256 kare)"},
    {"comet_spd", 32, 1024, 256, PG_PATTERN, 0, "COMET hız çarpanı (Q8; 256 = 1×)"},
    {"comet_trl", 100, 250, 204, PG_PATTERN, 0, "COMET iz kalıcılığı (Q8; büyük = uzun kuyruk)"},
    {"ember", 0, 60, 9, PG_PATTERN, 0, "köz parlaklığı: eşik altı LED'lerin taban V değeri"},
    {"idle_bri", 0, 150, 22, PG_TRANS, 0, "sessizlikte gökkuşağı taban parlaklığı (V)"},
    {"idle_spd", 0, 1024, 128, PG_TRANS, 0, "sessizlikte animasyon hızı (Q8 faz/kare)"},
    {"fade", 1, 120, 24, PG_TRANS, 0, "elle mod değişimi geçiş süresi (kare, 60/s)"},
    {"demo_beats", 1, 255, 32, PG_TRANS, 0, "DEMO: alt mod başına vuruş sayısı"},
    {"demo_sec", 1, 120, 30, PG_TRANS, 0, "DEMO: vuruş yoksa alt mod başına süre (s)"},
    {"demo_fade", 1, 120, 30, PG_TRANS, 0, "DEMO alt mod geçiş süresi (kare)"},
    {"bass_atk", 1, 255, 179, PG_LEDREACT, 0, "bas zarfı yükselme hızı (Q8; BASS/FIRE)"},
    {"bass_rel", 1, 255, 26, PG_LEDREACT, 0, "bas zarfı düşme hızı (Q8)"},
    {"dither", 0, 1, 1, PG_LEDREACT, 0, "LED nicemleme: 0 zamansal (düşük seviyede titrer), 1 sabit uzaysal (titreşimsiz)"},
    {"led_ma", 5, 100, 60, PG_STRIP, PF_SETUP | PF_NORAMP, "LED başına tam beyaz akımı (mA); 60 = WS2812B veri sayfası, kendi şeridinizi ölçün/etiketten hesaplayın"},
    {"led_idle", 0, 5000, 1000, PG_STRIP, PF_SETUP | PF_NORAMP, "LED başına bekleme akımı (µA); siyah LED de çeker"},
    {"psu_ma", 0, 30000, 0, PG_STRIP, PF_SETUP | PF_NORAMP, "güç kaynağı kapasitesi (mA); 0 = belirtilmedi (yalnız ma geçerli)"},
    {"psu_pct", 20, 100, 80, PG_STRIP, PF_SETUP | PF_NORAMP, "kaynağın kullanılacak oranı (%); etkin bütçe <= psu_ma·psu_pct/100"},
    {"ma_usb", 50, 5000, 500, PG_STRIP, PF_SETUP | PF_NORAMP, "USB beslemesinde akım bütçesi (mA); yalnız adaptör algılama açıkken"},
    {"smooth", 0, 1, 1, PG_COLOR, 0, "renk geçişleri: 1 yumuşak (palet dikişi yok, ton tekmesi yayılı, merkez-ton süzgeçli), 0 eski (atlamalı)"},
    {"autocal", 0, 1, 0, PG_AUDIO, PF_SETUP | PF_NORAMP, "açılışta otomatik gürültü kalibrasyonu (3 s sonra 2 s); müzik/ses/gürültü durağan değilse reddeder ve tekrar dener"},
};

const ParamInfo& param_info(ParamId id) { return kInfo[id < P_COUNT ? id : 0]; }

const char* param_group_name(uint8_t g) {
  static const char* const n[PG_COUNT] = {"Genel", "Şerit ve güç", "Ses ve analiz", "Ekran", "LED tepkisi",
                                          "Renk ve palet", "Desenler", "Geçişler ve boşta"};
  return g < PG_COUNT ? n[g] : "?";
}

static bool str_eq(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

bool param_lookup(const char* name, ParamId* out) {
  for (int i = 0; i < P_COUNT; ++i) {
    if (str_eq(name, kInfo[i].name)) { *out = (ParamId)i; return true; }
  }
  return false;
}

// Derleme zamanı ezmeler (firmware config.h). BSS'te sıfırlı; heap/guard yok.
static int32_t s_defVal[P_COUNT];
static bool s_defSet[P_COUNT];

int32_t param_default(ParamId id) {
  if (id >= P_COUNT) id = P_LED_MODE;
  return s_defSet[id] ? s_defVal[id] : kInfo[id].def;
}

bool param_set_default(ParamId id, int32_t value) {
  if (id >= P_COUNT) return false;
  const ParamInfo& in = kInfo[id];
  if (value < in.min) value = in.min;
  if (value > in.max) value = in.max;
  s_defVal[id] = value;
  s_defSet[id] = true;
  return true;
}

void param_clear_defaults() {
  for (int i = 0; i < P_COUNT; ++i) s_defSet[i] = false;
}

void Params::defaults() {
  for (int i = 0; i < P_COUNT; ++i) v[i] = param_default((ParamId)i);
  usbPower = 0;
}

bool Params::set(ParamId id, int32_t value) {
  if (id >= P_COUNT) return false;
  const ParamInfo& in = kInfo[id];
  if (value < in.min) value = in.min;
  if (value > in.max) value = in.max;
  if (v[id] == value) return false;
  v[id] = value;
  return true;
}

}  // namespace fv

// fv_params.h — çalışma zamanı parametreleri (CLI, NVS ve WASM ortak).
// Tüm değerler int32. Firmware'de tek yazar (core 1), okuyucular hizalı alan okur.
#pragma once
#include "fv_config.h"
#include "fv_types.h"

namespace fv {

constexpr int LED_PAL_N = 6;  // LED paleti sayısı (fv_leds.cpp tablosu)

enum ParamId : uint8_t {
  P_LED_MODE = 0,   // LedMode
  P_THEME,          // Theme
  P_BRIGHTNESS,     // 0..255 kullanıcı parlaklığı (varsayılan 255)
  P_MA_BUDGET,      // LED akım bütçesi mA, model tahmini (varsayılan 2500, 100..20000); PSU/USB sınırı bunun altına iner
  P_GAIN_DB,        // -128 = AGC otomatik; aksi halde sabit tavan R (dBFS, -45..3)
  P_AGC_MIN_DB,     // AGC tavanının alt sınırı (dBFS, varsayılan -45)
  P_HUD,            // 0 = otomatik (değişiklikte 2.5 s), 1 = sürekli, 2 = kapalı
  P_BAR_ATTACK,     // Q8 (varsayılan 154 ≈ 0.60)
  P_BAR_RELEASE,    // Q8 (varsayılan 31 ≈ 0.12)
  P_LED_ATTACK,     // Q8 (varsayılan 166 ≈ 0.65)
  P_LED_RELEASE,    // Q8 (varsayılan 56 ≈ 0.22)
  P_CAP_HOLD,       // kare (varsayılan 18)
  P_CAP_GRAVITY,    // Q16/kare² (varsayılan 66)
  P_BAR_THRESH,     // Q8 normalize eşik (varsayılan 84 ≈ 0.33)
  P_LED_THRESH,     // Q8 normalize eşik (varsayılan 50 ≈ 0.20)
  P_TILT,           // 0.1 dB/oktav (varsayılan 15 = +1.5 dB/okt)
  P_BEAT_SENS,      // Q8 sapma çarpanı (varsayılan 384 = 1.5)
  P_BLOOM,          // Q8 bloom çekirdeği k (varsayılan 128 = 0.5)
  P_LED_AE,         // LED otomatik pozlama hedef ortalaması Q8 (0 = kapalı, varsayılan 48 ≈ 0.19)
  P_FMIN,           // en düşük gösterilen frekans Hz (40 bant [fmin, fmax] log aralıklı; vars. 250)
  P_FMAX,           // en yüksek gösterilen frekans Hz (vars. 10000); değişince bant düzeni yeniden başlar
  // --- parametrik-cli: şerit uzunluğu ve eskiden gömülü olan desen sabitleri ---
  P_LEDS,           // aktif LED sayısı 1..LED_MAX (varsayılan 120 = 2 m @ 60 LED/m)
  P_HUE_SPD,        // ton akış hızı Q8 ton/kare, işaretli (vars. 6 ≈ 1,4 ton/s)
  P_HUE_OFS,        // sabit ton ofseti 0..255 (tüm modlara)
  P_HUE_KICK,       // vuruşta ton tekmesi (ton; vars. 6)
  P_PAL,            // LED paleti 0..LED_PAL_N-1 (0 = gökkuşağı, eski görünüm)
  P_PAL_CYC,        // paletler arası otomatik geçiş periyodu (s; 0 = kapalı)
  P_WF_SPD,         // WATERFALL kayma hızı Q8 LED/kare (vars. 192 = 0,75)
  P_WF_DEC,         // WATERFALL adım başına kalıcılık Q8 (vars. 247 ≈ 0,97)
  P_FIRE_COOL,      // FIRE soğuma tabanı (vars. 75)
  P_FIRE_SPARK,     // FIRE kıvılcım tabanı /256 (vars. 40)
  P_COMET_SPD,      // COMET hız çarpanı Q8 (vars. 256 = 1×)
  P_COMET_TRL,      // COMET iz kalıcılığı Q8 (vars. 204 ≈ 0,80)
  P_EMBER,          // AURORA/MIRROR köz parlaklığı (V; vars. 9)
  P_IDLE_BRI,       // boşta (sessizlik) gökkuşağının taban parlaklığı V (vars. 22)
  P_IDLE_SPD,       // boşta animasyon hızı Q8 faz/kare (vars. 128 = 0,5)
  P_FADE,           // elle mod değişimi crossfade süresi (kare; vars. 24)
  P_DEMO_BEATS,     // DEMO: alt mod başına vuruş sayısı (vars. 32)
  P_DEMO_SEC,       // DEMO: vuruş yoksa alt mod başına azami süre (s; vars. 30)
  P_DEMO_FADE,      // DEMO alt mod crossfade süresi (kare; vars. 30)
  P_BASS_ATK,       // bas zarfı atak Q8 (vars. 179 ≈ 0,70)
  P_BASS_REL,       // bas zarfı bırakma Q8 (vars. 26 ≈ 0,10)
  P_DITHER,         // 8 bit nicemleme: 0 = eski zamansal dither (kare kare titrer), 1 = sabit uzaysal eşik (vars.)
  // --- power-config: donanıma göre akım modeli ve güç kaynağı koruması ---
  P_LED_MA,         // LED başına tam beyaz (R+G+B = 255) akımı mA (vars. 60 = WS2812B veri sayfası tavanı; 5..100)
  P_LED_IDLE,       // LED başına bekleme akımı µA (vars. 1000; siyah bile bu kadar çeker; 0..5000)
  P_PSU_MA,         // güç kaynağı kapasitesi mA (0 = belirtilmedi: yalnız `ma` geçerli; 0..30000)
  P_PSU_PCT,        // kaynağın güvenle kullanılacak oranı % (vars. 80; yalnız psu_ma > 0 iken etkili; 20..100)
  P_MA_USB,         // USB beslemesinde bütçe mA (vars. 500; yalnız adaptör algılama donanımı + config açıksa etkili)
  // --- led-smooth: renk geçişlerinde atlama/takılma ---
  P_SMOOTH,         // 1 = yumuşak renk (palet katlama + çember↔palet çapraz geçişi, yumuşak ton tekmesi, merkez-ton süzgeci; vars.), 0 = eski
  // --- autocal-boot: açılışta korumalı otomatik gürültü kalibrasyonu ---
  P_AUTOCAL,        // 1 = açılıştan ~3 s sonra 2 s'lik gürültü kalibrasyonu dener; müzik/ses/gürültü durağan değilse reddeder (çekirdek vars. 0; firmware CFG_AUTOCAL_BOOT)
  P_COUNT
};

// Parametre grupları (CLI `params` çıktısı ve simülatör sıralaması için).
enum ParamGroup : uint8_t { PG_GENERAL = 0, PG_STRIP, PG_AUDIO, PG_SCREEN, PG_LEDREACT, PG_COLOR, PG_PATTERN, PG_TRANS, PG_COUNT };
// Bayraklar.
enum : uint8_t {
  PF_SETUP  = 1,  // kurulum/donanım ayarı: preset'e girmez (leds, ma, fmin, fmax, gain, agcmin, hud)
  PF_NORAMP = 2,  // ramp/sweep ile değiştirilemez (seçim/yeniden-düzen gerektirenler)
};

struct ParamInfo {
  const char* name;  // CLI/NVS anahtarı (≤ 12 karakter, küçük harf)
  int32_t min, max, def;
  uint8_t group;     // ParamGroup
  uint8_t flags;     // PF_*
  const char* desc;  // tek satırlık açıklama (help <param>)
};

const ParamInfo& param_info(ParamId id);
const char* param_group_name(uint8_t g);
// İsimden id bul; bulunamazsa false.
bool param_lookup(const char* name, ParamId* out);
// Etkin varsayılan: ParamInfo::def ya da firmware'in (config.h) ezdiği değer. Params::defaults() ve "varsayılandan farklı"
// karşılaştırmaları bunu kullanır. Ezme [min,max]'a kırpılır; testler param_clear_defaults() ile geri alır.
int32_t param_default(ParamId id);
bool param_set_default(ParamId id, int32_t value);
void param_clear_defaults();

struct Params {
  int32_t v[P_COUNT];
  // Çalışma zamanı durumu (parametre DEĞİL: NVS'e yazılmaz, preset'e girmez): 1 = şerit gücü USB hattından
  // (adaptör algılama donanımı varsa firmware yazar). 0 iken `ma_usb` hiçbir etki yapmaz.
  uint8_t usbPower;
  void defaults();
  int32_t get(ParamId id) const { return v[id]; }
  // Değeri [min,max]'a kırpar; değer değiştiyse true.
  bool set(ParamId id, int32_t value);
};

}  // namespace fv

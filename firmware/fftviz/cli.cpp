// cli.cpp — seri komut satırı (115200). Yanıtlar `OK ...` / `ERR ...` (+ `WARN`, `NOT` bilgi satırları).
// Her parametre (fv_params.cpp) doğrudan komuttur: `bri 128`, `leds 60`, `pal okyanus`. Ek komutlar: help params get set save
// reset preset ramp sweep anim palcycle ledtest ... (ayrıntı: `help`).
#include "cli.h"
#include "shared.h"
#include "pins.h"
#include "controls.h"
#include "powersrc.h"
#include "ir.h"
#include "display_task.h"
#include "audio_task.h"
#include "selftest.h"
#include "st7789.h"
#include <esp_timer.h>
#include <math.h>
#include <driver/gpio.h>

namespace cli {

static char s_line[128];
static int s_len = 0;
static uint32_t s_statsMs = 0, s_statsAt = 0;
static uint32_t s_streamMs = 0, s_streamAt = 0, s_streamSeq = 0;

// Hata/bilgi biçimi: tüm hatalar "ERR <komut>: <açıklama>" tek satırdır.
static void err(const char* cmd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void err(const char* cmd, const char* fmt, ...) {
  char msg[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Serial.printf("ERR %s: %s\n", cmd, msg);
}

// Komut tablosu: `help <komut>` ayrıntısı ve yakın eşleşme önerisi için. Parametreler ayrıca fv_params.cpp'de.
struct CmdInfo { const char* name; const char* usage; const char* desc; };
static const CmdInfo kCmds[] = {
    {"help", "help [komut|parametre]", "komut listesi; ayrıntı için komut ya da parametre adı verin"},
    {"params", "params [grup]", "parametreleri gruplu listeler (genel serit ses ekran led renk desen gecis); * = varsayılandan farklı"},
    {"get", "get [param]", "boşsa tüm parametreler tek satır JSON (araçlar için); param verilirse tek değer"},
    {"set", "set <param> <değer>", "parametre yazar (param adı doğrudan komut olarak da yazılabilir)"},
    {"save", "save", "ayarları hemen NVS'e yazar; ramp/sweep ile değişen değerler de o anki haliyle kaydedilir"},
    {"reset", "reset", "fabrika ayarları (NVS temizlenir, preset'ler dahil)"},
    {"reboot", "reboot", "kartı yeniden başlatır"},
    {"preset", "preset save|load|del <0-3> | preset list", "görünüm ayarlarını 4 yuvada saklar (leds/ma/fmin/fmax/gain/agcmin/hud hariç)"},
    {"ramp", "ramp <param> [başlangıç] <hedef> <süre> | ramp stop [param]", "parametreyi (başlangıç verilmezse şimdiki değerinden) hedefe süre boyunca doğrusal götürür; süre: 3000, 3s, 500ms, 2m"},
    {"sweep", "sweep <param> <min> <max> <periyot> [tri|sin] | sweep stop [param]", "parametreyi min-max arasında sürekli salındırır (üçgen ya da sinüs)"},
    {"anim", "anim | anim stop [param]", "çalışan ramp/sweep'leri listeler ya da durdurur (en çok 4 eş zamanlı)"},
    {"palcycle", "palcycle [saniye|off]", "paletler her periyotta kayarak sırayla değişsin (pal_cyc); 0/off = kapalı"},
    {"power", "power [reset]", "akım koruması durumu: model, etkin bütçe (ma/psu/usb), şimdiki ve tepe mA, limiter oranı; reset tepeleri sıfırlar (MODEL tahmini, gerçek akım ölçülmez)"},
    {"ledtest", "ledtest [0-7|auto]", "sabit LED desenleri: 1 kırmızı 2 yeşil 3 mavi 4 beyaz 5 işaretler(ilk kırmızı, son mavi) 6 gökkuşağı 7 gezen nokta"},
    {"ledoff", "ledoff [0|1]", "teşhis: LED çıkışını siyah yap (1) / normal (0)"},
    {"cal", "cal", "gürültü kalibrasyonu (2 s sessizlik); fmin/fmax değişince gerekir. Açılışta otomatik: autocal"},
    {"ir", "ir | ir learn on|off", "volatile NEC/raw learning; normalized LSB-first bytes -> CFG_IR_ADDRESS / CFG_IR_KEY_<label>; rebuild and user-approved flashing"},
    {"src", "src [mic|0..6]", "giriş: mikrofon ya da cihaz içi sentetik senaryo (4 = müzik benzeri)"},
    {"inj", "inj <Hz> <dBFS> | inj 0", "teşhis: mikrofona bilinen seviyede sinüs ekler"},
    {"bands", "bands", "bant teşhisi (JSON: ham/gürültü dBFS×10, seviye, bar, merkez Hz, kenarlar)"},
    {"raw", "raw [n]", "ham ADC istatistiği (n örnek @48 kHz, vars. 4800): min/max/rms/kırpma"},
    {"rawdump", "rawdump [n]", "ham ADC dalga biçimi (n ≤ 1024, 4 haneli hex)"},
    {"stats", "stats [hz]", "tek satır JSON durum; hz>0 periyodik, 0 durdur"},
    {"stream", "stream [hz]", "bar akışı (F satırları); 0 durdur"},
    {"bench", "bench", "performans ölçümü (JSON)"},
    {"selftest", "selftest", "kurulum self-testi"},
    {"dsp", "dsp", "yalnız DSP golden hash testi"},
    {"spi", "spi [10-80]", "ekran SPI saati (MHz)"},
    {"rot", "rot [0-3]", "ekran rotasyonu"},
    {"offs", "offs [x y|auto]", "ekran RAM ofseti (240x320 kontrolcülerde 0/80)"},
    {"skip", "skip [0|1]", "ekran şerit checksum atlaması (vars. 0 = SPI trafiği sabit)"},
    {"drive", "drive [0-3]", "teşhis: ekran SCK/MOSI/DC sürüş gücü (0 = 5 mA .. 3 = 40 mA)"},
    {"disp", "disp [0|1]", "teşhis: ekran çizimi/SPI durdur (0) / devam (1)"},
    {"bl", "bl [on|0-255]", "teşhis: arka ışık PWM'siz sabit (on) / PWM değeri"},
};
static constexpr int kCmdCount = sizeof(kCmds) / sizeof(kCmds[0]);

static const char kHelp[] =
    "FFTVIZ CLI. Her parametre doğrudan komuttur (bri 128, leds 60, pal okyanus); 'help <komut|parametre>' ayrıntı.\n"
    "  Görünüm     mode theme bri pal palcycle hue_spd hue_ofs hue_kick   (hepsi: params)\n"
    "  Şerit/güç   leds ma led_ma psu_ma psu_pct ma_usb led_idle   power ledtest ledoff\n"
    "  Otomasyon   ramp sweep anim          parametreyi zamanla değiştir\n"
    "  Kayıt       preset save|load|list    save reset get set params\n"
    "  Ses/analiz  gain agcmin fmin fmax tilt beat_sens   cal autocal src inj bands\n"
    "  Ekran       hud spi rot offs bl disp skip drive\n"
    "  Teşhis      stats stream raw rawdump bench selftest dsp ir\n"
    "Yanıtlar: OK ... | ERR <komut>: ... | WARN | NOT (bilgi). Örnek: 'ramp bri 0 3s', 'sweep hue_ofs 0 255 20s', 'palcycle 30'.\n"
    "reboot  kartı yeniden başlatır\n";

// --- yakın eşleşme (düzenleme uzaklığı) ---
static int editDist(const char* a, const char* b) {   // Damerau-Levenshtein (bitişik harf takası = 1)
  int la = (int)strlen(a), lb = (int)strlen(b);
  if (la > 15 || lb > 15) return 99;
  int r0[16], r1[16], r2[16];
  int *pp = r0, *prev = r1, *cur = r2;
  for (int j = 0; j <= lb; ++j) prev[j] = j;
  for (int i = 1; i <= la; ++i) {
    cur[0] = i;
    for (int j = 1; j <= lb; ++j) {
      int c = prev[j - 1] + (a[i - 1] != b[j - 1]);
      if (prev[j] + 1 < c) c = prev[j] + 1;
      if (cur[j - 1] + 1 < c) c = cur[j - 1] + 1;
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] && pp[j - 2] + 1 < c) c = pp[j - 2] + 1;
      cur[j] = c;
    }
    int* t = pp; pp = prev; prev = cur; cur = t;
  }
  return prev[lb];
}

// En yakın komut/parametre adı (uzaklık ≤ 2, kısa kelimede ≤ 1) ya da nullptr.
static const char* suggest(const char* w, bool params = true, bool cmds = true) {
  const char* best = nullptr;
  int bd = 99;
  auto consider = [&](const char* name) {
    int d = editDist(w, name);
    if (d == 0 || (w[0] && strncmp(name, w, strlen(w)) == 0 && strlen(w) >= 2)) d = 1;   // önek = yakın
    if (d < bd) { bd = d; best = name; }
  };
  if (cmds) for (int i = 0; i < kCmdCount; ++i) consider(kCmds[i].name);
  if (params) for (int i = 0; i < fv::P_COUNT; ++i) consider(fv::param_info((fv::ParamId)i).name);
  const int lim = strlen(w) <= 4 ? 1 : 2;
  return bd <= lim ? best : nullptr;
}

static void unknown(const char* what, const char* w) {
  const char* s = suggest(w);
  if (s) Serial.printf("ERR %s: bilinmeyen %s '%s' (%s mi demek istediniz? help)\n", what, what, w, s);
  else Serial.printf("ERR %s: bilinmeyen %s '%s' (help)\n", what, what, w);
}

void begin() { s_len = 0; }

void printStats() {
  Snapshot s;
  bool ok = g_snap.read(s);
  const fv::Params& p = g_engine.params;
  uint32_t dspAvg = g_stats.dspUsAvg, dspMax = g_stats.dspUsMax;
  Serial.printf(
      "{\"fs\":%.2f,\"afps\":%.1f,\"dfps\":%.1f,\"dsp_us\":%lu,\"dsp_max_us\":%lu,\"dsp_alarm\":%d,"
      "\"render_us\":%lu,\"render_max_us\":%lu,\"frame_us\":%lu,\"stripes_sent\":%lu,\"stripes_skipped\":%lu,"
      "\"leds\":%u,\"ma\":%u,\"limiter\":%u,\"mode\":%ld,\"amode\":%u,\"theme\":%ld,\"bri\":%ld,\"gain\":%ld,\"bfp\":%d,"
      "\"ceiling_db\":%.1f,\"beats\":%lu,\"bpm\":%u,\"silent\":%u,\"clip\":%lu,\"clip1s\":%lu,\"dc_raw\":%u,"
      "\"adc_ovf\":%lu,\"adc_bad\":%lu,\"adc_to\":%lu,\"rmt_ok\":%lu,\"rmt_busy\":%lu,\"rmt_fail\":%lu,\"spi_err\":%lu,"
      "\"budget\":%u,\"demand\":%u,\"ma_peak\":%lu,\"lim_pct\":%lu,\"lim_frames\":%lu,\"src\":\"%s\","
      "\"spi_mhz\":%d,\"heap\":%lu,\"heap_min\":%lu,\"stack_free\":[%u,%u,%u],\"up\":%lu,\"snap\":%d}\n",
      g_stats.fsMilli / 1000.0, g_stats.afps10 / 10.0, g_stats.dfps10 / 10.0, (unsigned long)dspAvg,
      (unsigned long)dspMax, dspMax > 2000 ? 1 : 0, (unsigned long)g_stats.renderUsAvg,
      (unsigned long)g_stats.renderUsMax, (unsigned long)g_stats.frameUsAvg, (unsigned long)g_stats.stripesSent,
      (unsigned long)g_stats.stripesSkipped, s.nLeds, s.ma, s.limiter, (long)p.get(fv::P_LED_MODE), s.activeMode,
      (long)p.get(fv::P_THEME), (long)p.get(fv::P_BRIGHTNESS), (long)p.get(fv::P_GAIN_DB), s.f.bfpShift,
      s.f.ceiling / 85.0403, (unsigned long)g_stats.beats, s.f.bpm, s.f.silent,
      (unsigned long)g_engine.decim().clipCount(), (unsigned long)g_stats.clip1s, g_engine.decim().dcRaw(), (unsigned long)g_stats.adcOvf,
      (unsigned long)g_stats.adcBad, (unsigned long)g_stats.adcTimeouts, (unsigned long)g_stats.rmtOk,
      (unsigned long)g_stats.rmtBusy, (unsigned long)g_stats.rmtFail, (unsigned long)g_stats.spiErr,
      s.budgetMa, s.demandMa, (unsigned long)g_stats.maPeak, (unsigned long)g_stats.limPct, (unsigned long)g_stats.limFrames,
      !powersrc::enabled() ? "off" : powersrc::usb() ? "usb" : "adp",
      controls::displayConfig().mhz, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
      audio::stackFree(), display::handle() ? (unsigned)uxTaskGetStackHighWaterMark(display::handle()) : 0u,
      (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned long)(millis() / 1000), ok ? 1 : 0);
}

static void printStream() {
  uint32_t q = g_snap.seq();
  if (q == s_streamSeq) return;
  s_streamSeq = q;
  Snapshot s;
  if (!g_snap.read(s)) return;
  char buf[16 + fv::NUM_BANDS * 2 + 48];
  int n = snprintf(buf, sizeof(buf), "F %lu %d %u %u ", (unsigned long)s.f.frame, s.f.bfpShift, s.f.beat, s.ma);
  static const char hx[] = "0123456789abcdef";
  for (int b = 0; b < fv::NUM_BANDS; ++b) {
    uint8_t v = s.f.bar[b] >> 8;
    buf[n++] = hx[v >> 4];
    buf[n++] = hx[v & 15];
  }
  buf[n] = 0;
  Serial.println(buf);
}

// Teşhis: tek satır JSON — bant başına ham seviye, gürültü tabanı (dBFS×10), normalize seviye ve bar (0..255).
static void printBands() {
  Snapshot s;
  if (!g_snap.read(s)) { err("bands", "snapshot okunamadı"); return; }
  auto d10 = [](fv::lvl l) { return (long)(l <= fv::LVL_NEG_INF / 2 ? -9990 : (l * 10 + (l >= 0 ? 42 : -42)) / 85); };
  Serial.printf("{\"bands\":1,\"ceil\":%ld,\"bfp\":%d,\"raw\":[", d10(s.f.ceiling), s.f.bfpShift);
  for (int b = 0; b < fv::NUM_BANDS; ++b) Serial.printf(b ? ",%ld" : "%ld", d10(s.f.raw[b]));
  Serial.print("],\"noise\":[");
  for (int b = 0; b < fv::NUM_BANDS; ++b) Serial.printf(b ? ",%ld" : "%ld", d10(s.f.noise[b]));
  Serial.print("],\"level\":[");
  for (int b = 0; b < fv::NUM_BANDS; ++b) Serial.printf(b ? ",%u" : "%u", s.f.level[b] >> 8);
  Serial.print("],\"bar\":[");
  for (int b = 0; b < fv::NUM_BANDS; ++b) Serial.printf(b ? ",%u" : "%u", s.f.bar[b] >> 8);
  Serial.printf("],\"hz\":[");
  for (int b = 0; b < fv::NUM_BANDS; ++b) {
    uint32_t lo = s.f.edge[b], hi = s.f.edge[b + 1];
    Serial.printf(b ? ",%lu" : "%lu", (unsigned long)(((lo + hi) * fv::FS) / (2 * fv::FFT_N)));
  }
  Serial.print("],\"edge\":[");  // bant sınırları (FFT kutusu, 23,4375 Hz/kutu): bant b = [edge[b], edge[b+1])
  for (int b = 0; b <= fv::NUM_BANDS; ++b) Serial.printf(b ? ",%u" : "%u", (unsigned)s.f.edge[b]);
  Serial.printf("],\"bass\":%ld,\"bassN\":%ld}\n", d10(s.f.bassRaw), d10(s.f.bassNoise));
}

// LED test desenleri (veri hattı teşhisi). Desenler düşük parlaklıkta; ayrıca audio görevi override'ları `ma` bütçesine kısar (uzun şeritte ledtest 4 bile bütçeyi aşardı). Aktif `leds` uzunluğunda.
static fv::Rgb s_ltBuf[fv::LED_MAX];
static int s_lt = 0;            // 0 = kapalı (normal)
static int s_ltN = 0;           // desenin kurulduğu uzunluk
static bool s_ltAuto = false;
static uint32_t s_ltAt = 0, s_ltDot = 0;
static const char* const kLtName[] = {"normal", "kirmizi", "yesil", "mavi", "beyaz", "isaretler", "gokkusagi", "gezen-nokta"};
static constexpr int LT_COUNT = 8;

static void ledtestFill(int k) {
  const int n = g_engine.leds().count();
  s_ltN = n;
  for (int i = 0; i < fv::LED_MAX; ++i) {
    fv::Rgb c{0, 0, 0};
    if (i < n) switch (k) {
      case 1: c = {48, 0, 0}; break;
      case 2: c = {0, 48, 0}; break;
      case 3: c = {0, 0, 48}; break;
      case 4: c = {32, 32, 32}; break;
      case 5:  // ilk LED kırmızı, son LED mavi, her 10. LED beyaz → kayma/eksik LED görünür
        if (i == 0) c = {64, 0, 0};
        else if (i == n - 1) c = {0, 0, 64};
        else if (i % 10 == 0) c = {40, 40, 40};
        break;
      case 6: c = fv::rgb_scale(fv::hsv8((uint8_t)(i * 255 / n), 255, 255), 64); break;
      case 7: if ((uint32_t)i == s_ltDot % (uint32_t)n) c = {64, 64, 64}; break;
    }
    s_ltBuf[i] = c;
  }
}

static void ledtestSet(int k) {
  s_lt = k;
  if (k <= 0) { audio::ledOverride(nullptr); return; }
  ledtestFill(k);
  audio::ledOverride(s_ltBuf);
}

static bool parseInt(const char* s, int32_t* out) {
  if (!s || !*s) return false;
  char* e = nullptr;
  long v = strtol(s, &e, 0);
  if (e == s || *e) return false;
  *out = (int32_t)v;
  return true;
}

static bool ieq(const char* a, const char* b) {
  for (; *a && *b; ++a, ++b) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

// "3000", "3s", "500ms", "2m" → ms
static bool parseDur(const char* s, uint32_t* out) {
  if (!s || !*s) return false;
  char* e = nullptr;
  long v = strtol(s, &e, 10);
  if (e == s || v < 0) return false;
  if (!*e || !strcmp(e, "ms")) *out = (uint32_t)v;
  else if (!strcmp(e, "s")) *out = (uint32_t)v * 1000u;
  else if (!strcmp(e, "m")) *out = (uint32_t)v * 60000u;
  else return false;
  return true;
}

// mode/theme/pal adla da verilebilir (mode fire, pal okyanus).
static const char* namedValue(fv::ParamId id, int i) {
  if (id == fv::P_LED_MODE) return fv::led_mode_name((uint8_t)i);
  if (id == fv::P_THEME) return fv::theme_name((uint8_t)i);
  if (id == fv::P_PAL) return fv::led_palette_name((uint8_t)i);
  return nullptr;
}

static void printParam(fv::ParamId id) {
  int32_t v = g_engine.params.get(id);
  const char* name = fv::param_info(id).name;
  if (const char* nv = namedValue(id, (int)v)) Serial.printf("%s=%ld (%s)\n", name, (long)v, nv);
  else Serial.printf("%s=%ld\n", name, (long)v);
}

static bool parseParamValue(fv::ParamId id, const char* arg, int32_t* v) {
  if (id == fv::P_GAIN_DB && ieq(arg, "auto")) { *v = -128; return true; }
  if ((ieq(arg, "off") || ieq(arg, "kapali")) && fv::param_info(id).min == 0) { *v = 0; return true; }
  if (namedValue(id, 0)) {
    for (int i = fv::param_info(id).min; i <= fv::param_info(id).max; ++i)
      if (ieq(arg, namedValue(id, i))) { *v = i; return true; }
  }
  return parseInt(arg, v);
}

static bool isPowerParam(fv::ParamId id) {
  return id == fv::P_LEDS || id == fv::P_MA_BUDGET || id == fv::P_LED_MA || id == fv::P_LED_IDLE || id == fv::P_PSU_MA ||
         id == fv::P_PSU_PCT || id == fv::P_MA_USB;
}

// Güç ayarı tutarlılık denetimi: yalnız uyarır, değeri değiştirmez (limiter her durumda güvenli kalır).
static void powerChecks(fv::ParamId id) {
  const fv::Params& p = g_engine.params;
  const long n = p.get(fv::P_LEDS), ma = p.get(fv::P_MA_BUDGET), psu = p.get(fv::P_PSU_MA), pct = p.get(fv::P_PSU_PCT);
  const long idle = p.get(fv::P_LED_IDLE), lma = p.get(fv::P_LED_MA), usb = p.get(fv::P_MA_USB);
  fv::BudgetSrc src;
  const long eff = (long)fv::led_budget_ma(p, &src);
  const long base = n * idle / 1000;
  if (eff <= base)
    Serial.printf("WARN etkin bütçe %ld mA <= bekleme akımı %ld mA (leds=%ld, led_idle=%ld uA): şerit tamamen siyah; ma/psu_ma'yı artırın ya da leds'i azaltın\n",
                  eff, base, n, idle);
  if ((id == fv::P_MA_BUDGET || id == fv::P_PSU_MA || id == fv::P_PSU_PCT) && psu > 0 && src == fv::BS_PSU)
    Serial.printf("NOT etkin bütçe %ld mA = psu_ma(%ld) x %%%ld; ma=%ld bunun üstüne çıkamaz\n", eff, psu, pct, ma);
  if (id == fv::P_PSU_PCT && psu == 0) Serial.println(F("NOT psu_pct yalnız psu_ma > 0 iken etkili (kaynak kapasitesi henüz girilmedi)"));
  if (id == fv::P_PSU_PCT && pct > 90) Serial.println(F("WARN psu_pct > 90: kaynağı sınırında çalıştırmak ısınma ve voltaj düşümü riski taşır; ESP32/ekran akımı da aynı kaynaktaysa ~0,2 A pay bırakın"));
  if (id == fv::P_LED_MA && lma < 60)
    Serial.printf("NOT led_ma=%ld: model LED başına %ld mA varsayar (veri sayfası 60). Değer gerçek tam-beyaz akımdan düşükse koruma gevşer; etiketten hesaplayın (W/m / 5 V / LED/m) ya da ampermetreyle ölçün\n", lma, lma);
  if (id == fv::P_MA_USB && !powersrc::enabled())
    Serial.println(F("NOT ma_usb yalnız adaptör algılama açıkken etkilidir (config.h: CFG_ADP_DETECT 1 + plaket donanımı); şu an etkisiz"));
  if (id == fv::P_MA_USB && usb > 500) Serial.println(F("WARN ma_usb > 500: USB 2.0 portları genelde 500 mA verir; ESP32/ekran payını da düşün"));
  if (id == fv::P_MA_BUDGET && ma > 5000) Serial.println(F("NOT ma > 5000 mA: şerit ve kablo kesitinin bu akıma uygun olduğundan emin olun (ek besleme noktaları)"));
}

static void printPower() {
  const fv::Params& p = g_engine.params;
  Snapshot s;
  const bool ok = g_snap.read(s);
  fv::BudgetSrc src;
  const long eff = (long)fv::led_budget_ma(p, &src);
  const long psu = p.get(fv::P_PSU_MA), pct = p.get(fv::P_PSU_PCT);
  Serial.printf("model : %ld mA/LED tam beyaz, %ld uA/LED bekleme, %ld LED -> tam beyaz ~%ld mA\n", (long)p.get(fv::P_LED_MA),
                (long)p.get(fv::P_LED_IDLE), (long)p.get(fv::P_LEDS),
                (long)(p.get(fv::P_LEDS) * p.get(fv::P_LED_IDLE) / 1000 + p.get(fv::P_LEDS) * p.get(fv::P_LED_MA)));
  Serial.printf("bütçe : ma=%ld", (long)p.get(fv::P_MA_BUDGET));
  if (psu > 0) Serial.printf("  psu=%ld x %%%ld = %ld", psu, pct, psu * pct / 100);
  else Serial.print(F("  psu=belirtilmedi"));
  if (powersrc::enabled()) Serial.printf("  usb=%ld (%s)", (long)p.get(fv::P_MA_USB), powersrc::usb() ? "şu an USB" : "şu an adaptör");
  else Serial.print(F("  usb=algılama kapalı"));
  Serial.printf("  => ETKİN %ld mA (sınırı koyan: %s)\n", eff, src == fv::BS_PSU ? "psu" : src == fv::BS_USB ? "usb" : "ma");
  if (ok)
    Serial.printf("şimdi : %u mA (talep %u mA, limiter %u/255)   tepe: %lu mA (talep tepesi %lu mA)\n", s.ma, s.demandMa, s.limiter,
                  (unsigned long)g_stats.maPeak, (unsigned long)g_stats.demandPeak);
  Serial.printf("koruma: son 1 s'de talep bütçeyi aşan kare %%%lu, toplam %lu kare, AKIM SINIRI uyarısı %lu kez\n",
                (unsigned long)g_stats.limPct, (unsigned long)g_stats.limFrames, (unsigned long)g_stats.limTrips);
  if (g_stats.limPct >= 50) Serial.println(F("WARN limiter sürekli devrede: istenen parlaklık bütçeyi aşıyor; bri/led_ae'yi düşürün ya da bütçeyi (ma/psu_ma/led_ma) gerçek donanıma göre ayarlayın"));
  Serial.println(F("NOT bunlar MODEL tahminidir; gerçek akım ölçülmez. led_ma gerçek şeridinizle uyuşmuyorsa değerler yanıltır (ampermetre/USB güç ölçer ile doğrulayın)."));
}

// Parametre komutu: argüman yoksa okur, varsa yazar. Yazılan değer kalıcıdır (5 s sonra NVS); geçici değişim için ramp/sweep.
static void paramCmd(fv::ParamId id, const char* arg) {
  const fv::ParamInfo& in = fv::param_info(id);
  if (!arg) { printParam(id); return; }
  int32_t v;
  if (!parseParamValue(id, arg, &v)) { err(in.name, "değer bekleniyor (%ld..%ld): %s", (long)in.min, (long)in.max, arg); return; }
  if (v < in.min || v > in.max) Serial.printf("WARN %s aralığı %ld..%ld, kırpıldı\n", in.name, (long)in.min, (long)in.max);
  controls::setParam(id, v, true);
  Serial.print("OK ");
  printParam(id);
  if (isPowerParam(id)) powerChecks(id);
  if (id == fv::P_LEDS) {
    const int32_t n = g_engine.params.get(fv::P_LEDS);
    const fv::PowerModel pm = fv::power_model(g_engine.params);
    Serial.printf("NOT leds=%ld: LED durumu sıfırlandı; tam beyaz ~%lu mA (model), etkin bütçe %ld mA (limiter sınırlar)\n", (long)n,
                  (unsigned long)((uint32_t)n * pm.idleUa / 1000u + (uint32_t)n * pm.ledMa), (long)fv::led_budget_ma(g_engine.params));
    if (n > 120) Serial.println(F("NOT 2 m'den uzun şeritte ek besleme şart: şeridin ucuna da 5 V/GND bağlayın (voltaj düşümü; bkz. kılavuz)"));
  } else if (id == fv::P_FMIN || id == fv::P_FMAX) {
    Serial.println(F("NOT bant düzeni ve gürültü tabanları yeniden başladı: sessizlikte 'cal' (ya da HASH 3 s) ile kalibre edin"));
    if (g_engine.params.get(fv::P_AUTOCAL)) Serial.println(F("NOT autocal açık: 3 s sonra otomatik kalibrasyon denenecek (sessiz değilse reddeder; elle: cal)"));
  } else if (id == fv::P_AUTOCAL) {
    if (g_engine.params.get(fv::P_AUTOCAL))
      Serial.println(F("NOT autocal açık: şimdi ve her açılışta 3 s sonra 2 s'lik ölçümle gürültü tabanı kendiliğinden öğrenilir. Müzik/ses/ton/aşırı gürültü varsa REDDEDER (5 s arayla en çok 6 deneme); elle her zaman: cal"));
    else
    Serial.println(F("NOT autocal kapalı: açılışta kalibrasyon yapılmaz; sessizlikte 'cal' (ya da HASH 3 s) ile kalibre edin"));
  }
}

static void printAllParams() {
  Serial.print('{');
  for (int i = 0; i < fv::P_COUNT; ++i)
    Serial.printf("%s\"%s\":%ld", i ? "," : "", fv::param_info((fv::ParamId)i).name, (long)g_engine.params.v[i]);
  st7789::Config c = controls::displayConfig();
  Serial.printf(",\"spi\":%d,\"rot\":%d,\"offx\":%d,\"offy\":%d}\n", c.mhz, c.rot, c.offX, c.offY);
}

// Gruplu insan-okur liste. filtre: grup anahtarı öneki (genel serit ses ekran led renk desen gecis) ya da boş.
static const char* const kGroupKey[fv::PG_COUNT] = {"genel", "serit", "ses", "ekran", "led", "renk", "desen", "gecis"};

static void printParams(const char* filterIn) {
  char fbuf[16] = {};
  const char* filter = nullptr;
  if (filterIn) {
    for (int i = 0; i < 15 && filterIn[i]; ++i) fbuf[i] = (char)tolower((unsigned char)filterIn[i]);
    filter = fbuf;
  }
  int shown = 0;
  for (int g = 0; g < fv::PG_COUNT; ++g) {
    if (filter && strncmp(filter, kGroupKey[g], strlen(filter)) != 0) continue;
    Serial.printf("[%s]\n", fv::param_group_name((uint8_t)g));
    for (int i = 0; i < fv::P_COUNT; ++i) {
      const fv::ParamInfo& in = fv::param_info((fv::ParamId)i);
      if (in.group != g) continue;
      const int32_t v = g_engine.params.v[i];
      const char* nv = namedValue((fv::ParamId)i, (int)v);
      char val[24];
      if (nv) snprintf(val, sizeof(val), "%ld %s", (long)v, nv);
      else snprintf(val, sizeof(val), "%ld", (long)v);
      Serial.printf("  %-11s %-14s %c %5ld..%-5ld vars %-5ld %s%s\n", in.name, val, v != fv::param_default((fv::ParamId)i) ? '*' : ' ', (long)in.min, (long)in.max,
                    (long)fv::param_default((fv::ParamId)i), controls::animTable().active((fv::ParamId)i) ? "[anim] " : "", in.desc);
    }
    ++shown;
  }
  if (!shown) err("params", "bilinmeyen grup '%s' (genel serit ses ekran led renk desen gecis)", filter);
}

static void helpCmd(const char* w) {
  if (!w) { Serial.print(kHelp); return; }
  for (int i = 0; i < kCmdCount; ++i)
    if (!strcmp(w, kCmds[i].name)) { Serial.printf("%s\n  %s\n", kCmds[i].usage, kCmds[i].desc); return; }
  fv::ParamId id;
  if (fv::param_lookup(w, &id)) {
    const fv::ParamInfo& in = fv::param_info(id);
    Serial.printf("%s [%ld..%ld, vars. %ld]  (%s%s)\n  %s\n", in.name, (long)in.min, (long)in.max, (long)fv::param_default(id), fv::param_group_name(in.group),
                  (in.flags & fv::PF_NORAMP) ? "; ramp/sweep yok" : "", in.desc);
    if (!(in.flags & fv::PF_NORAMP)) Serial.printf("  örnek: %s <değer> | ramp %s <hedef> 5s | sweep %s <min> <max> 10s\n", in.name, in.name, in.name);
    else Serial.printf("  örnek: %s <değer>\n", in.name);
    return;
  }
  unknown("komut", w);
}

// ramp / sweep / anim
static void animList() {
  const anim::Table& t = controls::animTable();
  if (!t.count()) { Serial.println(F("anim: çalışan yok")); return; }
  const uint32_t now = millis();
  for (int i = 0; i < anim::kSlots; ++i) {
    const anim::Slot& s = t.slot(i);
    if (s.kind == anim::K_NONE) continue;
    const char* nm = fv::param_info((fv::ParamId)s.param).name;
    const long cur = (long)g_engine.params.v[s.param];
    if (s.kind == anim::K_RAMP)
      Serial.printf("anim: ramp  %s %ld -> %ld, kalan %lu ms (şimdi %ld)\n", nm, (long)s.a, (long)s.b,
                    (unsigned long)(s.dur > now - s.t0 ? s.dur - (now - s.t0) : 0), cur);
    else
      Serial.printf("anim: sweep %s %ld..%ld, periyot %lu ms, %s (şimdi %ld)\n", nm, (long)s.a, (long)s.b, (unsigned long)s.dur,
                    s.shape == anim::SH_SIN ? "sin" : "tri", cur);
  }
}

static void animStop(const char* cmd, anim::Kind k, const char* pname) {
  int id = -1;
  if (pname) {
    fv::ParamId pid;
    if (!fv::param_lookup(pname, &pid)) { unknown("parametre", pname); return; }
    id = pid;
  }
  const int n = controls::animTable().stop(k, id);
  Serial.printf("OK %s stop: %d durdu (değerler olduğu yerde kaldı)\n", cmd, n);
}

static void rampCmd(int argc, char** argv) {
  if (argc >= 2 && !strcmp(argv[1], "stop")) return animStop("ramp", anim::K_RAMP, argc > 2 ? argv[2] : nullptr);
  fv::ParamId id;
  int32_t from, target;
  uint32_t dur;
  if (argc < 4) { err("ramp", "ramp <param> [başlangıç] <hedef> <süre_ms|3s> | ramp stop [param]"); return; }
  if (!fv::param_lookup(argv[1], &id)) { unknown("parametre", argv[1]); return; }
  const bool hasFrom = argc >= 5;   // ramp bri 0 255 3s: 0'dan başlar; başlangıç yoksa şimdiki değerden
  from = g_engine.params.get(id);
  if (hasFrom && !parseParamValue(id, argv[2], &from)) { err("ramp", "başlangıç sayı bekleniyor: %s", argv[2]); return; }
  const char* tArg = argv[hasFrom ? 3 : 2];
  if (!parseParamValue(id, tArg, &target)) { err("ramp", "hedef sayı bekleniyor: %s", tArg); return; }
  if (!parseDur(argv[hasFrom ? 4 : 3], &dur) || dur == 0) { err("ramp", "süre bekleniyor (ms, 3s, 2m): %s", argv[hasFrom ? 4 : 3]); return; }
  const char* e = controls::animTable().ramp(id, from, target, dur, millis());
  if (e) { err("ramp", "%s", e); return; }
  const fv::ParamInfo& in = fv::param_info(id);
  const long f2 = from < in.min ? in.min : from > in.max ? in.max : from;
  Serial.printf("OK ramp %s %ld -> %ld / %lu ms (geçici; 'save' o anki değeri kaydeder)\n", in.name, f2,
                (long)(target < in.min ? in.min : target > in.max ? in.max : target), (unsigned long)dur);
}

static void sweepCmd(int argc, char** argv) {
  if (argc >= 2 && !strcmp(argv[1], "stop")) return animStop("sweep", anim::K_SWEEP, argc > 2 ? argv[2] : nullptr);
  fv::ParamId id;
  int32_t lo, hi;
  uint32_t per;
  if (argc < 5) { err("sweep", "sweep <param> <min> <max> <periyot_ms|10s> [tri|sin] | sweep stop [param]"); return; }
  if (!fv::param_lookup(argv[1], &id)) { unknown("parametre", argv[1]); return; }
  if (!parseParamValue(id, argv[2], &lo) || !parseParamValue(id, argv[3], &hi)) { err("sweep", "min/max sayı bekleniyor"); return; }
  if (!parseDur(argv[4], &per)) { err("sweep", "periyot bekleniyor (ms, 10s, 1m): %s", argv[4]); return; }
  anim::Shape sh = anim::SH_TRI;
  if (argc > 5) {
    if (ieq(argv[5], "sin")) sh = anim::SH_SIN;
    else if (!ieq(argv[5], "tri")) { err("sweep", "biçim tri ya da sin: %s", argv[5]); return; }
  }
  const char* e = controls::animTable().sweep(id, lo, hi, per, sh, millis());
  if (e) { err("sweep", "%s", e); return; }
  Serial.printf("OK sweep %s %s periyot %lu ms (geçici; durdurmak için 'sweep stop %s')\n", fv::param_info(id).name,
                sh == anim::SH_SIN ? "sin" : "tri", (unsigned long)per, fv::param_info(id).name);
}

static void presetCmd(int argc, char** argv) {
  const char* a = argc > 1 ? argv[1] : "list";
  if (!strcmp(a, "list")) { controls::presetPrintList(); return; }
  int32_t slot;
  if (argc < 3 || !parseInt(argv[2], &slot) || slot < 0 || slot >= controls::PRESET_SLOTS) {
    err("preset", "preset save|load|del <0-%d> | preset list", controls::PRESET_SLOTS - 1);
    return;
  }
  if (!strcmp(a, "save")) {
    if (controls::presetSave((int)slot)) Serial.printf("OK preset %ld kaydedildi (görünüm ayarları; leds/ma/fmin/fmax/gain/agcmin/hud hariç)\n", (long)slot);
    else err("preset", "NVS yazılamadı");
  } else if (!strcmp(a, "load")) {
    if (controls::presetLoad((int)slot)) Serial.printf("OK preset %ld yüklendi\n", (long)slot);
    else err("preset", "yuva %ld boş", (long)slot);
  } else if (!strcmp(a, "del") || !strcmp(a, "clear")) {
    controls::presetClear((int)slot);
    Serial.printf("OK preset %ld silindi\n", (long)slot);
  } else {
    err("preset", "bilinmeyen alt komut '%s' (save load del list)", a);
  }
}

static void exec(char* line) {
  char* argv[8] = {};
  int argc = 0;
  for (char* t = strtok(line, " \t"); t && argc < 8; t = strtok(nullptr, " \t")) argv[argc++] = t;
  if (!argc) return;
  for (char* p = argv[0]; *p; ++p) *p = (char)tolower(*p);
  const char* c = argv[0];
  const char* a1 = argc > 1 ? argv[1] : nullptr;

  if (!strcmp(c, "help") || !strcmp(c, "?")) return helpCmd(a1);
  if (!strcmp(c, "ir")) return ir::command(argc, argv);
  if (!strcmp(c, "params")) return printParams(a1);
  if (!strcmp(c, "ramp")) return rampCmd(argc, argv);
  if (!strcmp(c, "sweep")) return sweepCmd(argc, argv);
  if (!strcmp(c, "anim")) {
    if (a1 && !strcmp(a1, "stop")) return animStop("anim", anim::K_NONE, argc > 2 ? argv[2] : nullptr);
    return animList();
  }
  if (!strcmp(c, "preset")) return presetCmd(argc, argv);
  if (!strcmp(c, "palcycle")) {   // paletler kayarak sırayla değişsin (pal_cyc)
    paramCmd(fv::P_PAL_CYC, a1);
    if (a1) Serial.println(F("NOT palcycle <sn>: her periyotta bir sonraki palete kayar; 'pal' başlangıç paleti; 0/off kapatır"));
    return;
  }

  if (!strcmp(c, "spi") || !strcmp(c, "rot") || !strcmp(c, "offs")) {
    st7789::Config cfg = controls::displayConfig();
    int32_t v = 0, v2 = 0;
    if (!a1) {
      int16_t ox, oy;
      st7789::effectiveOffsets(&ox, &oy);
      Serial.printf("spi=%d rot=%d offs=%d,%d (ayar %d,%d; -1=otomatik)\n", cfg.mhz, cfg.rot, ox, oy, cfg.offX, cfg.offY);
      return;
    }
    if (!strcmp(c, "spi")) {
      if (!parseInt(a1, &v) || v < 10 || v > 80) { err("spi", "10..80 MHz"); return; }
      cfg.mhz = v;
    } else if (!strcmp(c, "rot")) {
      if (!parseInt(a1, &v) || v < 0 || v > 3) { err("rot", "0..3"); return; }
      cfg.rot = (uint8_t)v;
    } else if (!strcmp(a1, "auto")) {
      cfg.offX = cfg.offY = -1;
    } else {
      if (argc < 3 || !parseInt(a1, &v) || !parseInt(argv[2], &v2) || v < 0 || v > 80 || v2 < 0 || v2 > 80) {
        err("offs", "offs <x 0..80> <y 0..80> | auto");
        return;
      }
      cfg.offX = (int16_t)v;
      cfg.offY = (int16_t)v2;
    }
    controls::setDisplay(cfg);
    Serial.printf("OK spi=%d rot=%d offx=%d offy=%d\n", cfg.mhz, cfg.rot, cfg.offX, cfg.offY);
    return;
  }

  if (!strcmp(c, "stats")) {
    int32_t hz;
    if (a1 && parseInt(a1, &hz)) {
      s_statsMs = hz > 0 ? 1000 / (hz > 20 ? 20 : hz) : 0;
      s_statsAt = millis();
      if (!s_statsMs) Serial.println(F("OK stats durdu"));
    }
    printStats();
    return;
  }
  if (!strcmp(c, "power")) {
    if (a1 && !strcmp(a1, "reset")) { g_stats.maPeak = 0; g_stats.demandPeak = 0; g_stats.limFrames = 0; g_stats.limTrips = 0; Serial.println(F("OK power tepeleri sıfırlandı")); }
    else if (a1) { err("power", "power | power reset"); return; }
    printPower();
    return;
  }
  if (!strcmp(c, "stream")) {
    int32_t hz = 30;
    if (a1 && !parseInt(a1, &hz)) { err("stream", "stream [hz]"); return; }
    s_streamMs = hz > 0 ? 1000 / (hz > 60 ? 60 : hz) : 0;
    Serial.printf("OK stream %ld Hz (F <kare> <bfp> <beat> <mA> <40×hex bar>)\n", (long)hz);
    return;
  }
  if (!strcmp(c, "ledtest")) {  // ledtest [0-7|auto]: sabit desenler (veri hattı teşhisi)
    int32_t v = 0;
    s_ltAuto = a1 && !strcmp(a1, "auto");
    if (s_ltAuto) v = 1;
    else if (a1 && !parseInt(a1, &v)) { err("ledtest", "0-7|auto"); return; }
    if (v < 0 || v >= LT_COUNT) v = 0;
    s_ltAt = millis();
    ledtestSet((int)v);
    Serial.printf("OK ledtest=%s%s\n", kLtName[v], s_ltAuto ? " (auto, 6 s/desen)" : "");
    return;
  }
  if (!strcmp(c, "bands")) { printBands(); return; }
  if (!strcmp(c, "raw") || !strcmp(c, "rawdump")) {  // teşhis: kırpma / dalga biçimi kanıtı
    const bool dump = c[3] == 'd';
    int32_t n = dump ? 1024 : 4800;
    if (a1 && !parseInt(a1, &n)) { err(c, "%s [n]", c); return; }
    if (n < 16) n = 16;
    if (n > (dump ? 1024 : 96000)) n = dump ? 1024 : 96000;
    static uint16_t dbuf[1024];
    RawStats r;
    if (!audio::captureRaw((uint32_t)n, &r, 4000, dump ? dbuf : nullptr)) { err("raw", "ADC verisi yok"); return; }
    Serial.printf("{\"raw\":1,\"n\":%lu,\"mean\":%.2f,\"rms\":%.2f,\"min\":%u,\"max\":%u,\"clip\":%lu}\n",
                  (unsigned long)r.n, r.mean / 16.0, r.rms16 / 16.0, r.min, r.max, (unsigned long)r.clip);
    if (dump) {
      Serial.print("R ");
      for (uint32_t i = 0; i < r.n; ++i) Serial.printf("%04x", dbuf[i]);
      Serial.println();
    }
    return;
  }
  if (!strcmp(c, "drive")) {  // teşhis: kenar hızı/anlık akım gürültüsü deneyi
    int32_t v = TFT_DRIVE_CAP;
    if (a1 && !parseInt(a1, &v)) { err("drive", "0..3"); return; }
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    gpio_set_drive_capability((gpio_num_t)PIN_TFT_SCK, (gpio_drive_cap_t)v);
    gpio_set_drive_capability((gpio_num_t)PIN_TFT_MOSI, (gpio_drive_cap_t)v);
    gpio_set_drive_capability((gpio_num_t)PIN_TFT_DC, (gpio_drive_cap_t)v);
    Serial.printf("OK drive=%ld\n", (long)v);
    return;
  }
  if (!strcmp(c, "disp")) {  // teşhis: disp 0 = çizim/SPI dur, 1 = devam
    int32_t v = 1;
    if (a1) parseInt(a1, &v);
    display::setPaused(v == 0);
    Serial.printf("OK disp=%d\n", v != 0);
    return;
  }
  if (!strcmp(c, "bl")) {  // teşhis: bl on = PWM olmadan sabit, bl <0-255> = PWM
    int32_t v = TFT_BACKLIGHT;
    if (a1 && !strcmp(a1, "on")) { display::requestBacklight(255, true); Serial.println(F("OK bl=const")); return; }
    if (a1) parseInt(a1, &v);
    display::requestBacklight((uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v));
    Serial.printf("OK bl=%ld\n", (long)v);
    return;
  }
  if (!strcmp(c, "ledoff")) {  // teşhis: ledoff 1 = LED'ler siyah, 0 = normal
    static fv::Rgb black[fv::LED_MAX] = {};
    int32_t v = 1;
    if (a1) parseInt(a1, &v);
    audio::ledOverride(v ? black : nullptr);
    Serial.printf("OK ledoff=%d\n", v != 0);
    return;
  }
  if (!strcmp(c, "src")) {
    int32_t v = -1;
    if (a1 && strcmp(a1, "mic") && !parseInt(a1, &v)) { err("src", "mic|0..6"); return; }
    audio::setSynth(a1 && strcmp(a1, "mic") ? (int)v : -1);
    delay(30);
    int sc = audio::synth();
    Serial.printf("OK src=%s\n", sc < 0 ? "mic" : fv::scenario_name(sc));
    return;
  }
  if (!strcmp(c, "inj")) {  // teşhis: inj <Hz> <dBFS> (tam ölçek sinüse göre, ör. inj 8000 -30); inj 0 = kapalı
    int32_t hz = 0, db = 0;
    if (!a1 || !parseInt(a1, &hz) || hz < 0 || hz > 20000 || (hz > 0 && (argc < 3 || !parseInt(argv[2], &db) || db > 0 || db < -90))) {
      err("inj", "inj <Hz> <dBFS -90..0> | inj 0");
      return;
    }
    const int32_t amp = hz ? (int32_t)(2047.0f * powf(10.0f, db / 20.0f) + 0.5f) : 0;
    audio::setInject((uint32_t)hz, amp);
    Serial.printf("OK inj %ld Hz %ld dBFS (tepe %ld LSB)\n", (long)hz, (long)db, (long)amp);
    return;
  }
  if (!strcmp(c, "skip")) {
    int32_t v = 1;
    if (a1 && !parseInt(a1, &v)) { err("skip", "0|1"); return; }
    display::setSkip(v != 0);
    Serial.printf("OK skip=%d\n", v != 0);
    return;
  }
  if (!strcmp(c, "selftest")) { selftest::run(false); return; }
  if (!strcmp(c, "dsp")) { selftest::dspOnly(); return; }
  if (!strcmp(c, "cal")) { controls::calibrate(); Serial.println(F("OK cal")); return; }
  if (!strcmp(c, "bench")) { selftest::bench(); return; }
  if (!strcmp(c, "save")) { controls::saveNow(true); Serial.println(F("OK save")); return; }
  if (!strcmp(c, "reset")) {
    controls::factoryReset();
    audio::requestReset();
    Serial.println(F("OK reset (varsayılanlar, NVS temizlendi)"));
    return;
  }
  if (!strcmp(c, "reboot")) { Serial.println(F("OK reboot")); Serial.flush(); delay(50); ESP.restart(); return; }

  if (!strcmp(c, "set") || !strcmp(c, "get")) {
    if (!a1) {
      if (c[0] == 'g') printAllParams();   // tek satır JSON: araçlar buna bağlı (tools/*.py)
      else err("set", "set <param> <değer>");
      return;
    }
    fv::ParamId id;
    if (!fv::param_lookup(a1, &id)) { unknown("parametre", a1); return; }
    if (c[0] == 'g') { printParam(id); return; }
    if (argc < 3) { err("set", "set <param> <değer>"); return; }
    paramCmd(id, argv[2]);
    return;
  }
  fv::ParamId pid;   // her parametre doğrudan komuttur: bri 128, leds 60 ...
  if (fv::param_lookup(c, &pid)) return paramCmd(pid, a1);
  unknown("komut", c);
}

void poll() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (s_len > 0) {
        s_line[s_len] = 0;
        s_len = 0;
        exec(s_line);
      }
    } else if (ch == 8 || ch == 127) {
      if (s_len) --s_len;
    } else if (s_len < (int)sizeof(s_line) - 1 && ch >= 32) {
      s_line[s_len++] = (char)ch;
    }
  }
  uint32_t now = millis();
  if (s_statsMs && now - s_statsAt >= s_statsMs) { s_statsAt = now; printStats(); }
  if (s_streamMs && now - s_streamAt >= s_streamMs) { s_streamAt = now; printStream(); }
  if (s_lt == 7) { s_ltDot = now / 60; ledtestFill(7); }   // gezen nokta: ~16 LED/s
  else if (s_lt > 0 && s_ltN != g_engine.leds().count()) ledtestFill(s_lt);   // leds değişti: desen yeni uzunluğa
  if (s_ltAuto && now - s_ltAt >= 6000) {
    s_ltAt = now;
    int k = s_lt + 1;
    if (k >= LT_COUNT) { s_ltAuto = false; k = 0; }
    ledtestSet(k);
    Serial.printf("LEDTEST %s\n", kLtName[k]);
  }
}

}  // namespace cli

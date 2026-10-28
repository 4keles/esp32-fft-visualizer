// selftest.cpp — her adım `TEST <ad> PASS|FAIL|SKIP <detay>` basar, sonunda
// `SELFTEST PASS|FAIL n/m` (m = SKIP olmayan adımlar). Sonuçlar ekranda da gösterilir.
#include "selftest.h"
#include "shared.h"
#include "pins.h"
#include "audio_task.h"
#include "display_task.h"
#include "controls.h"
#include "ws2812.h"
#include "st7789.h"
#include "ir.h"
#include <esp_timer.h>
#include <esp_dsp.h>
#if __has_include(<fv_golden_gen.h>)
#include <fv_golden_gen.h>
#define FV_HAVE_GOLDEN 1
#else
#define FV_HAVE_GOLDEN 0
#endif

namespace selftest {

enum St : uint8_t { PASS = 0, FAIL, SKIP };
static const char* const kSt[3] = {"PASS", "FAIL", "SKIP"};

static char s_lines[13][21];
static const char* s_lp[13];
static int s_nl = 0;
static uint32_t s_returnAt = 0;
static fv::Rgb s_ovr[2][fv::LED_MAX];

static void screenLine(const char* fmt, ...) {
  if (s_nl >= 13) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_lines[s_nl], sizeof(s_lines[0]), fmt, ap);
  va_end(ap);
  s_lp[s_nl] = s_lines[s_nl];
  ++s_nl;
  display::showText(s_lp, s_nl);
}

static void screenReplaceLast(const char* fmt, ...) {
  if (s_nl == 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_lines[s_nl - 1], sizeof(s_lines[0]), fmt, ap);
  va_end(ap);
  display::showText(s_lp, s_nl);
}

static void report(const char* name, St st, const char* detail, int* pass, int* total) {
  Serial.printf("TEST %s %s %s\n", name, kSt[st], detail);
  if (st != SKIP) ++*total;
  if (st == PASS) ++*pass;
  screenReplaceLast("%-12s%s", name, kSt[st]);
}

// Yeni snapshot gelene kadar bekler (en fazla timeoutMs).
static bool nextFrame(uint32_t* lastSeq, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    uint32_t s = g_snap.seq();
    if (s != *lastSeq && !(s & 1)) { *lastSeq = s; return true; }
    delay(2);
  }
  return false;
}

// ---------------------------------------------------------------- DISPLAY
static St stepDisplay(char* d, size_t n) {
  uint32_t e0 = st7789::errors(), x0 = st7789::transfers(), f0 = display::frames();
  display::showTest(fv::TP_COLORBARS);
  delay(1500);
  display::showTest(fv::TP_FRAME);
  delay(1500);
  uint32_t errs = st7789::errors() - e0, xf = st7789::transfers() - x0, fr = display::frames() - f0;
  const st7789::Config& c = st7789::config();
  int16_t ox, oy;
  st7789::effectiveOffsets(&ox, &oy);
  snprintf(d, n, "spi=%dMHz rot=%d offs=%d,%d frames=%lu xfers=%lu err=%lu (geri okuma yok: 1 px çerçeve 4 kenarda görünmeli)",
           c.mhz, c.rot, ox, oy, (unsigned long)fr, (unsigned long)xf, (unsigned long)errs);
  return (display::ok() && errs == 0 && xf >= 10 && fr >= 2) ? PASS : FAIL;
}

// ---------------------------------------------------------------- LED
static St stepLed(char* d, size_t n) {
  const fv::PowerModel pm = fv::power_model(g_engine.params);
  uint32_t budget = (uint32_t)fv::led_budget_ma(g_engine.params);   // etkin bütçe (ma / psu / usb)
  const int nLed = g_engine.leds().count();   // aktif şerit uzunluğu (leds)
  static const fv::Rgb cols[3] = {{128, 0, 0}, {0, 128, 0}, {0, 0, 128}};
  uint32_t ok0 = g_stats.rmtOk, fail0 = g_stats.rmtFail;
  uint32_t maxMa = 0;
  for (int c = 0; c < 3; ++c) {
    for (int pos = 0; pos < nLed; pos += 4) {
      fv::Rgb* f = s_ovr[0];
      for (int i = 0; i < nLed; ++i) {
        int dd = (i - pos + nLed) % nLed;
        f[i] = dd < 10 ? cols[c] : fv::Rgb{0, 0, 0};
      }
      uint32_t ma = fv::led_estimate_ma(f, nLed, pm);
      if (ma > maxMa) maxMa = ma;
      audio::ledOverride(f);  // audio görevi bir sonraki karede gönderir
      uint32_t fr = g_stats.frames;
      delay(17);
      if (g_stats.frames == fr) ws2812::show(f, nLed);  // analiz durmuşsa doğrudan gönder
    }
  }
  // Engine limiter'ı: son kareler bütçe içinde mi
  audio::ledOverride(nullptr);
  uint32_t seq = g_snap.seq(), engMax = 0;
  Snapshot s;
  for (int i = 0; i < 30; ++i) {
    if (!nextFrame(&seq, 100)) break;
    if (g_snap.read(s) && s.ma > engMax) engMax = s.ma;
  }
  int ok = (int)(g_stats.rmtOk - ok0), fail = (int)(g_stats.rmtFail - fail0);
  snprintf(d, n, "rmt ok=%d fail=%d chase_max=%lumA engine_max=%lumA budget=%lumA (görsel: R,G,B kayan segment)",
           ok, fail, (unsigned long)maxMa, (unsigned long)engMax, (unsigned long)budget);
  return (fail == 0 && ok > 0 && maxMa <= budget && engMax <= budget) ? PASS : FAIL;
}

// ---------------------------------------------------------------- MIC
static St stepMic(char* d, size_t n, bool* micOk) {
  RawStats r;
  *micOk = false;
  if (!audio::captureRaw(fv::FS_ADC / 2, &r, 2000)) {
    snprintf(d, n, "ADC verisi yok (adc_continuous çalışmıyor)");
    return FAIL;
  }
  uint32_t meanRaw = (r.mean + 8) / 16;
  uint32_t mv = audio::rawToMv(meanRaw);
  uint32_t mvMin = audio::rawToMv(r.min), mvMax = audio::rawToMv(r.max);
  uint32_t p2p = mvMax - mvMin;
  float rmsLsb = r.rms16 / 16.0f;
  float rmsMv = rmsLsb * 0.80f;  // DB_12 eğimi ≈0,8 mV/LSB
  float v = mv / 1000.0f;
  char adv[96];
  St st;
  if (mv < 300) {
    snprintf(adv, sizeof(adv), "bias %.2f V → mikrofon bağlı değil / OUT pini GPIO34'e bağlı mı? VCC=3V3 mü?", v);
    st = FAIL;
  } else if (mv > 2600) {
    snprintf(adv, sizeof(adv), "bias %.2f V → çok yüksek: MAX4466 5V ile mi besleniyor? 3V3'e bağlayın", v);
    st = FAIL;
  } else if (mv < 1400 || mv > 1900) {
    snprintf(adv, sizeof(adv), "bias %.2f V (beklenen 1,65±0,25 V) → besleme/GND/kabloyu kontrol edin", v);
    st = FAIL;
  } else {
    st = PASS;
    *micOk = true;
    if (r.clip > 0 || p2p > 2800) snprintf(adv, sizeof(adv), "kırpma var → kazanç pot'unu azaltın (saat yönünün tersine)");
    else if (rmsLsb < 2.0f) snprintf(adv, sizeof(adv), "sinyal zayıf → sessiz ortamda normal; müzikte de zayıfsa kazancı artırın");
    else if (rmsLsb > 300.0f) snprintf(adv, sizeof(adv), "sessizde bile rms yüksek → ekran SPI gürültüsü (disp 0 ile karşılaştırın): mikrofon VCC filtresi/kablo");
    else if (p2p > 2000) snprintf(adv, sizeof(adv), "yüksek seviye → kazancı biraz azaltmak iyi olur");
    else snprintf(adv, sizeof(adv), "kazanç uygun");
  }
  snprintf(d, n, "bias=%lumV (%s) rms=%.1fLSB/%.1fmV p2p=%lumV min=%u max=%u clip=%lu n=%lu | %s", (unsigned long)mv,
           audio::caliAvailable() ? "cali" : "yaklaşık", rmsLsb, rmsMv, (unsigned long)p2p, r.min, r.max,
           (unsigned long)r.clip, (unsigned long)r.n, adv);
  return st;
}

// ---------------------------------------------------------------- FS
static St stepFs(char* d, size_t n) {
  // fs her 2 s'de güncellenir; açılıştan hemen sonraysa bekle
  uint32_t t0 = millis();
  while (g_stats.fsMilli == 0 && millis() - t0 < 3000) delay(50);
  double fs = g_stats.fsMilli / 1000.0;
  double err = (fs - fv::FS_ADC) * 100.0 / fv::FS_ADC;
  snprintf(d, n, "fs=%.1fHz hata=%+.2f%% (tolerans ±2%%) afps=%.1f ovf=%lu", fs, err, g_stats.afps10 / 10.0,
           (unsigned long)g_stats.adcOvf);
  if (g_stats.fsMilli == 0) return FAIL;
  return (err > -2.0 && err < 2.0) ? PASS : FAIL;
}

// ---------------------------------------------------------------- DSP
#if FV_HAVE_GOLDEN
static constexpr int DSP_FRAMES = fv::GOLDEN_FRAMES;
static constexpr uint32_t DSP_SEED = fv::GOLDEN_SEED;
#else
static constexpr int DSP_FRAMES = 300;
static constexpr uint32_t DSP_SEED = 1;
#endif

static St stepDsp(char* d, size_t n) {
  uint32_t h[fv::SC_COUNT];
  uint32_t ha[fv::SC_COUNT];
  // Çalışma alanı (~42 KB) statik DRAM yerine heap'te
  void* mem = heap_caps_malloc(sizeof(fv::ScenarioHashWork), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!mem) {
    snprintf(d, n, "heap yetersiz (%u B gerekli)", (unsigned)sizeof(fv::ScenarioHashWork));
    return FAIL;
  }
  fv::ScenarioHashWork* w = new (mem) fv::ScenarioHashWork();
  int64_t t0 = esp_timer_get_time();
  for (int sc = 0; sc < fv::SC_COUNT; ++sc) {
    h[sc] = fv::scenario_hash(*w, (fv::Scenario)sc, DSP_FRAMES, DSP_SEED, fv::GH_FEATURES);
    delay(1);
  }
#if FV_HAVE_GOLDEN
  // GH_ALL: özellikler + LED çıktısı + ekran pikselleri (cihazda tam hat bit-exact kanıtı)
  if (fv::GOLDEN_ALL_STABLE) {
    for (int sc = 0; sc < fv::SC_COUNT; ++sc) {
      ha[sc] = fv::scenario_hash(*w, (fv::Scenario)sc, DSP_FRAMES, DSP_SEED, fv::GH_ALL);
      delay(1);
    }
  }
#endif
  w->~ScenarioHashWork();
  heap_caps_free(mem);
  uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
  int match = 0;
  Serial.printf("DSPHASH frames=%d seed=%lu", DSP_FRAMES, (unsigned long)DSP_SEED);
  for (int sc = 0; sc < fv::SC_COUNT; ++sc) {
#if FV_HAVE_GOLDEN
    bool m = h[sc] == fv::GOLDEN_FEATURES[sc];
    match += m;
    Serial.printf(" %s=0x%08lx%s", fv::scenario_name(sc), (unsigned long)h[sc], m ? "" : "!");
#else
    Serial.printf(" %s=0x%08lx", fv::scenario_name(sc), (unsigned long)h[sc]);
#endif
  }
  Serial.println();
#if FV_HAVE_GOLDEN
  int matchAll = 0;
  if (fv::GOLDEN_ALL_STABLE) {
    Serial.printf("DSPHASH_ALL frames=%d", DSP_FRAMES);
    for (int sc = 0; sc < fv::SC_COUNT; ++sc) {
      bool m = ha[sc] == fv::GOLDEN_ALL[sc];
      matchAll += m;
      Serial.printf(" %s=0x%08lx%s", fv::scenario_name(sc), (unsigned long)ha[sc], m ? "" : "!");
    }
    Serial.println();
    snprintf(d, n, "özellik %d/%d, tam hat (LED+ekran) %d/%d golden ile eşleşti (%d kare, %lu ms)", match,
             (int)fv::SC_COUNT, matchAll, (int)fv::SC_COUNT, DSP_FRAMES, (unsigned long)ms);
    return (match == fv::SC_COUNT && matchAll == fv::SC_COUNT) ? PASS : FAIL;
  }
  snprintf(d, n, "%d/%d senaryo golden hash ile eşleşti (%d kare, %lu ms)", match, (int)fv::SC_COUNT, DSP_FRAMES,
           (unsigned long)ms);
  return match == fv::SC_COUNT ? PASS : FAIL;
#else
  snprintf(d, n, "fv_golden_gen.h yok; hash'ler DSPHASH satırında (%d kare, %lu ms)", DSP_FRAMES, (unsigned long)ms);
  return SKIP;
#endif
}

// ---------------------------------------------------------------- INTERFERENCE
// ms boyunca bas kanalının (47-150 Hz kutuları; görüntü aralığından bağımsız) ortalama ham seviyesi (lvl).
// flash: LED'ler 2 karede bir beyaz/siyah.
static bool bassLevel(uint32_t ms, bool flash, uint8_t white, double* out) {
  for (int i = 0; i < fv::LED_MAX; ++i) { s_ovr[0][i] = {0, 0, 0}; s_ovr[1][i] = {white, white, white}; }
  audio::ledOverride(s_ovr[0]);
  uint32_t seq = g_snap.seq();
  Snapshot s;
  double acc = 0;
  int cnt = 0, k = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (!nextFrame(&seq, 100)) continue;
    ++k;
    if (flash) audio::ledOverride(s_ovr[(k >> 1) & 1]);
    if (millis() - t0 < 200) continue;  // geçişi atla
    if (!g_snap.read(s)) continue;
    acc += s.f.bassRaw < -(1 << 20) ? -(1 << 20) : s.f.bassRaw;
    ++cnt;
  }
  audio::ledOverride(nullptr);
  if (!cnt) return false;
  *out = acc / cnt;
  return true;
}

static St stepInterference(char* d, size_t n, bool micOk) {
  if (!micOk) {
    snprintf(d, n, "WARN mikrofon yok/arızalı → ölçüm anlamsız, atlandı");
    return SKIP;
  }
  const fv::PowerModel pm = fv::power_model(g_engine.params);
  const int32_t budget = fv::led_budget_ma(g_engine.params);
  // n LED beyaz (v,v,v): n·idle + n·3·v·led_ma/765 <= bütçe  ⇒  v = (bütçe − n·idle)·255/(n·led_ma)
  const int nLed = g_engine.leds().count();
  const int32_t base = (int32_t)((uint32_t)nLed * pm.idleUa / 1000u);
  int32_t w = (int32_t)(((int32_t)budget - base) * 255 / ((int32_t)nLed * (int32_t)pm.ledMa));
  uint8_t white = (uint8_t)(w > 255 ? 255 : (w < 0 ? 0 : w));
  double off, on;
  if (!bassLevel(1200, false, white, &off) || !bassLevel(1200, true, white, &on)) {
    snprintf(d, n, "analiz karesi gelmiyor");
    return FAIL;
  }
  double offDb = off / 85.0403, onDb = on / 85.0403, delta = onDb - offDb;
  uint32_t ma = (uint32_t)base + (uint32_t)nLed * 3 * white * pm.ledMa / 765u;
  snprintf(d, n, "bas(<=%dHz kanalı) kapalı=%.1fdB yanıp-sönen=%.1fdB fark=%+.1fdB (beyaz=%u, ~%lumA) eşik<3dB",
           fv::BASS_F_MAX, offDb, onDb, delta, white, (unsigned long)ma);
  return delta < 3.0 ? PASS : FAIL;
}

static St stepInput(char* d, size_t n, bool interactive) {
  if (!interactive) {
    snprintf(d, n, "noninteractive: INPUT atlandı");
    return SKIP;
  }
  display::feedback("Yeni OK tuşuna basın", true);
  Serial.println("INPUT: Yeni OK tuşuna basın (5 s)");
  const bool ok = ir::waitForOk(5000);
  display::feedback(nullptr);
  snprintf(d, n, "IR OK %s", ok ? "alındı" : "alınamadı (5 s / alıcı kullanılamıyor)");
  return ok ? PASS : FAIL;
}

// ----------------------------------------------------------------
bool run(bool fromButton) {
  if (controls::standby()) {
    Serial.println(F("ERR selftest: bekleme modu; önce 7'yi 2 s basılı tutun veya BOOT'a basın"));
    return false;
  }
  ir::setTesting(true);
  char d[200];
  int pass = 0, total = 0;
  bool micOk = false;
  s_nl = 0;
  s_returnAt = 0;
  Serial.println(F("SELFTEST BAŞLA"));

  // DISPLAY: önce desenler, sonra metin sayfası
  St st = stepDisplay(d, sizeof(d));
  screenLine("SELFTEST");
  screenLine("DISPLAY ...");
  report("DISPLAY", st, d, &pass, &total);

  screenLine("LED ...");
  st = stepLed(d, sizeof(d));
  report("LED", st, d, &pass, &total);

  screenLine("MIC ...");
  st = stepMic(d, sizeof(d), &micOk);
  report("MIC", st, d, &pass, &total);

  screenLine("FS ...");
  st = stepFs(d, sizeof(d));
  report("FS", st, d, &pass, &total);

  screenLine("DSP ...");
  st = stepDsp(d, sizeof(d));
  report("DSP", st, d, &pass, &total);

  screenLine("INTERFERENCE ...");
  st = stepInterference(d, sizeof(d), micOk);
  report("INTERFERENCE", st, d, &pass, &total);

  screenLine("INPUT ...");
  st = stepInput(d, sizeof(d), fromButton);
  report("INPUT", st, d, &pass, &total);

  bool ok = pass == total;
  Serial.printf("SELFTEST %s %d/%d\n", ok ? "PASS" : "FAIL", pass, total);
  screenLine("");
  screenLine("SONUC %s %d/%d", ok ? "PASS" : "FAIL", pass, total);
  s_returnAt = millis() + 8000;
  ir::setTesting(false);
  return ok;
}

bool dspOnly() {
  char d[200];
  int pass = 0, total = 0;
  St st = stepDsp(d, sizeof(d));
  Serial.printf("TEST DSP %s %s\n", kSt[st], d);
  (void)pass; (void)total;
  return st != FAIL;
}

void poll() {
  if (s_returnAt && (int32_t)(millis() - s_returnAt) >= 0) {
    s_returnAt = 0;
    display::showNormal();
  }
}

// ---------------------------------------------------------------- BENCH
// Bench nesneleri büyük (ScreenRenderer ~27 KB, Engine ~15 KB): statik DRAM yerine
// yalnız ölçüm süresince heap'te tutulur.
struct BenchWork {
  fv::Engine eng;
  fv::Synth syn;
  fv::ScreenRenderer scr;
  uint16_t raw[fv::ADC_BLOCK];
  uint16_t stripe[fv::SCREEN_W * fv::STRIPE_ROWS];
  fv::cq15 fft[fv::FFT_N / 2];
  float f[fv::FFT_N] __attribute__((aligned(16)));
  fv::Decimator dec;
  fv::LedEngine led;
  fv::q15 pcm[fv::ADC_BLOCK / 2];
  fv::q15 frame[fv::FFT_N];
};

void bench() {
  void* mem = heap_caps_aligned_alloc(16, sizeof(BenchWork), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!mem) {
    Serial.printf("{\"bench\":0,\"error\":\"heap yetersiz (%u B gerekli, en büyük blok %u B)\"}\n",
                  (unsigned)sizeof(BenchWork), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return;
  }
  BenchWork& W = *new (mem) BenchWork();
  fv::Engine& s_benchEng = W.eng;
  fv::Synth& s_benchSyn = W.syn;
  fv::ScreenRenderer& s_benchScr = W.scr;
  uint16_t* s_benchRaw = W.raw;
  uint16_t* s_benchStripe = W.stripe;
  fv::cq15* s_benchFft = W.fft;
  float* s_benchF = W.f;
  UBaseType_t prio = uxTaskPriorityGet(nullptr);
  vTaskPrioritySet(nullptr, DISPLAY_PRIO + 1);  // ölçüm sırasında display görevi araya girmesin

  // Engine (desimasyon + analiz + LED adımı), MIX senaryosu
  s_benchEng.params.defaults();
  s_benchEng.reset(1);
  s_benchSyn.reset(fv::SC_MIX, 1, -6);
  uint32_t eSum = 0, eMax = 0, eN = 0, frames = 0;
  for (int i = 0; i < 180; ++i) {
    s_benchSyn.render(s_benchRaw, fv::ADC_BLOCK);
    uint32_t t0 = (uint32_t)esp_timer_get_time();
    int f = s_benchEng.pushAdc(s_benchRaw, fv::ADC_BLOCK);
    uint32_t dt = (uint32_t)esp_timer_get_time() - t0;
    if (i >= 60) { eSum += dt; ++eN; if (dt > eMax) eMax = dt; frames += f; }
  }

  // Aşama ayrıştırması: desimasyon / analyze / leds.step (mod başına)
  W.dec.reset();
  W.syn.reset(fv::SC_MIX, 7, -6);
  uint32_t dcSum = 0, dcN = 0;
  for (int i = 0; i < 60; ++i) {
    W.syn.render(W.raw, fv::ADC_BLOCK);
    uint32_t t0 = (uint32_t)esp_timer_get_time();
    int k = W.dec.process(W.raw, fv::ADC_BLOCK, W.pcm);
    uint32_t dt = (uint32_t)esp_timer_get_time() - t0;
    if (i >= 10) { dcSum += dt; ++dcN; }
    // son 1024 örneği kaydırarak çerçeve oluştur (en eski önce)
    memmove(W.frame, W.frame + k, (fv::FFT_N - k) * sizeof(fv::q15));
    memcpy(W.frame + fv::FFT_N - k, W.pcm, k * sizeof(fv::q15));
  }
  fv::Analyzer& an = W.eng.analyzer();  // ısınmış durum
  uint32_t anSum = 0, anMax = 0;
  for (int i = 0; i < 40; ++i) {
    uint32_t t0 = (uint32_t)esp_timer_get_time();
    an.analyze(W.frame, W.eng.params);
    uint32_t dt = (uint32_t)esp_timer_get_time() - t0;
    anSum += dt; if (dt > anMax) anMax = dt;
  }
  uint32_t ledUs[fv::LM_COUNT];
  fv::Params lp = W.eng.params;
  for (int m = 0; m < fv::LM_COUNT; ++m) {
    lp.set(fv::P_LED_MODE, m);
    W.led.reset(1);
    uint32_t s = 0;
    for (int i = 0; i < 40; ++i) {
      uint32_t t0 = (uint32_t)esp_timer_get_time();
      W.led.step(an.features(), lp);
      uint32_t dt = (uint32_t)esp_timer_get_time() - t0;
      if (i >= 10) s += dt;
    }
    ledUs[m] = s / 30;
  }

  // Tam ekran render (beginFrame + 10 şerit)
  fv::HudInfo hud = {"BENCH", 600, 120, 255};
  uint32_t rSum = 0, rMax = 0;
  for (int i = 0; i < 20; ++i) {
    uint32_t t0 = (uint32_t)esp_timer_get_time();
    s_benchScr.beginFrame(s_benchEng.features(), s_benchEng.params, hud, (uint32_t)i);
    for (int s = 0; s < fv::SCREEN_H / fv::STRIPE_ROWS; ++s)
      s_benchScr.renderRows(s * fv::STRIPE_ROWS, fv::STRIPE_ROWS, s_benchStripe);
    uint32_t dt = (uint32_t)esp_timer_get_time() - t0;
    rSum += dt; if (dt > rMax) rMax = dt;
  }

  // rfft_q15 N=1024
  uint32_t rng = 12345, fSum = 0;
  for (int it = 0; it < 50; ++it) {
    for (int i = 0; i < fv::FFT_N / 2; ++i) {
      s_benchFft[i].re = (int16_t)((int32_t)(fv::xorshift32(rng) & 0x3FFF) - 0x2000);
      s_benchFft[i].im = (int16_t)((int32_t)(fv::xorshift32(rng) & 0x3FFF) - 0x2000);
    }
    uint32_t t0 = (uint32_t)esp_timer_get_time();
    fv::rfft_q15(s_benchFft, fv::FFT_LOG2N);
    fSum += (uint32_t)esp_timer_get_time() - t0;
  }

  // esp-dsp karşılaştırma: 512 noktalı kompleks fc32 + bitrev + split (1024 gerçek eşdeğeri)
  int32_t dspUs = -1;
  if (dsps_fft2r_init_fc32(nullptr, fv::FFT_N) == ESP_OK) {
    uint32_t dSum = 0;
    for (int it = 0; it < 50; ++it) {
      for (int i = 0; i < fv::FFT_N; ++i) s_benchF[i] = (float)((int32_t)(fv::xorshift32(rng) & 0x3FFF) - 0x2000);
      uint32_t t0 = (uint32_t)esp_timer_get_time();
      dsps_fft2r_fc32(s_benchF, fv::FFT_N / 2);
      dsps_bit_rev_fc32(s_benchF, fv::FFT_N / 2);
      dsps_cplx2reC_fc32(s_benchF, fv::FFT_N / 2);
      dSum += (uint32_t)esp_timer_get_time() - t0;
    }
    dsps_fft2r_deinit_fc32();
    dspUs = (int32_t)(dSum / 50);
  }

  // WS2812 kodlama + şerit checksum
  uint32_t t0 = (uint32_t)esp_timer_get_time();
  uint32_t h = 2166136261u;
  const uint32_t* w = (const uint32_t*)s_benchStripe;
  for (int i = 0; i < fv::SCREEN_W * fv::STRIPE_ROWS / 2; ++i) h = (h ^ w[i]) * 16777619u;
  uint32_t ckUs = (uint32_t)esp_timer_get_time() - t0;

  vTaskPrioritySet(nullptr, prio);
  char lb[16 * fv::LM_COUNT];
  int ln = 0;
  for (int m = 0; m < fv::LM_COUNT; ++m)
    ln += snprintf(lb + ln, sizeof(lb) - ln, "%s\"%s\":%lu", m ? "," : "", fv::led_mode_name(m), (unsigned long)ledUs[m]);
  Serial.printf(
      "{\"bench\":1,\"engine_us\":%lu,\"engine_max_us\":%lu,\"engine_frames\":%lu,\"engine_calls\":%lu,"
      "\"render_frame_us\":%lu,\"render_max_us\":%lu,\"rfft1024_us\":%lu,\"esp_dsp_fft1024_us\":%ld,"
      "\"decim800_us\":%lu,\"analyze_us\":%lu,\"analyze_max_us\":%lu,\"leds_us\":{%s},"
      "\"ws_encode_us\":%lu,\"stripe_checksum_us\":%lu,\"h\":%lu,\"cpu_mhz\":%lu}\n",
      (unsigned long)(eN ? eSum / eN : 0), (unsigned long)eMax, (unsigned long)frames, (unsigned long)eN,
      (unsigned long)(rSum / 20), (unsigned long)rMax, (unsigned long)(fSum / 50), (long)dspUs,
      (unsigned long)(dcSum / dcN), (unsigned long)(anSum / 40), (unsigned long)anMax, lb,
      (unsigned long)ws2812::lastEncodeUs(), (unsigned long)ckUs, (unsigned long)(h & 1), (unsigned long)getCpuFrequencyMhz());
  W.~BenchWork();
  heap_caps_free(mem);
}

}  // namespace selftest

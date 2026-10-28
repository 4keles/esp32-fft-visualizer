// audio_task.cpp — ham IDF adc_continuous (analogContinuous ortaladığı için değil),
// fs ölçümü, Engine beslemesi, LED gönderimi, snapshot yayını. Core 0, öncelik 10.
#include "audio_task.h"
#include "shared.h"
#include "pins.h"
#include "ws2812.h"
#include <esp_adc/adc_continuous.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_timer.h>

namespace audio {

static constexpr int FRAME_BYTES = fv::ADC_BLOCK * SOC_ADC_DIGI_RESULT_BYTES;  // 1600 B = 800 örnek
static constexpr int POOL_BYTES = 6400;
static constexpr uint32_t LIM_WARN_PCT = 50;      // 1 s pencerede bu oranın üstünde sınırlama → AKIM SINIRI uyarısı
static constexpr uint32_t CLIP_WARN_SAMPLES = 4;  // 1 s içinde bu kadar kırpılmış örnek → HUD uyarısı
#ifdef FV_ADC_DIAG
// Teşhis: GPIO32 (ADC1_CH4) dahili pull-up ile ~4095 okumalı → ADC veri yolu doğrulaması
#include <driver/rtc_io.h>
static constexpr adc_channel_t ADC_CH = ADC_CHANNEL_4;
#else
static constexpr adc_channel_t ADC_CH = (adc_channel_t)adc1ChannelOfGpio(PIN_MIC);  // config.h CFG_PIN_MIC
#endif

static adc_continuous_handle_t s_adc = nullptr;
static adc_cali_handle_t s_cali = nullptr;
static TaskHandle_t s_task = nullptr, s_disp = nullptr;
static volatile bool s_running = false;
static bool s_standby = false, s_quiesce = false;

static volatile const fv::Rgb* s_ledOvr = nullptr;
// Sentetik kaynak: -1 = mikrofon; aksi halde fv::Scenario (ADC zamanlaması korunur, örnekler sentezden).
static volatile int s_synthReq = -1;
static int s_synthCur = -1;
static fv::Synth s_synth;
// Ton enjeksiyonu (teşhis): gerçek mikrofon örneklerinin üstüne bilinen seviyede sinüs ekler. Akustik
// kaynak olmadan "bu bant hangi seviyeden görünür" sorusunu gerçek ekran gürültüsü altında ölçmeyi sağlar.
static volatile uint32_t s_injInc = 0;   // faz artışı (2^32 = tam tur @48 kHz); 0 = kapalı
static volatile int32_t s_injAmp = 0;    // tepe genlik (ADC LSB)
static uint32_t s_injPh = 0;
static volatile int32_t s_calReq = 0;
static volatile bool s_resetReq = false;

// Ham istatistik isteği (loop yazar want/active, audio doldurur).
static struct {
  volatile bool active, done;
  uint32_t want, n, clip;
  uint64_t sum, sumsq;
  uint16_t mn, mx;
  uint16_t* dst;  // isteğe bağlı: ilk örnekleri ham olarak da yaz (dalga biçimi dökümü)
} s_cap;

static uint8_t s_rx[FRAME_BYTES];
static uint16_t s_raw[fv::ADC_BLOCK + 2];
static Snapshot s_out;
static void warn(const char* label) {
  snprintf(s_out.warning, sizeof(s_out.warning), "%s", label);
  s_out.warningAt = millis();
}

static bool IRAM_ATTR on_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void*) {
  g_stats.adcOvf = g_stats.adcOvf + 1;
  return false;
}

static void captureFeed(const uint16_t* x, int n) {
  if (!s_cap.active || s_cap.done) return;
  for (int i = 0; i < n && s_cap.n < s_cap.want; ++i) {
    uint32_t v = x[i];
    s_cap.sum += v;
    s_cap.sumsq += (uint64_t)v * v;
    if (v < s_cap.mn) s_cap.mn = v;
    if (v > s_cap.mx) s_cap.mx = v;
    if (v == 0 || v == 4095) s_cap.clip++;
    if (s_cap.dst) s_cap.dst[s_cap.n] = (uint16_t)v;
    s_cap.n++;
  }
  if (s_cap.n >= s_cap.want) s_cap.done = true;
}

// Dışarıdan verilen desenleri (ledtest, selftest, ledoff) etkin akım bütçesine kısar: motorun limiter'ı bu yolu görmez.
// Akım modeli: n·idle + Σ(r+g+b)·led_ma/765 ≤ B  ⇔  Σ ≤ (B − n·idle)·765/led_ma. Bütçe tabana yetmiyorsa şerit siyah.
// Bütçe içindeki desene dokunmaz.
static fv::Rgb s_ovrScaled[fv::LED_MAX];
static const fv::Rgb* limitOverride(const fv::Rgb* src, int n) {
  const uint32_t allowed = fv::led_sum_allowed(n, fv::led_budget_ma(g_engine.params), fv::power_model(g_engine.params));
  uint32_t sum = 0;
  for (int i = 0; i < n; ++i) sum += (uint32_t)src[i].r + src[i].g + src[i].b;
  if (sum <= allowed) return src;
  for (int i = 0; i < n; ++i) {
    s_ovrScaled[i].r = (uint8_t)((uint32_t)src[i].r * allowed / sum);
    s_ovrScaled[i].g = (uint8_t)((uint32_t)src[i].g * allowed / sum);
    s_ovrScaled[i].b = (uint8_t)((uint32_t)src[i].b * allowed / sum);
  }
  return s_ovrScaled;
}

static uint32_t s_winLim = 0, s_winFrames = 0;   // 1 s pencere: sınırlanan / toplam kare (yalnız audio görevi)

// Açılış otomatik kalibrasyonu: durumu her karede yansıt; olay (acSeq) değişince metrikleri ÖNCE, acSeq'i SON yaz.
static void publishAutocal(const fv::AutoCalInfo& ac) {
  g_stats.acState = ac.state;
  static uint8_t seq = 0;
  if (ac.seq == seq) return;
  seq = ac.seq;
  g_stats.acReason = ac.reason;
  g_stats.acTries = ac.tries;
  g_stats.acWin = ac.win;
  g_stats.acLevel10 = ac.level10;
  g_stats.acBass10 = ac.bass10;
  g_stats.acSpread10 = ac.spread10;
  g_stats.acPeak10 = ac.peak10;
  g_stats.acState = ac.state;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  g_stats.acSeq = g_stats.acSeq + 1;
}

static void publish() {
  const fv::Features& f = g_engine.features();
  const fv::LedEngine& le = g_engine.leds();
  const fv::Rgb* px = le.out();
  const fv::Rgb* ovr = (const fv::Rgb*)s_ledOvr;
  const int n = le.count();
  if (ovr) { px = limitOverride(ovr, n); }
  ws2812::show(px, n);  // async; ~3,8 ms/120 LED donanımda (30 µs/LED)
  memcpy(&s_out.f, &f, sizeof(fv::Features));
  memcpy(s_out.leds, px, sizeof(fv::Rgb) * (size_t)n);
  s_out.nLeds = (uint16_t)n;
  s_out.ma = ovr ? (uint16_t)fv::led_estimate_ma(px, n, fv::power_model(g_engine.params)) : le.milliamps();
  s_out.demandMa = ovr ? s_out.ma : le.demandMa();
  s_out.budgetMa = le.budgetMa();
  s_out.limiter = le.limiterQ8();
  if (s_out.ma > g_stats.maPeak) g_stats.maPeak = s_out.ma;
  if (s_out.demandMa > g_stats.demandPeak) g_stats.demandPeak = s_out.demandMa;
  if (!ovr && s_out.demandMa > s_out.budgetMa) { g_stats.limFrames = g_stats.limFrames + 1; ++s_winLim; }   // talep bütçeyi aştı → limiter kıstı
  ++s_winFrames;
  s_out.activeMode = le.activeMode();
  publishAutocal(f.ac);
  g_snap.write(s_out);
  if (f.beat) g_stats.beats = g_stats.beats + 1;
  if (s_disp) xTaskNotifyGive(s_disp);
}

static void task(void*) {
  int carry = -1;           // tek sayıda örnek kalırsa bir sonraki bloğa taşınır
  int warm = 6;             // havuzda biriken eski blokları fs ölçümüne katma
  int64_t fsT0 = 0;
  uint32_t fsN = 0;
  int64_t winT0 = esp_timer_get_time();
  uint32_t winFrames = 0, dspSum = 0, dspCnt = 0, dspMax = 0;
  uint32_t clipPrev = 0;
  uint32_t limHigh = 0;      // üst üste kaç pencerede limiter ağırlıklı etkindi

  for (;;) {
    // Latch entry even if BOOT wakes before a pending ADC read returns.
    if (__atomic_exchange_n(&s_quiesce, false, __ATOMIC_ACQ_REL)) {
      if (s_running) {
        adc_continuous_stop(s_adc);
        adc_continuous_flush_pool(s_adc);
        s_running = false;
        s_cap.active = false;
        static const fv::Rgb black[fv::LED_MAX] = {};
        while (ws2812::busy()) vTaskDelay(1);
        ws2812::show(black, g_engine.params.get(fv::P_LEDS));
        while (ws2812::busy()) vTaskDelay(1);
      }
    }
    if (__atomic_load_n(&s_standby, __ATOMIC_ACQUIRE)) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (!s_running) {
      adc_continuous_flush_pool(s_adc);
      g_engine.discardInput();
      carry = -1;
      warm = 6;
      fsN = 0;
      fsT0 = 0;
      winT0 = esp_timer_get_time();
      winFrames = dspSum = dspCnt = dspMax = 0;
      if (adc_continuous_start(s_adc) != ESP_OK) {
        g_stats.adcBad = g_stats.adcBad + 1;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        continue;
      }
      s_running = true;
    }
    uint32_t len = 0;
    esp_err_t e = adc_continuous_read(s_adc, s_rx, FRAME_BYTES, &len, 200);
    int64_t now = esp_timer_get_time();
    if (e == ESP_ERR_TIMEOUT) { g_stats.adcTimeouts = g_stats.adcTimeouts + 1; continue; }
    if (e != ESP_OK) { g_stats.adcBad = g_stats.adcBad + 1; vTaskDelay(1); continue; }
    g_stats.adcReads = g_stats.adcReads + 1;

    int n = 0;
    if (carry >= 0) { s_raw[n++] = (uint16_t)carry; carry = -1; }
    const uint16_t* w = (const uint16_t*)s_rx;
    int got = 0;
    for (uint32_t i = 0; i < len / 2; ++i) {
      uint16_t v = w[i];
      if ((v >> 12) != ADC_CH) { g_stats.adcBad = g_stats.adcBad + 1; continue; }
      s_raw[n++] = v & 0x0FFF;
      ++got;
    }
    g_stats.adcSamples = g_stats.adcSamples + got;
    if (n & 1) carry = s_raw[--n];

    // Sentetik kaynak (ölçüm/demo): mikrofon olmadan gerçek yük altında FPS ölçmek için.
    int sreq = s_synthReq;
    if (sreq != s_synthCur) {
      s_synthCur = sreq;
      if (sreq >= 0) s_synth.reset((fv::Scenario)sreq, 1, 0);
    }
    if (s_synthCur >= 0 && n > 0) s_synth.render(s_raw, n);
    if (s_injInc && n > 0) {
      const uint32_t inc = s_injInc;
      const int32_t amp = s_injAmp;
      for (int i = 0; i < n; ++i) {
        s_injPh += inc;
        int32_t v = (int32_t)s_raw[i] + ((fv::sin16((uint16_t)(s_injPh >> 16)) * amp) >> 15);
        s_raw[i] = (uint16_t)(v < 0 ? 0 : (v > 4095 ? 4095 : v));
      }
    }

    // fs ölçümü: 2 s pencereler, read dönüş zamanları DMA frame sonuna kilitli.
    if (warm > 0) {
      if (--warm == 0) { fsT0 = now; fsN = 0; }
    } else {
      fsN += got;
      int64_t dt = now - fsT0;
      if (dt >= 2000000) {
        g_stats.fsMilli = (uint32_t)((uint64_t)fsN * 1000000000ull / (uint64_t)dt);
        fsT0 = now;
        fsN = 0;
      }
    }

    captureFeed(s_raw, n);

    if (s_resetReq) { g_engine.reset(1); s_resetReq = false; }
    int32_t cal = s_calReq;
    if (cal > 0) { g_engine.analyzer().calibrateNoise(cal); s_calReq = 0; }

    int frames = 0;
    if (n > 0) {
      uint32_t t0 = (uint32_t)esp_timer_get_time();
      frames = g_engine.pushAdc(s_raw, n);
      uint32_t dt = (uint32_t)esp_timer_get_time() - t0;
      if (frames > 0) {
        dspSum += dt; dspCnt += frames;
        if (dt > dspMax) dspMax = dt;
      }
    }
    if (frames > 0) {
      winFrames += frames;
      g_stats.frames = g_stats.frames + frames;
      publish();
    }

    int64_t wdt = now - winT0;
    if (wdt >= 1000000) {
      g_stats.afps10 = (uint32_t)((uint64_t)winFrames * 10000000ull / (uint64_t)wdt);
      g_stats.dspUsAvg = dspCnt ? dspSum / dspCnt : 0;
      g_stats.dspUsMax = dspMax;
      // Kırpma: sıfırlamada sayaç geri sarar. Kırpılmış sinüs f, 3f, 5f... çizgileri üretir (aynı anda birden çok bar).
      const uint32_t clipNow = g_engine.decim().clipCount();
      g_stats.clip1s = clipNow >= clipPrev ? clipNow - clipPrev : 0;
      clipPrev = clipNow;
      if (g_stats.clip1s >= CLIP_WARN_SAMPLES) warn("KIRPMA: KAZANCI KIS");
      // Yüksek akım koruması uyarısı: limiter son 1 s'nin yarısından fazlasında sınırladıysa (üst üste 2 pencere)
      // model talebi bütçeyi sürekli aşıyor demektir. Tek tük tepe (müzik vuruşu) uyarı vermez.
      g_stats.limPct = s_winFrames ? s_winLim * 100u / s_winFrames : 0;
      if (g_stats.limPct >= LIM_WARN_PCT) {
        if (++limHigh >= 2) { warn("AKIM SINIRI AKTIF"); if (limHigh == 2) g_stats.limTrips = g_stats.limTrips + 1; }
      } else limHigh = 0;
      s_winLim = s_winFrames = 0;
      winT0 = now; winFrames = 0; dspSum = dspCnt = dspMax = 0;
    }
  }
}

static void initCali() {
  adc_cali_line_fitting_config_t cc = {};
  cc.unit_id = ADC_UNIT_1;
  cc.atten = ADC_ATTEN_DB_12;
  cc.bitwidth = ADC_BITWIDTH_12;
  cc.default_vref = 1100;
  if (adc_cali_create_scheme_line_fitting(&cc, &s_cali) != ESP_OK) s_cali = nullptr;
}

bool begin(TaskHandle_t displayTask, bool standby) {
  s_standby = standby;
  s_quiesce = false;
  s_disp = displayTask;
  initCali();

  adc_continuous_handle_cfg_t hc = {};
  hc.max_store_buf_size = POOL_BYTES;
  hc.conv_frame_size = FRAME_BYTES;
  hc.flags.flush_pool = 1;
  if (adc_continuous_new_handle(&hc, &s_adc) != ESP_OK) return false;

  adc_digi_pattern_config_t pat = {};
  pat.atten = ADC_ATTEN_DB_12;
  pat.channel = ADC_CH;
  pat.unit = ADC_UNIT_1;
  pat.bit_width = ADC_BITWIDTH_12;
  adc_continuous_config_t cfg = {};
  cfg.pattern_num = 1;
  cfg.adc_pattern = &pat;
  cfg.sample_freq_hz = fv::FS_ADC;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
  if (adc_continuous_config(s_adc, &cfg) != ESP_OK) return false;

  adc_continuous_evt_cbs_t cbs = {};
  cbs.on_pool_ovf = on_ovf;
  adc_continuous_register_event_callbacks(s_adc, &cbs, nullptr);

  s_running = false;
#ifdef FV_ADC_DIAG
  rtc_gpio_pullup_en(GPIO_NUM_32);
  rtc_gpio_pulldown_dis(GPIO_NUM_32);
#endif
  if (xTaskCreatePinnedToCore(task, "audio", 6144, nullptr, AUDIO_PRIO, &s_task, AUDIO_CORE) != pdPASS) return false;
  if (!standby) {
    const uint32_t start = millis();
    while (!s_running && uint32_t(millis() - start) < 1000) delay(1);
    return s_running;
  }
  return true;
}

void setStandby(bool standby) {
  __atomic_store_n(&s_standby, standby, __ATOMIC_RELEASE);
  if (standby) __atomic_store_n(&s_quiesce, true, __ATOMIC_RELEASE);
  if (s_task) xTaskNotifyGive(s_task);
}

bool captureRaw(uint32_t samples, RawStats* out, uint32_t timeoutMs, uint16_t* dst) {
  s_cap.active = false;
  s_cap.dst = dst;  // samples ≤ dst kapasitesi (çağıran sağlar)
  s_cap.done = false;
  s_cap.n = s_cap.clip = 0;
  s_cap.sum = s_cap.sumsq = 0;
  s_cap.mn = 0xFFFF;
  s_cap.mx = 0;
  s_cap.want = samples;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  s_cap.active = true;
  uint32_t t0 = millis();
  while (!s_cap.done) {
    if (millis() - t0 > timeoutMs) { s_cap.active = false; s_cap.dst = nullptr; return false; }
    delay(10);
  }
  s_cap.active = false;
  s_cap.dst = nullptr;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  uint32_t n = s_cap.n ? s_cap.n : 1;
  double mean = (double)s_cap.sum / n;
  double var = (double)s_cap.sumsq / n - mean * mean;
  if (var < 0) var = 0;
  out->n = s_cap.n;
  out->mean = (uint32_t)(mean * 16.0 + 0.5);
  out->rms16 = (uint32_t)(sqrt(var) * 16.0 + 0.5);
  out->min = s_cap.mn;
  out->max = s_cap.mx;
  out->clip = s_cap.clip;
  return true;
}

void ledOverride(const fv::Rgb* px) { s_ledOvr = px; }
void requestCalibrate(int frames) { s_calReq = frames; }
void requestReset() { s_resetReq = true; }
void setSynth(int sc) { s_synthReq = (sc >= 0 && sc < fv::SC_COUNT) ? sc : -1; }
int synth() { return s_synthCur; }
void setInject(uint32_t hz, int32_t ampLsb) {
  s_injInc = (hz == 0 || ampLsb <= 0) ? 0 : (uint32_t)(((uint64_t)hz << 32) / (uint64_t)fv::FS_ADC);
  s_injAmp = ampLsb;
}
bool caliAvailable() { return s_cali != nullptr; }
unsigned stackFree() { return s_task ? (unsigned)uxTaskGetStackHighWaterMark(s_task) : 0u; }

uint32_t rawToMv(uint32_t raw) {
  if (raw > 4095) raw = 4095;
  int mv = 0;
  if (s_cali && adc_cali_raw_to_voltage(s_cali, (int)raw, &mv) == ESP_OK) return (uint32_t)mv;
  return raw * 3100 / 4095;  // DB_12 için kaba yaklaşım
}

}  // namespace audio

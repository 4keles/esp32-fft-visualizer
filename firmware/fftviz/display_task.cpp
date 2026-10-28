// display_task.cpp — core 1, öncelik 5. İki DMA buffer ping-pong; şerit checksum'ı
// önceki kareyle aynıysa DMA atlanır; ardışık şeritler tek pencerede akar.
#include "display_task.h"
#include "shared.h"
#include "pins.h"
#include "control_text.h"

namespace display {

static constexpr int STRIPES = fv::SCREEN_H / fv::STRIPE_ROWS;  // 10
static constexpr int STRIPE_PX = fv::SCREEN_W * fv::STRIPE_ROWS;
static constexpr uint32_t HUD_MS = 2500;
static constexpr uint32_t BOOT_MS = 1000;

static TaskHandle_t s_task = nullptr;
static bool s_ok = false;
static uint16_t* s_buf[2] = {nullptr, nullptr};
static fv::ScreenRenderer s_r;
static Snapshot s_snap;

static volatile uint8_t s_mode = DM_BOOT;
static volatile uint8_t s_test = 0;
static char s_text[13][21];
static volatile int s_textN = 0;
static volatile uint32_t s_textVer = 0;

static portMUX_TYPE s_cfgMux = portMUX_INITIALIZER_UNLOCKED;
static st7789::Config s_reqCfg;
static volatile bool s_cfgReq = false;
static volatile uint32_t s_frames = 0;
// Checksum ile şerit atlama. Varsayılan KAPALI: atlama SPI trafiğini içeriğe bağımlı yapar (durağan ekran = trafik yok,
// hareketli ekran = tam trafik); ekran SPI'ı ADC'ye ~20 dB gürültü bindirdiğinden taban kalibrasyonu bozuluyor ve
// sessizlikte barlar/LED'ler aniden yanıyordu. Kapalıyken trafik sabit.
static volatile bool s_skipEnabled = false;
static volatile bool s_paused = false;      // teşhis: çizim/SPI tamamen durur
static bool s_power = true;
static bool s_started = false;
static char s_feedback[128] = {};
static uint32_t s_feedbackAt = 0;
static bool s_feedbackPersistent = false;
static uint8_t s_backlight = TFT_BACKLIGHT;
static bool s_backlightConstant = !TFT_BL_PWM, s_backlightReq = false;

// HUD
static char s_hudLabel[32] = {};
static volatile uint32_t s_hudAt = 0;

}  // namespace display

void hud_notify(const char* label) {
  portENTER_CRITICAL(&display::s_cfgMux);
  snprintf(display::s_hudLabel, sizeof(display::s_hudLabel), "%s", label);
  display::s_hudAt = millis();
  portEXIT_CRITICAL(&display::s_cfgMux);
}

namespace display {

static inline uint32_t stripeSum(const uint16_t* p) {
  const uint32_t* w = (const uint32_t*)p;
  uint32_t h = 2166136261u;
  for (int i = 0; i < STRIPE_PX / 2; ++i) h = (h ^ w[i]) * 16777619u;
  return h;
}

static void hudInfo(fv::HudInfo& h, uint32_t now, char* lbl, size_t lblN) {
  int32_t mode = g_engine.params.get(fv::P_HUD);
  portENTER_CRITICAL(&s_cfgMux);
  snprintf(lbl, lblN, "%s", s_hudLabel);
  uint32_t age = now - s_hudAt;
  portEXIT_CRITICAL(&s_cfgMux);
  const uint32_t warningAge = now - s_snap.warningAt;
  if (s_snap.warning[0] && (!lbl[0] || warningAge <= age)) {
    snprintf(lbl, lblN, "%s", s_snap.warning);
    age = warningAge;
  }
  h.fps10 = (uint16_t)g_stats.dfps10;
  h.bpm = s_snap.f.bpm;
  h.label = nullptr;
  h.show = 0;
  if (mode == 2) return;
  if (lbl[0] && age < HUD_MS) {
    h.label = lbl;
    h.show = age > HUD_MS - 300 ? (uint8_t)((HUD_MS - age) * 255 / 300) : 255;
    if (mode == 1) h.show = 255;
  } else if (s_snap.f.ac.state == fv::AC_GAVE_UP) {   // otomatik kalibrasyon vazgeçti: elle kalibre edilene kadar kalıcı ipucu
    h.label = "KALIBRASYON GEREKLI";
    h.show = 255;
  } else if (mode == 1) {
    snprintf(lbl, lblN, "%s", fv::led_mode_name((uint8_t)g_engine.params.get(fv::P_LED_MODE)));
    h.label = lbl;
    h.show = 255;
  }
}

static void task(void*) {
  uint32_t last[STRIPES];
  uint32_t tok[2] = {0, 0};
  bool force = true;
  uint8_t prevMode = 0xFF;
  uint32_t prevTextVer = 0;
  uint32_t t0 = millis();
  uint32_t winT0 = t0, winFrames = 0, rSum = 0, rMax = 0, fSum = 0;
  char hudLbl[24];
  char page[13][21];
  const char* lines[13];
  for (int i = 0; i < 13; ++i) lines[i] = page[i];
  bool initialized = false, awake = false, light = false;
  ControlText prompt;
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, LOW);
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, LOW);

  for (;;) {
    if (!__atomic_load_n(&s_power, __ATOMIC_ACQUIRE)) {
      __atomic_store_n(&s_started, true, __ATOMIC_RELEASE);
      if (awake) { st7789::sleep(); awake = light = false; }
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (!initialized) {
      s_ok = st7789::begin(s_reqCfg) && st7789::errors() == 0;
      initialized = true;
      __atomic_store_n(&s_started, true, __ATOMIC_RELEASE);
      awake = s_ok;
      force = true;
    }
    if (!s_ok) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
    if (!awake) { st7789::wake(); awake = true; force = true; }
    if (s_cfgReq) {
      st7789::Config c;
      portENTER_CRITICAL(&s_cfgMux);
      c = s_reqCfg;
      s_cfgReq = false;
      portEXIT_CRITICAL(&s_cfgMux);
      st7789::waitIdle();
      st7789::reconfigure(c);
      force = true;
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(40));  // yeni snapshot ya da 25 FPS taban
    if (!__atomic_load_n(&s_power, __ATOMIC_ACQUIRE)) continue;
    if (s_paused) { force = true; continue; }
    uint32_t now = millis();
    uint32_t fStart = micros();
    uint8_t mode = s_mode;
    if (mode == DM_BOOT && now - t0 >= BOOT_MS) { s_mode = mode = DM_NORMAL; }
    if (mode != prevMode) { force = true; prevMode = mode; }
    if (mode == DM_TEXT && s_textVer != prevTextVer) { force = true; prevTextVer = s_textVer; }
    char feedback[128];
    portENTER_CRITICAL(&s_cfgMux);
    const bool showFeedback = s_feedback[0] && (s_feedbackPersistent || uint32_t(now - s_feedbackAt) < HUD_MS);
    memcpy(feedback, s_feedback, sizeof(feedback));
    const bool backlightReq = s_backlightReq;
    const int textN = s_textN;
    if (mode == DM_TEXT) memcpy(page, s_text, sizeof(page));
    portEXIT_CRITICAL(&s_cfgMux);
    if (showFeedback) prompt.layout(feedback);

    uint32_t rUs = 0;
    uint32_t tr = micros();
    if (mode == DM_NORMAL) {
      g_snap.read(s_snap);
      fv::HudInfo h;
      hudInfo(h, now, hudLbl, sizeof(hudLbl));
      s_r.beginFrame(s_snap.f, g_engine.params, h, s_snap.f.frame);
    }
    rUs += micros() - tr;

    bool contiguous = false;
    for (int s = 0; s < STRIPES; ++s) {
      int b = s & 1;
      uint16_t* buf = s_buf[b];
      st7789::waitToken(tok[b]);  // bu buffer'ın önceki DMA'sı bitmeli
      tr = micros();
      int y0 = s * fv::STRIPE_ROWS;
      if (showFeedback) prompt.render(y0, fv::STRIPE_ROWS, buf);
      else switch (mode) {
        case DM_NORMAL: s_r.renderRows(y0, fv::STRIPE_ROWS, buf); break;
        case DM_BOOT: s_r.renderTest(fv::TP_BOOT, (now - t0) * 60 / 1000, y0, fv::STRIPE_ROWS, buf); break;
        case DM_TEST: s_r.renderTest(s_test, now * 60 / 1000, y0, fv::STRIPE_ROWS, buf); break;
        default: s_r.renderText(lines, textN, y0, fv::STRIPE_ROWS, buf); break;
      }
      uint32_t sum = stripeSum(buf);
      rUs += micros() - tr;
      if (!force && s_skipEnabled && sum == last[s]) {
        g_stats.stripesSkipped = g_stats.stripesSkipped + 1;
        contiguous = false;
        continue;
      }
      last[s] = sum;
      if (!contiguous) st7789::setWindowRows(y0, fv::SCREEN_H - 1);
      tok[b] = st7789::pushPixels(buf, STRIPE_PX);
      contiguous = true;
      g_stats.stripesSent = g_stats.stripesSent + 1;
    }
    force = false;
    if ((!light || backlightReq) && __atomic_load_n(&s_power, __ATOMIC_ACQUIRE)) {
      st7789::waitIdle();
      portENTER_CRITICAL(&s_cfgMux);
      const uint8_t value = s_backlight;
      const bool constant = s_backlightConstant;
      s_backlightReq = false;
      portEXIT_CRITICAL(&s_cfgMux);
      if (constant) st7789::backlightConst();
      else st7789::backlight(value);
      light = true;
    }
    s_frames = s_frames + 1;
    g_stats.dframes = s_frames;
    g_stats.spiErr = st7789::errors();

    ++winFrames;
    rSum += rUs;
    if (rUs > rMax) rMax = rUs;
    fSum += micros() - fStart;
    if (now - winT0 >= 1000) {
      uint32_t dt = now - winT0;
      g_stats.dfps10 = winFrames * 10000 / dt;
      g_stats.renderUsAvg = rSum / winFrames;
      g_stats.renderUsMax = rMax;
      g_stats.frameUsAvg = fSum / winFrames;
      winT0 = now; winFrames = 0; rSum = rMax = fSum = 0;
    }
  }
}

bool begin(const st7789::Config& cfg, bool power) {
  s_power = power;
  s_started = false;
  s_mode = power ? DM_BOOT : DM_NORMAL;
  for (int i = 0; i < 13; ++i) s_text[i][0] = 0;
  s_reqCfg = cfg;
  for (int i = 0; i < 2; ++i) {
    s_buf[i] = (uint16_t*)heap_caps_malloc(STRIPE_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf[i]) return false;
  }
  if (xTaskCreatePinnedToCore(task, "display", 6144, nullptr, DISPLAY_PRIO, &s_task, DISPLAY_CORE) != pdPASS) return false;
  while (!__atomic_load_n(&s_started, __ATOMIC_ACQUIRE)) delay(1);
  return power ? s_ok : true;
}

TaskHandle_t handle() { return s_task; }
bool ok() { return s_ok; }

void setPower(bool on) {
  __atomic_store_n(&s_power, on, __ATOMIC_RELEASE);
  if (s_task) xTaskNotifyGive(s_task);
}

void feedback(const char* text, bool persistent) {
  portENTER_CRITICAL(&s_cfgMux);
  snprintf(s_feedback, sizeof(s_feedback), "%s", text ? text : "");
  s_feedbackAt = millis();
  s_feedbackPersistent = persistent;
  portEXIT_CRITICAL(&s_cfgMux);
  if (s_task) xTaskNotifyGive(s_task);
}

void requestBacklight(uint8_t value, bool constant) {
  portENTER_CRITICAL(&s_cfgMux);
  s_backlight = value;
  s_backlightConstant = constant;
  s_backlightReq = true;
  portEXIT_CRITICAL(&s_cfgMux);
  if (s_task) xTaskNotifyGive(s_task);
}

void showNormal() { s_mode = DM_NORMAL; }
void showTest(uint8_t pattern) { s_test = pattern; s_mode = DM_TEST; s_textVer = s_textVer + 1; }

void showText(const char* const* lines, int n) {
  if (n > 13) n = 13;
  portENTER_CRITICAL(&s_cfgMux);
  for (int i = 0; i < n; ++i) {
    strncpy(s_text[i], lines[i] ? lines[i] : "", 20);
    s_text[i][20] = 0;
  }
  s_textN = n;
  s_textVer = s_textVer + 1;
  s_mode = DM_TEXT;
  portEXIT_CRITICAL(&s_cfgMux);
}

void requestConfig(const st7789::Config& cfg) {
  portENTER_CRITICAL(&s_cfgMux);
  s_reqCfg = cfg;
  s_cfgReq = true;
  portEXIT_CRITICAL(&s_cfgMux);
}

st7789::Config pendingConfig() {
  portENTER_CRITICAL(&s_cfgMux);
  st7789::Config c = s_reqCfg;
  portEXIT_CRITICAL(&s_cfgMux);
  return c;
}

uint32_t frames() { return s_frames; }
void setSkip(bool en) { s_skipEnabled = en; }
void setPaused(bool p) { s_paused = p; }

}  // namespace display

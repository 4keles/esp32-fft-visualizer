// Physical gestures and shared button/IR actions run on the core1 loop.
#include "controls.h"
#include "shared.h"
#include "pins.h"
#include "display_task.h"
#include "audio_task.h"
#include "selftest.h"
#include "ir.h"
#include <Preferences.h>

namespace controls {
static constexpr uint32_t SAVE_DELAY_MS = 5000;
static Btn s_boot;
static bool s_standby = false, s_displayOn = true, s_bootConsumed = false;
static int8_t s_slots[6] = {-1, -1, -1, -1, -1, -1};
static uint32_t s_standbyAt = 0;

static Preferences s_prefs;
static bool s_prefsOk = false;
static bool s_dirty = false;
static uint32_t s_dirtyAt = 0;
static int32_t s_saved[fv::P_COUNT];
static anim::Table s_anim;
static uint64_t s_taint = 0;   // animasyonla değişmiş, kullanıcı kaydetmemiş parametreler (bit = ParamId)
static_assert(fv::P_COUNT <= 64, "s_taint 64 bit");
static st7789::Config s_disp, s_dispSaved;

static bool pressed() { return digitalRead(PIN_BTN_BOOT) == LOW; }

// config.h → ilk açılış varsayılanları. Aralık dışı değer derlemeyi durdurur (param_set_default sessizce kırpardı).
static_assert(CFG_LEDS >= 1 && CFG_LEDS <= fv::LED_MAX, "CFG_LEDS 1..LED_MAX");
static_assert(CFG_LED_MA >= 5 && CFG_LED_MA <= 100, "CFG_LED_MA 5..100");
static_assert(CFG_LED_IDLE_UA >= 0 && CFG_LED_IDLE_UA <= 5000, "CFG_LED_IDLE_UA 0..5000");
static_assert(CFG_MA_BUDGET >= 100 && CFG_MA_BUDGET <= 20000, "CFG_MA_BUDGET 100..20000");
static_assert(CFG_PSU_MA >= 0 && CFG_PSU_MA <= 30000, "CFG_PSU_MA 0..30000");
static_assert(CFG_PSU_PCT >= 20 && CFG_PSU_PCT <= 100, "CFG_PSU_PCT 20..100");
static_assert(CFG_MA_USB >= 50 && CFG_MA_USB <= 5000, "CFG_MA_USB 50..5000");
static_assert(CFG_DEFAULT_MODE >= 0 && CFG_DEFAULT_MODE < fv::LM_COUNT, "CFG_DEFAULT_MODE 0..8");
static_assert(CFG_DEFAULT_THEME >= 0 && CFG_DEFAULT_THEME < fv::TH_COUNT, "CFG_DEFAULT_THEME 0..4");
static_assert(CFG_DEFAULT_BRIGHTNESS >= 0 && CFG_DEFAULT_BRIGHTNESS <= 255, "CFG_DEFAULT_BRIGHTNESS 0..255");
static_assert(CFG_AUTOCAL_BOOT == 0 || CFG_AUTOCAL_BOOT == 1, "CFG_AUTOCAL_BOOT 0 ya da 1");

static void applyConfigDefaults() {
  fv::param_set_default(fv::P_LEDS, CFG_LEDS);
  fv::param_set_default(fv::P_LED_MA, CFG_LED_MA);
  fv::param_set_default(fv::P_LED_IDLE, CFG_LED_IDLE_UA);
  fv::param_set_default(fv::P_MA_BUDGET, CFG_MA_BUDGET);
  fv::param_set_default(fv::P_PSU_MA, CFG_PSU_MA);
  fv::param_set_default(fv::P_PSU_PCT, CFG_PSU_PCT);
  fv::param_set_default(fv::P_MA_USB, CFG_MA_USB);
  fv::param_set_default(fv::P_LED_MODE, CFG_DEFAULT_MODE);
  fv::param_set_default(fv::P_THEME, CFG_DEFAULT_THEME);
  fv::param_set_default(fv::P_BRIGHTNESS, CFG_DEFAULT_BRIGHTNESS);
  fv::param_set_default(fv::P_AUTOCAL, CFG_AUTOCAL_BOOT);
}

static st7789::Config defaultDisplay() {
  st7789::Config d;
  d.mhz = TFT_SPI_MHZ_DEFAULT;
  d.rot = TFT_ROTATION_DEFAULT & 3;
  d.offX = TFT_OFFSET_X_DEFAULT;
  d.offY = TFT_OFFSET_Y_DEFAULT;
  return d;
}

void begin() {
  pinMode(4, INPUT_PULLDOWN);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  delay(5);
  s_boot = {};
  s_boot.raw = s_boot.stable = pressed();
  s_boot.rawAt = millis();
  s_bootConsumed = false;
}

anim::Table& animTable() { return s_anim; }

bool setParam(fv::ParamId id, int32_t v, bool showHud) {
  s_anim.stop(anim::K_NONE, id);   // elle ayar otomasyonu keser
  s_taint &= ~(1ull << id);
  bool ch = g_engine.params.set(id, v);
  if (ch) markDirty();
  if (showHud) {
    int32_t x = g_engine.params.get(id);
    if (id == fv::P_LED_MODE) hud_notify(fv::led_mode_name((uint8_t)x));
    else if (id == fv::P_THEME) hud_notify(fv::theme_name((uint8_t)x));
    else if (id == fv::P_FMIN || id == fv::P_FMAX) hud_notify("ARALIK DEGISTI: CAL");
  }
  return ch;
}

void selectMode(uint8_t mode) { setParam(fv::P_LED_MODE, mode, true); }
void nextMode(int delta) {
  selectMode((g_engine.params.get(fv::P_LED_MODE) + delta + fv::LM_COUNT) % fv::LM_COUNT);
}
void nextTheme() {
  setParam(fv::P_THEME, (g_engine.params.get(fv::P_THEME) + 1) % fv::TH_COUNT, true);
}
static void setBrightness(int32_t value) {
  setParam(fv::P_BRIGHTNESS, value, false);
  char lbl[16];
  snprintf(lbl, sizeof(lbl), "BRI %d%%", (int)((g_engine.params.get(fv::P_BRIGHTNESS) * 100 + 127) / 255));
  hud_notify(lbl);
}
void brightnessDelta(int delta) { setBrightness(g_engine.params.get(fv::P_BRIGHTNESS) + delta); }
void toggleHud() {
  int32_t h = g_engine.params.get(fv::P_HUD);
  setParam(fv::P_HUD, h == 1 ? 0 : 1, false);
  hud_notify(h == 1 ? "HUD AUTO" : "HUD ON");
}
// A value outside the steps (set from the CLI) moves to the next more sensitive step; the last step wraps to Normal.
void cycleSensitivity() {
  struct Step { int32_t agcMinDb; const char* label; };
  static constexpr Step kSteps[] = {{-45, "HASSASIYET: NORMAL"}, {-52, "HASSASIYET: YUKSEK"}, {-60, "HASSASIYET: COK YUKSEK"}};
  const int32_t current = g_engine.params.get(fv::P_AGC_MIN_DB);
  const Step* next = &kSteps[0];
  for (const Step& step : kSteps) if (step.agcMinDb < current) { next = &step; break; }
  setParam(fv::P_AGC_MIN_DB, next->agcMinDb, false);
  hud_notify(next->label);
}

bool standby() { return s_standby; }

void toggleDisplay() {
  s_displayOn = !s_displayOn;
  display::setPower(s_displayOn && !s_standby);
}

bool toggleStandby() {
  const bool next = !s_standby;
  if (!s_prefsOk || s_prefs.putBool("standby", next) != 1) {
    display::feedback("Kayıt başarısız");
    return false;
  }
  if (next) s_standbyAt = millis();
  else s_anim.resume(s_standbyAt, millis());
  s_standby = next;
  ir::cancelFlow();
  ir::consumeUntilRelease();
  audio::setStandby(next);
  display::setPower(s_displayOn && !next);
  return true;
}

int remoteSlot(unsigned slot) { return slot < 6 ? s_slots[slot] : -1; }

bool saveRemoteSlot(unsigned slot, uint8_t mode) {
  if (slot >= 6 || mode >= fv::LM_COUNT || !s_prefsOk) return false;
  char key[] = "rs1";
  key[2] += slot;
  if (s_prefs.putInt(key, mode) != sizeof(int32_t)) return false;
  s_slots[slot] = mode;
  return true;
}

void setDisplay(const st7789::Config& c) {
  s_disp = c;
  display::requestConfig(c);
  markDirty();
}

st7789::Config displayConfig() { return s_disp; }

void calibrate() {
  hud_notify("KALIBRASYON");
  Serial.println(F("CAL gürültü kalibrasyonu: 2 s sessiz kalın"));
  audio::requestCalibrate(2 * fv::FRAME_HZ);
}

static void onBootBtn(Event e) {
  if (e == EV_SHORT) {
    toggleHud();
    Serial.printf("BTN boot hud=%ld\n", (long)g_engine.params.get(fv::P_HUD));
  } else if (e == EV_LONG) {
    Serial.println(F("BTN boot long → selftest"));
    selftest::run(true);
  }
}

// Açılış otomatik kalibrasyonu olayları (audio görevi g_stats.ac* alanlarını yayınlar; seri yazımı burada, loop görevinde yapılır:
// audio görevi UART'ta bloklanmasın). acSeq en son yazılır; metrikler ondan önce yazıldığı için tutarlıdır.
static void autocalReport() {
  static uint32_t seen = 0;
  const uint32_t seq = g_stats.acSeq;
  if (seq == seen) return;
  seen = seq;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  const uint32_t st = g_stats.acState, why = g_stats.acReason, tries = g_stats.acTries, win = g_stats.acWin;
  const float lv = g_stats.acLevel10 / 10.0f, bs = g_stats.acBass10 / 10.0f, sp = g_stats.acSpread10 / 10.0f, pk = g_stats.acPeak10 / 10.0f;
  static const char* const kWhy[] = {"-", "seviye yüksek", "durağan değil", "tepeli"};
  if (st == fv::AC_OBSERVE) {
    if (win == 0) {
      Serial.printf("AUTOCAL başladı (deneme %lu/%d): 2 s ölçülüyor, sessiz kalın\n", (unsigned long)tries + 1, fv::AUTOCAL_MAX_TRIES);
      if (tries == 0) hud_notify("KALIBRASYON");   // yeniden denemelerde ekranı kurcalama; seri satırı yeter
    } else {
      Serial.printf("AUTOCAL pencere %lu/%d geçti, onay penceresi: 2 s daha sessiz kalın\n", (unsigned long)win, fv::AUTOCAL_CONFIRM);
    }
  } else if (st == fv::AC_OK) {
    Serial.printf("AUTOCAL OK: gürültü tabanı öğrenildi (seviye %.1f dBFS, bas %.1f dBFS, sapma %.1f dB, tepe %.1f dB)\n", lv, bs, sp, pk);
    hud_notify("KALIBRE EDILDI");
  } else if (st == fv::AC_WAIT || st == fv::AC_GAVE_UP) {
    Serial.printf("AUTOCAL RED(%s): seviye %.1f dBFS (sınır %.1f), bas %.1f (sınır %.1f), sapma %.1f dB (sınır %.1f), tepe %.1f dB (sınır %.1f)\n",
                  kWhy[why < 4 ? why : 0], lv, fv::Analyzer::AC_LEVEL_MAX10 / 10.0f, bs, fv::Analyzer::AC_BASS_MAX10 / 10.0f, sp,
                  fv::Analyzer::AC_SPREAD_MAX10 / 10.0f, pk, fv::Analyzer::AC_PEAK_MAX10 / 10.0f);
    if (st == fv::AC_WAIT)
      Serial.printf("AUTOCAL %d s sonra yeniden denenecek (deneme %lu/%d)\n", fv::AUTOCAL_RETRY_FRAMES / fv::FRAME_HZ, (unsigned long)tries + 1, fv::AUTOCAL_MAX_TRIES);
    else
      Serial.printf("AUTOCAL vazgeçildi (%lu ret): sessizlikte 'cal' ya da HASH 3 s ile elle kalibre edin\n", (unsigned long)tries);
  }
}

void poll() {
  uint32_t now = millis();
  autocalReport();
  bool wasDown = s_boot.down;
  Event ev = buttonStep(s_boot, pressed(), now, [] {
    if (!s_bootConsumed && !s_standby) hud_notify("SELFTEST");
  });
  if (s_standby && !wasDown && s_boot.down) {
    s_bootConsumed = true;
    toggleStandby();
  }
  if (s_bootConsumed) {
    if (!s_boot.stable) s_bootConsumed = false;
  } else if (!s_standby) onBootBtn(ev);
  if (!s_standby && s_anim.count()) {
    s_anim.tick(now, [](fv::ParamId id, int32_t v) {
      if (g_engine.params.set(id, v)) s_taint |= 1ull << id;
    });
  }
  if (s_dirty && now - s_dirtyAt >= SAVE_DELAY_MS) saveNow(false);
}

void markDirty() {
  s_dirty = true;
  s_dirtyAt = millis();
}

void saveNow(bool userSave) {
  s_dirty = false;
  if (!s_prefsOk) return;
  int w = 0;
  for (int i = 0; i < fv::P_COUNT; ++i) {
    int32_t v = g_engine.params.v[i];
    if (!userSave && ((s_taint >> i) & 1u)) continue;   // geçici (animasyon) değer
    if (v != s_saved[i]) { s_prefs.putInt(fv::param_info((fv::ParamId)i).name, v); s_saved[i] = v; ++w; }
  }
  if (s_disp.mhz != s_dispSaved.mhz) { s_prefs.putInt("spi", s_disp.mhz); ++w; }
  if (s_disp.rot != s_dispSaved.rot) { s_prefs.putInt("rot", s_disp.rot); ++w; }
  if (s_disp.offX != s_dispSaved.offX) { s_prefs.putInt("offx", s_disp.offX); ++w; }
  if (s_disp.offY != s_dispSaved.offY) { s_prefs.putInt("offy", s_disp.offY); ++w; }
  s_dispSaved = s_disp;
  if (userSave) s_taint = 0;
  if (w) Serial.printf("NVS %d anahtar yazıldı\n", w);
}

void load(st7789::Config* disp) {
  applyConfigDefaults();
  g_engine.params.defaults();
  s_standby = false;
  s_displayOn = true;
  for (auto& slot : s_slots) slot = -1;
  s_prefsOk = s_prefs.begin("fftviz", false);
  st7789::Config d = defaultDisplay();
  if (s_prefsOk) {
    for (int i = 0; i < fv::P_COUNT; ++i) {
      const char* k = fv::param_info((fv::ParamId)i).name;
      if (s_prefs.isKey(k)) g_engine.params.set((fv::ParamId)i, s_prefs.getInt(k, g_engine.params.v[i]));
    }
    if (s_prefs.isKey("spi")) d.mhz = constrain(s_prefs.getInt("spi", d.mhz), 10, 80);
    if (s_prefs.isKey("rot")) d.rot = (uint8_t)(s_prefs.getInt("rot", 0) & 3);
    if (s_prefs.isKey("offx")) d.offX = (int16_t)s_prefs.getInt("offx", -1);
    if (s_prefs.isKey("offy")) d.offY = (int16_t)s_prefs.getInt("offy", -1);
    s_standby = s_prefs.getBool("standby", false);
    for (unsigned i = 0; i < 6; ++i) {
      char key[] = "rs1";
      key[2] += i;
      int mode = s_prefs.getInt(key, -1);
      s_slots[i] = mode >= 0 && mode < fv::LM_COUNT ? mode : -1;
    }
  }
  memcpy(s_saved, g_engine.params.v, sizeof(s_saved));
  s_disp = s_dispSaved = d;
  *disp = d;
}

void factoryReset() {
  s_anim.stop(anim::K_NONE, -1);
  s_taint = 0;
  if (s_prefsOk) s_prefs.clear();
  for (auto& slot : s_slots) slot = -1;
  ir::cancelFlow();
  ir::consumeUntilRelease();
  if (s_standby) {
    s_standby = false;
    audio::setStandby(false);
    display::setPower(s_displayOn);
  }
  g_engine.params.defaults();
  memcpy(s_saved, g_engine.params.v, sizeof(s_saved));
  st7789::Config d = defaultDisplay();
  s_disp = s_dispSaved = d;
  display::requestConfig(d);
  s_dirty = false;
}

// --- preset (0..3) ---
// NVS anahtarları: "p<slot>" (varlık işareti) ve "p<slot>_<param>" (≤ 15 karakter; param adı ≤ 12). Yalnız
// varsayılandan farklı, kurulumla ilgili olmayan (PF_SETUP değil) parametreler yazılır; eksik anahtar = varsayılan.
static void presetKey(char* out, int slot, const char* name) {
  if (name) snprintf(out, 16, "p%d_%s", slot, name);
  else snprintf(out, 16, "p%d", slot);
}

bool presetUsed(int slot) {
  if (!s_prefsOk || slot < 0 || slot >= PRESET_SLOTS) return false;
  char k[16];
  presetKey(k, slot, nullptr);
  return s_prefs.isKey(k);
}

bool presetClear(int slot) {
  if (!s_prefsOk || slot < 0 || slot >= PRESET_SLOTS) return false;
  char k[16];
  for (int i = 0; i < fv::P_COUNT; ++i) {
    presetKey(k, slot, fv::param_info((fv::ParamId)i).name);
    if (s_prefs.isKey(k)) s_prefs.remove(k);
  }
  presetKey(k, slot, nullptr);
  if (s_prefs.isKey(k)) s_prefs.remove(k);
  return true;
}

bool presetSave(int slot) {
  if (!s_prefsOk || slot < 0 || slot >= PRESET_SLOTS) return false;
  presetClear(slot);
  char k[16];
  for (int i = 0; i < fv::P_COUNT; ++i) {
    const fv::ParamInfo& in = fv::param_info((fv::ParamId)i);
    if (in.flags & fv::PF_SETUP) continue;
    const int32_t v = g_engine.params.v[i];
    if (v == fv::param_default((fv::ParamId)i)) continue;
    presetKey(k, slot, in.name);
    s_prefs.putInt(k, v);
  }
  presetKey(k, slot, nullptr);
  s_prefs.putInt(k, 1);
  return true;
}

bool presetLoad(int slot) {
  if (!presetUsed(slot)) return false;
  s_anim.stop(anim::K_NONE, -1);
  char k[16];
  for (int i = 0; i < fv::P_COUNT; ++i) {
    const fv::ParamInfo& in = fv::param_info((fv::ParamId)i);
    if (in.flags & fv::PF_SETUP) continue;
    presetKey(k, slot, in.name);
    const int32_t v = s_prefs.isKey(k) ? s_prefs.getInt(k, fv::param_default((fv::ParamId)i)) : fv::param_default((fv::ParamId)i);
    g_engine.params.set((fv::ParamId)i, v);
    s_taint &= ~(1ull << i);
  }
  markDirty();
  static char lbl[16];
  snprintf(lbl, sizeof(lbl), "PRESET %d", slot);
  hud_notify(lbl);
  return true;
}

void presetPrintList() {
  char k[16];
  for (int s = 0; s < PRESET_SLOTS; ++s) {
    if (!presetUsed(s)) { Serial.printf("preset %d: boş\n", s); continue; }
    int diff = 0;
    int32_t mode = 0, theme = 0, bri = 255, pal = 0;
    for (int i = 0; i < fv::P_COUNT; ++i) {
      presetKey(k, s, fv::param_info((fv::ParamId)i).name);
      if (!s_prefs.isKey(k)) continue;
      ++diff;
      const int32_t v = s_prefs.getInt(k, 0);
      if (i == fv::P_LED_MODE) mode = v;
      else if (i == fv::P_THEME) theme = v;
      else if (i == fv::P_BRIGHTNESS) bri = v;
      else if (i == fv::P_PAL) pal = v;
    }
    Serial.printf("preset %d: dolu, varsayılandan %d fark (mode=%ld theme=%ld bri=%ld pal=%ld)\n", s, diff, (long)mode, (long)theme,
                  (long)bri, (long)pal);
  }
}

}  // namespace controls

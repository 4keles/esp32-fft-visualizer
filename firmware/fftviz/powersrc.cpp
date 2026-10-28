// powersrc.cpp — adaptör algılama (GPIO35; yalnız CFG_ADP_DETECT=1 iken etkin, v2 donanımında kapalıdır).
// HIGH ⇔ şeridin gücü gerçekten adaptörden; LOW ⇔ USB (ya da belirsiz). Pin giriş-only, iç pull yok → dış bölücü şart.
// Akım limiter'ı aşağı yönde anlıktır: USB'ye düşünce bir sonraki karede bütçe ma_usb'ye iner; adaptöre dönünce bütçe
// yavaş (kare başına +4/256) yükselir. Böylece anahtar çevirmede kısa aşırı akım penceresi olmaz.
#include "powersrc.h"
#include "shared.h"
#include "pins.h"

namespace powersrc {

static bool s_usb = false;
static uint8_t s_same = 0;
static bool s_cand = false;
static uint32_t s_lastMs = 0;
static constexpr uint32_t SAMPLE_MS = 20;
static constexpr uint8_t NEED_SAME = 3;

bool enabled() { return ADP_DETECT; }
bool usb() { return ADP_DETECT && s_usb; }

static bool readUsb() { return digitalRead(PIN_ADP_OK) == LOW; }

void begin() {
  if (!ADP_DETECT) { g_engine.params.usbPower = 0; return; }
  pinMode(PIN_ADP_OK, INPUT);
  // Açılışta 3 okuma: hepsi HIGH değilse USB bütçesi (güvenli varsayılan).
  bool adaptor = true;
  for (int i = 0; i < 3; ++i) { adaptor = adaptor && !readUsb(); delay(5); }
  s_usb = !adaptor;
  s_cand = s_usb;
  s_same = NEED_SAME;
  g_engine.params.usbPower = s_usb ? 1 : 0;
}

void poll() {
  if (!ADP_DETECT) return;
  const uint32_t now = millis();
  if (now - s_lastMs >= SAMPLE_MS) {
    s_lastMs = now;
    const bool u = readUsb();
    if (u == s_cand) { if (s_same < NEED_SAME) ++s_same; }
    else { s_cand = u; s_same = 1; }
    if (s_same >= NEED_SAME && s_usb != s_cand) {
      s_usb = s_cand;
      g_engine.params.usbPower = s_usb ? 1 : 0;
      Serial.printf("POWER kaynak=%s etkin_butce=%ld mA\n", s_usb ? "usb" : "adaptor", (long)fv::led_budget_ma(g_engine.params));
    }
  }
  g_engine.params.usbPower = s_usb ? 1 : 0;   // factoryReset/defaults() sıfırlasa da geri yazılır
}

}  // namespace powersrc

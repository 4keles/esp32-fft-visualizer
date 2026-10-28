// fftviz.ino — ESP32 gerçek zamanlı FFT görselleştirici: setup/loop, görev oluşturma,
// açılış self-test kontrolü.
//   core 0: audio (öncelik 10) — ADC → Engine → RMT → snapshot → ekran bildirimi
//   core 1: display (öncelik 5) + Arduino loop (butonlar, CLI, NVS)
#include <fftviz.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include "pins.h"
#include "shared.h"
#include "ws2812.h"
#include "st7789.h"
#include "display_task.h"
#include "audio_task.h"
#include "controls.h"
#include "powersrc.h"
#include "cli.h"
#include "ir.h"
#include "selftest.h"

SnapshotLock g_snap;
Stats g_stats;
fv::Engine g_engine;

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_EXT: return "EXT";
    default: return "OTHER";
  }
}

void setup() {
  Serial.begin(115200);
  delay(20);
  Serial.printf("\nFFTVIZ boot fw=%s %s arduino=%d.%d.%d idf=%s cpu=%luMHz reset=%s\n", __DATE__, __TIME__,
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH, esp_get_idf_version(),
                (unsigned long)getCpuFrequencyMhz(), resetReason());

  controls::begin();
  Serial.printf("INIT controls BOOT=GPIO%d BLK=GPIO%d IR=GPIO%d GPIO4=NC/pulldown\n",
                PIN_BTN_BOOT, PIN_TFT_BLK, PIN_IR);
  st7789::Config dc;
  controls::load(&dc);
  g_engine.reset(1);
  Serial.printf("INIT params mode=%ld theme=%ld bri=%ld ma=%ld spi=%d rot=%d\n",
                (long)g_engine.params.get(fv::P_LED_MODE), (long)g_engine.params.get(fv::P_THEME),
                (long)g_engine.params.get(fv::P_BRIGHTNESS), (long)g_engine.params.get(fv::P_MA_BUDGET), dc.mhz, dc.rot);

  powersrc::begin();   // adaptör algılama (config'te kapalıysa hiçbir şey yapmaz); LED'den ÖNCE: ilk kare doğru bütçeyle gider
  {
    fv::BudgetSrc bs;
    const long b = (long)fv::led_budget_ma(g_engine.params, &bs);
    Serial.printf("INIT power model=%ldmA/LED+%lduA bütçe=%ldmA (%s) kaynak=%s [model tabanlı koruma; akım ölçülmez]\n",
                  (long)g_engine.params.get(fv::P_LED_MA), (long)g_engine.params.get(fv::P_LED_IDLE), b,
                  bs == fv::BS_PSU ? "psu" : bs == fv::BS_USB ? "usb" : "ma",
                  !powersrc::enabled() ? "algılama-kapalı" : powersrc::usb() ? "usb" : "adaptör");
  }
  const int nLed = g_engine.params.get(fv::P_LEDS);   // NVS'ten (vars. 120)
  bool led = ws2812::begin(PIN_LED, fv::LED_MAX, nLed);
  if (led) {
    static const fv::Rgb off[fv::LED_MAX] = {};
    ws2812::show(off, nLed);   // ilk kare LED_MAX'a kadar siyah yazar (önceki durumdan kalan yanık LED'ler)
    delay(15);                 // 300 LED ≈ 9 ms
    ws2812::trim();            // sonra tampon aktif uzunluğa küçülür
  }
  Serial.printf("INIT led %s\n", led ? "OK" : "FAIL");
  ir::begin();

  bool disp = display::begin(dc, !controls::standby());
  Serial.printf("INIT display %s (spi=%dMHz; panel bağlı olmasa da SPI çalışır)\n", disp ? "OK" : "FAIL", dc.mhz);

  bool aud = audio::begin(display::handle(), controls::standby());
  Serial.printf("INIT audio %s (adc_continuous %d Hz, GPIO%d)\n", aud ? "OK" : "FAIL", fv::FS_ADC, PIN_MIC);

  cli::begin();
  Serial.printf("READY heap=%lu (help yazın)\n", (unsigned long)ESP.getFreeHeap());

}

void loop() {
  ir::poll();
  ir::tick();
  cli::poll();
  controls::poll();
  powersrc::poll();
  selftest::poll();
  ir::drain();
  delay(5);
}

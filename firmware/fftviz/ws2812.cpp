// ws2812.cpp — RGB → GRB → rmt_data_t, rmtWriteAsync, çift buffer.
//
// Zamanlama (tick = 100 ns, WS2812B datasheet): T0H 0,4 µs / T0L 0,85 µs,
// T1H 0,8 µs / T1L 0,45 µs (±150 ns). Kullanılan: bit0 = {4 | 8}, bit1 = {8 | 4}
// → 1,2 µs/bit. Not: datasheet değerleri kullanıldı; {1,4|0,8}/{1,8|0,4} gibi
// daha kısa darbe çiftleri datasheet ile uyuşmuyor.
#include "ws2812.h"
#include "shared.h"

namespace ws2812 {

static constexpr uint32_t RMT_HZ = 10000000;  // 100 ns tick
static constexpr uint16_t T0H = 4, T0L = 8, T1H = 8, T1L = 4;
static constexpr uint16_t RESET_HALF = 1500;  // 2 × 150 µs düşük = 300 µs reset (≥280 µs)

static int s_pin = -1, s_max = 0;
static int s_lastN = 0;   // son başarıyla gönderilen uzunluk (kısalınca fazlası bir kez siyaha çekilir)
// Tek tampon: show() önceki gönderimin bittiğini doğruladıktan sonra kodlar (rmtWriteAsync tamponu gönderim boyunca
// okur; tamamlanmadan dokunulmaz). Boyutu gönderilen uzunluğa uyar (LED_MAX'ta 28,8 KB; 120'de 11,5 KB).
static rmt_data_t* s_buf = nullptr;
static size_t s_cap = 0;   // tampon kapasitesi (sembol)
static uint32_t s_encUs = 0;
static bool s_ok = false;
static SemaphoreHandle_t s_mtx = nullptr;

// Tamponu en az `need` sembole getirir; çok büyük kaldıysa küçültür (bellek parçalanmasın, bench/selftest için
// büyük blok kalsın). Yalnızca gönderim bitmişken ve muteks altında çağrılır.
static bool fit(size_t need) {
  if (s_buf && need <= s_cap && s_cap <= need + need / 2 + 512) return true;
  rmt_data_t* nb = (rmt_data_t*)heap_caps_malloc(need * sizeof(rmt_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!nb) return s_buf && need <= s_cap;   // büyütemedik; eskisi yetiyorsa onu kullan
  if (s_buf) heap_caps_free(s_buf);
  s_buf = nb;
  s_cap = need;
  return true;
}

bool begin(int pin, int maxLeds, int initialLeds) {
  s_pin = pin;
  s_max = maxLeds;
  if (initialLeds < 1) initialLeds = 1;
  if (initialLeds > maxLeds) initialLeds = maxLeds;
  s_lastN = maxLeds;   // açılışta şeridin önceki durumu bilinmez (reset/brownout sonrası eski uzunlukta yanık LED olabilir): ilk kare tüm LED_MAX'ı siyah yazar
  s_mtx = xSemaphoreCreateMutex();
  if (!fit((size_t)maxLeds * 24 + 1)) return false;
  s_ok = rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_4, RMT_HZ);
  if (s_ok) rmtSetEOT(pin, 0);
  return s_ok;
}

void trim() {
  if (!s_buf || !s_mtx || xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) != pdTRUE) return;
  if (rmtTransmitCompleted(s_pin)) {
    const size_t need = (size_t)s_lastN * 24 + 1;   // ilk kare sonrası s_lastN = aktif uzunluk
    rmt_data_t* nb = (rmt_data_t*)heap_caps_malloc(need * sizeof(rmt_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (nb) { heap_caps_free(s_buf); s_buf = nb; s_cap = need; }
  }
  xSemaphoreGive(s_mtx);
}

bool busy() { return s_ok && !rmtTransmitCompleted(s_pin); }

uint32_t lastEncodeUs() { return s_encUs; }

int show(const fv::Rgb* px, int n) {
  if (n < 1) n = 1;
  if (n > s_max) n = s_max;
  if (!s_ok || !s_mtx) { g_stats.rmtFail = g_stats.rmtFail + 1; return -1; }
  if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(5)) != pdTRUE) { g_stats.rmtBusy = g_stats.rmtBusy + 1; return 0; }
  if (!rmtTransmitCompleted(s_pin)) {
    xSemaphoreGive(s_mtx);
    g_stats.rmtBusy = g_stats.rmtBusy + 1;
    return 0;
  }
  uint32_t t0 = micros();
  const int sendN = n < s_lastN ? s_lastN : n;   // kısalma: eski uzunluğa kadar, fazlası siyah
  if (!fit((size_t)sendN * 24 + 1)) {
    xSemaphoreGive(s_mtx);
    g_stats.rmtFail = g_stats.rmtFail + 1;
    return -1;
  }
  rmt_data_t* d = s_buf;
  const uint32_t one = (uint32_t)T1H | (1u << 15) | ((uint32_t)T1L << 16);
  const uint32_t zero = (uint32_t)T0H | (1u << 15) | ((uint32_t)T0L << 16);
  for (int i = 0; i < sendN; ++i) {
    uint32_t grb = i < n ? (((uint32_t)px[i].g << 16) | ((uint32_t)px[i].r << 8) | px[i].b) : 0u;
    for (int k = 23; k >= 0; --k) (d++)->val = ((grb >> k) & 1) ? one : zero;
  }
  d->val = (uint32_t)RESET_HALF | ((uint32_t)RESET_HALF << 16);  // iki yarı da düşük
  s_encUs = micros() - t0;
  bool ok = rmtWriteAsync(s_pin, s_buf, (size_t)sendN * 24 + 1);
  if (ok) { s_lastN = n; g_stats.rmtOk = g_stats.rmtOk + 1; }
  else g_stats.rmtFail = g_stats.rmtFail + 1;
  xSemaphoreGive(s_mtx);
  return ok ? 1 : -1;
}

}  // namespace ws2812

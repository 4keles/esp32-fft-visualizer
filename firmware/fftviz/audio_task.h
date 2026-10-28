// audio_task.h — adc_continuous (48 kHz) → Engine → WS2812 → snapshot → ekran bildirimi.
#pragma once
#include <Arduino.h>
#include <fftviz.h>

struct RawStats {
  uint32_t n;
  uint32_t mean;    // ADC birimi ×16 (alt LSB çözünürlüğü)
  uint32_t rms16;   // AC RMS, ADC birimi ×16
  uint16_t min, max;
  uint32_t clip;    // 0 veya 4095 örnek sayısı
};

namespace audio {

bool begin(TaskHandle_t displayTask, bool standby = false);
void setStandby(bool standby);
// Sonraki `samples` ham örnek üzerinden istatistik toplar (bloklar, timeoutMs).
// dst verilirse (en az `samples` kapasiteli) örneklerin kendisi de oraya yazılır.
bool captureRaw(uint32_t samples, RawStats* out, uint32_t timeoutMs, uint16_t* dst = nullptr);
// LED çıkışını geçici olarak devral (nullptr = Engine çıktısına dön).
void ledOverride(const fv::Rgb* px);
// Gürültü kalibrasyonu (audio görevinde uygulanır).
void requestCalibrate(int frames);
// Engine'i sıfırla (audio görevinde uygulanır).
void requestReset();
// Giriş kaynağı: -1 = mikrofon, 0..SC_COUNT-1 = sentetik senaryo (ölçüm/demo).
void setSynth(int sc);
int synth();
// Teşhis: giriş örneklerine sinüs ekle (hz = 0 ya da ampLsb ≤ 0: kapalı). ampLsb tepe genlik (ADC LSB).
void setInject(uint32_t hz, int32_t ampLsb);
// mV dönüşümü (adc_cali line fitting; yoksa doğrusal yaklaşım).
uint32_t rawToMv(uint32_t raw);
bool caliAvailable();
// Audio görevi yığınının en düşük boş miktarı (bayt).
unsigned stackFree();

}  // namespace audio

// fv_engine.h — cephe: ADC örnekleri → desimasyon → 1024 ring → Analyzer → LedEngine.
#pragma once
#include "fv_config.h"
#include "fv_params.h"
#include "fv_decim.h"
#include "fv_analyzer.h"
#include "fv_leds.h"
#include "fv_synth.h"
#include "fv_screen.h"

namespace fv {

class Engine {
 public:
  Params params;
  void reset(uint32_t seed = 1);  // params.defaults() ÇAĞIRMAZ (ayarlar korunur)
  void discardInput();
  // 48 kHz ham ADC; her HOP (400 @24k) tamamlandığında analyze + leds.step çalışır.
  // Tamamlanan kare sayısını döner. n tek olabilir (artan örnek bir sonraki çağrıya kalır).
  // Pencere başlangıçta sıfırla doludur: ilk kare 400. örnekte gelir, 10 s ADC → tam 600 kare.
  int pushAdc(const uint16_t* raw12, int n);
  // Desimatörü atlayan 24 kHz giriş (testler).
  int pushPcm(const q15* x24k, int n);
  const Features& features() const { return an_.features(); }
  const LedEngine& leds() const { return leds_; }
  const Decimator& decim() const { return dec_; }
  Analyzer& analyzer() { return an_; }

 private:
  Decimator dec_;
  Analyzer an_;
  LedEngine leds_;
  q15 ring_[2 * FFT_N];  // her örnek head ve head+FFT_N'e yazılır → &ring_[head] bitişik pencere
  q15 tmp_[ADC_BLOCK / 2];
  int head_ = 0;      // en eski örneğin konumu (= sonraki yazma konumu)
  int sinceHop_ = 0;
  uint32_t seed_ = 1;
  uint16_t pend_ = 0;  // tek sayılı pushAdc'den kalan örnek
  bool hasPend_ = false;
};

// Golden hash bayrakları.
enum : uint32_t { GH_FEATURES = 1, GH_LEDS = 2, GH_SCREEN = 4, GH_ALL = 7 };

// Senaryoyu varsayılan parametrelerle `frames` kare koşturur ve FNV-1a hash döner.
// GH_FEATURES: level/bar/cap/led/beat/silent/centroidHue/bpm alanları her kare.
// GH_LEDS: LedEngine::out() her kare (mod = ledMode).
// GH_SCREEN: her 8. karede tam ekran render edilir ve 240 satırın pikselleri sırayla hash'e katılır.
// Statik buffer kullanır (heap yok); yeniden girişli değildir.
// fmin: 0 = varsayılan aralık; aksi halde P_FMIN (ör. 47 = eski sabit tablo aralığı, regresyon testi).
// leds: 0 = varsayılan uzunluk (120); aksi halde P_LEDS (host = WASM parite testi, tests/golden "leds_param").
// dither: -1 = varsayılan (P_DITHER); 0 = eski zamansal dither (led-flicker öncesi hash'lerle karşılaştırma).
// smooth: -1 = varsayılan (P_SMOOTH); 0 = eski atlamalı renk (led-smooth öncesi hash'lerle karşılaştırma).
uint32_t scenario_hash(Scenario sc, int frames, uint32_t seed, uint32_t flags, uint8_t ledMode = LM_AURORA,
                       uint8_t theme = 0, int32_t fmin = 0, int32_t leds = 0, int32_t dither = -1, int32_t smooth = -1);

// Aynı hesap, çağıranın sağladığı çalışma alanıyla (~42 KB). Firmware bunu heap'te ayırıp kullanırsa
// yukarıdaki statik sürüm hiç bağlanmaz ve DRAM'de yer kaplamaz. Hash değerleri birebir aynıdır.
struct ScenarioHashWork {
  Engine engine;
  Synth synth;
  ScreenRenderer screen;
  uint16_t adc[ADC_BLOCK];
  uint16_t row[SCREEN_W];  // ekran satır satır hash'lenir (şerit bölünmesinden bağımsız)
};
uint32_t scenario_hash(ScenarioHashWork& w, Scenario sc, int frames, uint32_t seed, uint32_t flags,
                       uint8_t ledMode = LM_AURORA, uint8_t theme = 0, int32_t fmin = 0, int32_t leds = 0,
                       int32_t dither = -1, int32_t smooth = -1);

}  // namespace fv

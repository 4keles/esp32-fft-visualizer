// shared.h — görevler arası paylaşım: Snapshot seqlock + istatistik sayaçları.
#pragma once
#include <Arduino.h>
#include <string.h>
#include <fftviz.h>

// Audio görevinin her karede yayınladığı durum.
struct Snapshot {
  fv::Features f;
  fv::Rgb leds[fv::LED_MAX];   // gönderilen wire değerleri (RGB sırası); ilk nLeds geçerli
  uint16_t nLeds;              // aktif LED sayısı
  uint16_t ma;                 // limiter sonrası akım tahmini (model)
  uint16_t demandMa;           // limiter öncesi talep (model)
  uint16_t budgetMa;           // uygulanan etkin bütçe
  uint8_t limiter;             // Q8, 255 = sınırlama yok
  uint8_t activeMode;          // DEMO içindeki gerçek alt mod
  char warning[24];           // Owned audio warning, published through the same single-writer snapshot.
  uint32_t warningAt;
  uint32_t seq;                // yayın sayacı (çift = kararlı)
};

// Tek yazar (core 0), çok okuyucu seqlock.
class SnapshotLock {
 public:
  void write(const Snapshot& s) {
    uint32_t q = __atomic_load_n(&seq_, __ATOMIC_RELAXED);
    __atomic_store_n(&seq_, q + 1, __ATOMIC_RELAXED);  // tek: yazılıyor
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memcpy(&data_, &s, sizeof(Snapshot));
    data_.seq = q + 2;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&seq_, q + 2, __ATOMIC_RELAXED);  // çift: kararlı
  }
  // En fazla 3 deneme; başarısızsa false (out kısmen eski kalabilir).
  bool read(Snapshot& out) const {
    for (int i = 0; i < 3; ++i) {
      uint32_t a = __atomic_load_n(&seq_, __ATOMIC_ACQUIRE);
      if (a & 1) { delayMicroseconds(2); continue; }
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      memcpy(&out, (const void*)&data_, sizeof(Snapshot));
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      if (__atomic_load_n(&seq_, __ATOMIC_ACQUIRE) == a) return true;
    }
    return false;
  }
  uint32_t seq() const { return __atomic_load_n(&seq_, __ATOMIC_ACQUIRE); }

 private:
  volatile uint32_t seq_ = 0;
  Snapshot data_;
};

// Sayaçlar: her alanın tek yazarı var; okuyucular hizalı 32 bit okur.
struct Stats {
  // audio (core 0)
  volatile uint32_t fsMilli;      // ölçülen fs × 1000 (Hz)
  volatile uint32_t afps10;       // analiz FPS × 10
  volatile uint32_t frames;       // toplam analiz karesi
  volatile uint32_t dspUsAvg, dspUsMax;  // pushAdc süresi (son 1 s)
  volatile uint32_t adcReads, adcSamples, adcOvf, adcBad, adcTimeouts;
  volatile uint32_t beats;
  volatile uint32_t clip1s;       // son 1 s içinde ADC kırpma örneği (0 veya 4095)
  volatile uint32_t rmtOk, rmtFail, rmtBusy;
  // açılış otomatik kalibrasyonu (core 0 yazar; core 1 acSeq değişince okur: metrikler acSeq'ten ÖNCE yazılır)
  volatile uint32_t acSeq, acState, acReason, acTries, acWin;
  volatile int32_t acLevel10, acBass10, acSpread10, acPeak10;
  // güç koruması (model tabanlı; gerçek akım ölçülmez)
  volatile uint32_t maPeak, demandPeak;   // açılıştan/`power reset`ten beri en yüksek model mA (çıkış / talep)
  volatile uint32_t limFrames;            // talebin bütçeyi aştığı (limiter'ın kıstığı) toplam kare
  volatile uint32_t limPct;               // son 1 s'de sınırlanan kare oranı %
  volatile uint32_t limTrips;             // "AKIM SINIRI" uyarısının tetiklenme sayısı
  // display (core 1)
  volatile uint32_t dfps10;       // ekran FPS × 10
  volatile uint32_t dframes;
  volatile uint32_t renderUsAvg, renderUsMax;  // beginFrame + 10 şerit render + checksum
  volatile uint32_t frameUsAvg;   // tam ekran döngüsü (DMA bekleme dahil)
  volatile uint32_t stripesSent, stripesSkipped;
  volatile uint32_t spiErr;
};

extern SnapshotLock g_snap;
extern Stats g_stats;
extern fv::Engine g_engine;  // params: tek yazar loop (core 1)

// Control-owned HUD label timing (loop writes, display reads); audio warnings travel in Snapshot.
void hud_notify(const char* label);

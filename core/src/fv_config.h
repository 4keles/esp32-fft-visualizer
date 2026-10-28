// fv_config.h — derleme zamanı sabitleri.
#pragma once
#include "fv_types.h"

namespace fv {

constexpr int FS_ADC        = 48000;  // ADC örnekleme hızı (Hz)
constexpr int FS            = 24000;  // desimasyon sonrası (Hz)
constexpr int ADC_BLOCK     = 800;    // bir hop'a karşılık gelen ADC örnek sayısı (48 kHz)
constexpr int FFT_LOG2N     = 10;
constexpr int FFT_N         = 1 << FFT_LOG2N;  // 1024 gerçek örnek
constexpr int FFT_BINS      = FFT_N / 2;       // 512 kutu (0..511), 23.4375 Hz
constexpr int HOP           = 400;             // 24 kHz'de → 60.0 kare/s
constexpr int FRAME_HZ      = FS / HOP;        // 60
constexpr int HALFBAND_TAPS = 47;

constexpr int NUM_BANDS     = 40;
constexpr int BASS_F_MAX    = 150;    // beat bas bölgesi üst sınırı (Hz)
// Beat/bas kanalı görüntü bantlarından bağımsızdır: doğrudan FFT kutuları [BASS_BIN_LO, BASS_BIN_HI).
constexpr int BASS_BIN_LO   = 2;                                  // 47 Hz (kutu 0-1: DC/HPF)
constexpr int BASS_BIN_HI   = BASS_F_MAX * FFT_N / FS + 1;        // 150 Hz dahil → 7
constexpr int BASS_BINS     = BASS_BIN_HI - BASS_BIN_LO;          // 5 tek kutulu bas kanalı
constexpr int TREBLE_F_MIN  = 4000;   // onset tiz bölgesi alt sınırı (Hz)

constexpr int LED_MAX       = 300;    // en büyük şerit (tamponlar buna göre statik ayrılır; 60 LED/m'de 5 m)
constexpr int NUM_LEDS      = 120;    // varsayılan LED sayısı (2 m); gerçek sayı `leds` parametresi (P_LEDS)

constexpr int SCREEN_W      = 240;
constexpr int SCREEN_H      = 240;
constexpr int STRIPE_ROWS   = 24;     // 10 şerit × 24 satır
constexpr int BAR_TOP_Y     = 8;
constexpr int BAR_BASE_Y    = 176;    // zemin çizgisi satırı
constexpr int BAR_AREA_H    = BAR_BASE_Y - BAR_TOP_Y;  // 168 px
constexpr int BAR_PITCH     = SCREEN_W / NUM_BANDS;    // 6 px

// autocal-boot: açılışta korumalı otomatik gürültü kalibrasyonu (Analyzer::autoCalStep; ölçütler fv_analyzer.cpp'de).
constexpr int AUTOCAL_START_FRAMES  = 3 * FRAME_HZ;  // ilk deneme: ekran/ADC oturduktan sonra (açılış ya da reset'ten 3 s)
constexpr int AUTOCAL_WINDOW_FRAMES = 2 * FRAME_HZ;  // gözlem penceresi = elle kalibrasyonun öğrenme süresi (2 s)
constexpr int AUTOCAL_CONFIRM       = 2;             // kabul için art arda bu kadar pencere ölçütleri geçmeli (rastlantısal sessiz müzik anı kabul ettirmesin)
constexpr int AUTOCAL_RETRY_FRAMES  = 5 * FRAME_HZ;  // reddedilirse yeniden deneme aralığı
constexpr int AUTOCAL_MAX_TRIES     = 6;             // bu kadar ret sonra vazgeç (HUD'da "kalibrasyon gerekli")

constexpr int SILENCE_FRAMES = 240;   // 4 s → idle
constexpr int IDLE_XFADE     = 18;

}  // namespace fv

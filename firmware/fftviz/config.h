// config.h — derleme zamanı ayarları (pinler, donanım, güç, varsayılanlar). Public varsayılanlar GÜVENLİDİR
// (60 mA/LED veri sayfası modeli, güç kaynağı belirtilmemiş, adaptör algılama kapalı).
//
// KENDİ DEĞERLERİNİZ İÇİN bu dosyayı değiştirmeyin: `user_config.example.h` dosyasını `user_config.h` olarak
// kopyalayıp yalnız istediğiniz satırları `#define` ile yazın (user_config.h git'e girmez). Bu dosya her ayarı
// `#ifndef` ile tanımlar; user_config.h'deki değer önceliklidir.
//
// Çalışma zamanı: akım/şerit/mod değerleri YALNIZ ilk açılış varsayılanıdır. CLI ve NVS bunları ezer
// (`ma 2500`, `led_ma 60`, ... kalıcıdır); `reset` fabrika ayarlarını bu dosyadaki değerlere döndürür.
// Pin ve ekran donanımı ayarları yalnız burada (yeniden derleme gerekir).
#pragma once

#if defined(__has_include)
#if __has_include("user_config.h")
#include "user_config.h"
#endif
#endif

// ===================================================================== PİNLER (ESP32 DevKit)
#ifndef CFG_PIN_TFT_SCK
#define CFG_PIN_TFT_SCK 18      // ST7789 SCK (VSPI IO_MUX pini; başka pin seçerseniz 40 MHz'de kenar kalitesi düşebilir)
#endif
#ifndef CFG_PIN_TFT_MOSI
#define CFG_PIN_TFT_MOSI 23     // ST7789 SDA/MOSI (VSPI IO_MUX pini)
#endif
#ifndef CFG_PIN_TFT_DC
#define CFG_PIN_TFT_DC 21       // ST7789 DC
#endif
#ifndef CFG_PIN_TFT_RST
#define CFG_PIN_TFT_RST 22      // ST7789 RES
#endif
#ifndef CFG_PIN_TFT_BLK
#define CFG_PIN_TFT_BLK 19      // ST7789 backlight; fixed HIGH unless PWM is enabled
#endif
#ifndef CFG_PIN_MIC
#define CFG_PIN_MIC 34          // mikrofon çıkışı; ADC1 pini olmalı (32..39). ADC kanalı pinden türetilir
#endif
#ifndef CFG_PIN_LED
#define CFG_PIN_LED 25          // WS2812B veri (seviye dönüştürücü/74HC tampon girişi)
#endif
#ifndef CFG_PIN_BTN_BOOT
#define CFG_PIN_BTN_BOOT 0      // BOOT düğmesi (HUD / selftest)
#endif

// ===================================================================== İSTEĞE BAĞLI DONANIM (kapalı = hiçbir şey yapmaz)
#ifndef CFG_ADP_DETECT
#define CFG_ADP_DETECT 0        // 1: adaptör algılama (plaket tasarımı); 0: kapalı → `ma_usb` hiç uygulanmaz
#endif
#ifndef CFG_PIN_ADP_OK
#define CFG_PIN_ADP_OK 35       // yalnız CFG_ADP_DETECT=1: giriş-only pin; HIGH = şerit gücü adaptörden, LOW = USB
#endif
#ifndef CFG_PIN_IR
#define CFG_PIN_IR 16           // Demodulated IR input; -1 disables reception
#endif

// Default NEC codes of the author's 17-key handset (16 captured on hardware, key 2 outstanding); another handset may differ, see `ir learn`. Values are normalized bytes.
#ifndef CFG_IR_ADDRESS
#define CFG_IR_ADDRESS 0x00
#endif
#ifndef CFG_IR_KEY_1
#define CFG_IR_KEY_1 0x45
#endif
#ifndef CFG_IR_KEY_2
#define CFG_IR_KEY_2 0x46
#endif
#ifndef CFG_IR_KEY_3
#define CFG_IR_KEY_3 0x47
#endif
#ifndef CFG_IR_KEY_4
#define CFG_IR_KEY_4 0x44
#endif
#ifndef CFG_IR_KEY_5
#define CFG_IR_KEY_5 0x40
#endif
#ifndef CFG_IR_KEY_6
#define CFG_IR_KEY_6 0x43
#endif
#ifndef CFG_IR_KEY_7
#define CFG_IR_KEY_7 0x07
#endif
#ifndef CFG_IR_KEY_8
#define CFG_IR_KEY_8 0x15
#endif
#ifndef CFG_IR_KEY_9
#define CFG_IR_KEY_9 0x09
#endif
#ifndef CFG_IR_KEY_0
#define CFG_IR_KEY_0 0x19
#endif
#ifndef CFG_IR_KEY_LEFT
#define CFG_IR_KEY_LEFT 0x08
#endif
#ifndef CFG_IR_KEY_RIGHT
#define CFG_IR_KEY_RIGHT 0x5A
#endif
#ifndef CFG_IR_KEY_UP
#define CFG_IR_KEY_UP 0x18
#endif
#ifndef CFG_IR_KEY_DOWN
#define CFG_IR_KEY_DOWN 0x52
#endif
#ifndef CFG_IR_KEY_OK
#define CFG_IR_KEY_OK 0x1C
#endif
#ifndef CFG_IR_KEY_STAR
#define CFG_IR_KEY_STAR 0x16
#endif
#ifndef CFG_IR_KEY_HASH
#define CFG_IR_KEY_HASH 0x0D
#endif

// ===================================================================== EKRAN (ST7789 240x240)
#ifndef CFG_TFT_SPI_MHZ
#define CFG_TFT_SPI_MHZ 40      // SPI saati MHz (10..80); CLI: spi
#endif
#ifndef CFG_TFT_ROTATION
#define CFG_TFT_ROTATION 0      // 0..3; CLI: rot
#endif
#ifndef CFG_TFT_OFFSET_X
#define CFG_TFT_OFFSET_X -1     // RAM ofseti; -1 = rotasyona göre otomatik; CLI: offs
#endif
#ifndef CFG_TFT_OFFSET_Y
#define CFG_TFT_OFFSET_Y -1
#endif
#ifndef CFG_TFT_DRIVE_CAP
#define CFG_TFT_DRIVE_CAP 0     // SCK/MOSI/DC sürüş gücü 0..3 (5/10/20/40 mA); düşük = ADC'ye daha az gürültü; CLI: drive
#endif
#ifndef CFG_TFT_BL_PWM
#define CFG_TFT_BL_PWM 0        // 0: arka ışık sabit HIGH (PWM, mikrofona 5 kHz tonu bindirir); 1: PWM
#endif
#ifndef CFG_TFT_BACKLIGHT
#define CFG_TFT_BACKLIGHT 200   // 0..255, yalnız CFG_TFT_BL_PWM=1 iken
#endif
#ifndef CFG_TFT_BL_FREQ
#define CFG_TFT_BL_FREQ 5000    // PWM frekansı (Hz), yalnız CFG_TFT_BL_PWM=1 iken
#endif

// ===================================================================== LED ŞERİT ve GÜÇ (ilk açılış varsayılanları)
// Akım modeli: mA = leds·CFG_LED_IDLE_UA/1000 + Σ(R+G+B)·CFG_LED_MA/765.  Limiter bu modelin etkin bütçeyi aşmasını
// engeller. Bu MODEL TABANLI bir korumadır: gerçek akım ölçülmez; led_ma'yı gerçek şeridinize göre ayarlayın.
#ifndef CFG_LEDS
#define CFG_LEDS 120            // aktif LED sayısı (1..300; üst sınır core/src/fv_config.h LED_MAX); CLI: leds
#endif
#ifndef CFG_LED_MA
#define CFG_LED_MA 60           // LED başına tam beyaz akımı mA. 60 = WS2812B veri sayfası (güvenli). Etiketten: W/m ÷ 5 V ÷ LED/m
#endif
#ifndef CFG_LED_IDLE_UA
#define CFG_LED_IDLE_UA 1000    // LED başına bekleme akımı µA (siyah bile çeker)
#endif
#ifndef CFG_MA_BUDGET
#define CFG_MA_BUDGET 2500      // şerit için izin verdiğiniz üst akım (mA, model); CLI: ma (100..20000)
#endif
#ifndef CFG_PSU_MA
#define CFG_PSU_MA 0            // güç kaynağı kapasitesi mA; 0 = belirtilmedi (yalnız CFG_MA_BUDGET geçerli)
#endif
#ifndef CFG_PSU_PCT
#define CFG_PSU_PCT 80          // kaynağın kullanılacak oranı %; etkin bütçe ≤ CFG_PSU_MA·CFG_PSU_PCT/100
#endif
#ifndef CFG_MA_USB
#define CFG_MA_USB 500          // USB beslemesinde bütçe mA (yalnız CFG_ADP_DETECT=1 ve USB algılanınca)
#endif

// ===================================================================== KALİBRASYON
// Manual noise capture: hold HASH for 3 s or use `cal`.
#ifndef CFG_AUTOCAL_BOOT
#define CFG_AUTOCAL_BOOT 1      // 1: açılıştan ~3 s sonra 2 s'lik otomatik gürültü kalibrasyonu dener. Müzik/ses/ton/aşırı gürültü varsa
                                // REDDEDER (5 s arayla en çok 6 deneme, sonra vazgeçer, ekranda "KALIBRASYON GEREKLI"); 0: yalnız elle. CLI: autocal
#endif

// ===================================================================== AÇILIŞ GÖRÜNÜMÜ
#ifndef CFG_DEFAULT_MODE
#define CFG_DEFAULT_MODE 0      // 0 AURORA 1 SPARKLE 2 MIRROR 3 SPECTRUM 4 BASS 5 WATERFALL 6 FIRE 7 COMET 8 DEMO
#endif
#ifndef CFG_DEFAULT_THEME
#define CFG_DEFAULT_THEME 0     // ekran: 0 SPECTRUM 1 EMBER 2 NEON 3 FIRE 4 CHROMA
#endif
#ifndef CFG_DEFAULT_BRIGHTNESS
#define CFG_DEFAULT_BRIGHTNESS 255   // LED parlaklık tavanı 0..255
#endif

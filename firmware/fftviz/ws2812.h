// ws2812.h — WS2812B sürücüsü: Arduino core 3.x RMT API, çift buffer, async gönderim.
#pragma once
#include <Arduino.h>
#include <fftviz.h>

namespace ws2812 {

// maxLeds: üst sınır (fv::LED_MAX); initialLeds: açılış uzunluğu (tampon buna göre ayrılır, heap parçalanmasın). Çalışma
// uzunluğu show()'a verilir.
bool begin(int pin, int maxLeds, int initialLeds);
// Açılış siyah karesinden sonra tamponu aktif uzunluğa küçültür (heap parçalanmasın). Gönderim bitmiş olmalı.
void trim();
// n RGB sıralı pikseli GRB'ye kodlar ve async gönderir (iki görevden çağrılabilir). n önceki gönderimden küçükse
// artakalan LED'leri bir kez siyaha çekmek için o kare eski uzunlukta gönderilir.
// 1 = gönderildi, 0 = önceki gönderim sürüyor (kare atlandı), -1 = RMT hatası.
int show(const fv::Rgb* px, int n);
bool busy();
uint32_t lastEncodeUs();

}  // namespace ws2812

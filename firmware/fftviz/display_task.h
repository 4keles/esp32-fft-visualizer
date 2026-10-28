// display_task.h — snapshot → ScreenRenderer → 10 şerit × 24 satır → ST7789 DMA.
#pragma once
#include <Arduino.h>
#include "st7789.h"

namespace display {

enum Mode : uint8_t { DM_BOOT = 0, DM_NORMAL, DM_TEST, DM_TEXT };

bool begin(const st7789::Config& cfg, bool power = true);
void setPower(bool on);
void feedback(const char* text, bool persistent = false);
void requestBacklight(uint8_t value, bool constant = false);
TaskHandle_t handle();
bool ok();  // panel init başarılı mı (SPI hatası yok)

void showNormal();
void showTest(uint8_t pattern);
// En fazla 13 satır × 20 karakter; içerik kopyalanır.
void showText(const char* const* lines, int n);
// Saat/rotasyon/ofset değişikliğini display görevinde uygular.
void requestConfig(const st7789::Config& cfg);
st7789::Config pendingConfig();

uint32_t frames();
// Şerit checksum atlamasını aç/kapat (kapalı = en kötü durum SPI yükü ölçümü).
void setSkip(bool en);
// Teşhis: çizimi ve SPI trafiğini durdur/başlat.
void setPaused(bool p);

}  // namespace display

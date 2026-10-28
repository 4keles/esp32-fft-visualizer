// Runtime controls and backward-compatible Preferences storage.
#pragma once
#include <Arduino.h>
#include <fftviz.h>
#include "st7789.h"
#include "anim.h"
#include "button.h"

namespace controls {


void begin();
// loop'tan sık çağrılır (≤10 ms): butonları işler, gecikmeli NVS yazımı yapar.
void poll();

// Ayar yardımcıları (CLI ve butonlar ortak). Değer değiştiyse true. Elle ayar o parametrenin
// ramp/sweep otomasyonunu durdurur.
bool setParam(fv::ParamId id, int32_t v, bool showHud);
void nextMode(int delta = 1);
void selectMode(uint8_t mode);
void nextTheme();
void brightnessDelta(int delta);
void toggleHud();
void cycleSensitivity();   // AGC floor (agcmin): -45 → -52 → -60 dBFS → -45, saved by the delayed NVS write
bool standby();
bool toggleStandby();
void toggleDisplay();
int remoteSlot(unsigned slot);
bool saveRemoteSlot(unsigned slot, uint8_t mode);
// Otomasyon tablosu (ramp/sweep). Değerler geçicidir: otomatik NVS yazımına katılmaz; `save` o anki değeri yazar.
anim::Table& animTable();
// Preset (0..3): kurulum dışı parametre seti NVS'te; yalnız varsayılandan farklılar saklanır.
constexpr int PRESET_SLOTS = 4;
bool presetSave(int slot);
bool presetLoad(int slot);   // yoksa false
bool presetClear(int slot);
bool presetUsed(int slot);
void presetPrintList();
void setDisplay(const st7789::Config& c);
st7789::Config displayConfig();

void markDirty();       // 5 s sonra NVS'e yaz
// userSave=false (otomatik): animasyonla değişmiş, henüz kullanıcı onaylamamış değerler yazılmaz.
void saveNow(bool userSave = true);
void load(st7789::Config* disp);
void factoryReset();    // varsayılanlar + NVS temizle

// Gürültü kalibrasyonu (2 s sessizlik).
void calibrate();

}  // namespace controls

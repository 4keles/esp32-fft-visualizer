// Installation diagnostics (DISPLAY, LED, MIC, FS, DSP, INTERFERENCE, INPUT).
#pragma once
#include <Arduino.h>

namespace selftest {

// Interactive entry checks a fresh remote OK; CLI entry skips INPUT.
bool run(bool fromButton);
// Sonuç sayfası süresi dolunca normal görünüme dön (loop'tan çağrılır).
void poll();
// Performans ölçümü; tek satır JSON basar.
void bench();
// Yalnız DSP adımı (golden hash karşılaştırması).
bool dspOnly();

}  // namespace selftest

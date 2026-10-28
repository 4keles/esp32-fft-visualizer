// test_leds_regress: parametrik-cli öncesi LED motorunun çıktısı (leds=120, yeni parametreler varsayılan)
// bit-aynı kalmalı. Referans hash'ler değişiklikten ÖNCE üretilmiştir (tamsayı Synth → platformdan bağımsız).
// Kasıtlı olarak görünüm değiştiren bir iş bu tabloyu yeniden üretmelidir (FV_PRINT_REGRESS=1 ile yazdırılır).
#include <cstdlib>
#include "fvtest.h"
#include "fftviz.h"
using namespace fv;

namespace {
struct Work { Engine engine; Synth synth; uint16_t adc[ADC_BLOCK]; };
Work& work() { static Work w; return w; }

// Varyant: 0 varsayılan, 1 bloom/bri/ae kapalı, 2 düşük bütçe + yüksek ae, 3 mod geçişleri (crossfade).
uint32_t run(int mode, Scenario sc, int variant) {
  Work& w = work();
  Engine& e = w.engine;
  e.params.defaults();
  e.params.set(P_LED_MODE, mode);
  e.params.set(P_DITHER, 0);   // referanslar eski zamansal dither ile üretildi (varsayılan led-flicker'da değişti)
  e.params.set(P_SMOOTH, 0);   // ... ve eski atlamalı renkle (varsayılan led-smooth'ta değişti)
  if (variant == 1) { e.params.set(P_BLOOM, 230); e.params.set(P_BRIGHTNESS, 100); e.params.set(P_LED_AE, 0); }
  if (variant == 2) { e.params.set(P_MA_BUDGET, 400); e.params.set(P_LED_AE, 255); e.params.set(P_LED_THRESH, 100); }
  e.reset(7);
  w.synth.reset(sc, 7, 0);
  uint32_t h = 2166136261u;
  int done = 0;
  while (done < 360) {
    w.synth.render(w.adc, ADC_BLOCK);
    if (e.pushAdc(w.adc, ADC_BLOCK) == 0) continue;
    if (variant == 3 && done % 90 == 45) e.params.set(P_LED_MODE, (mode + 1 + done / 90) % LM_COUNT);
    h = fnv1a(e.leds().out(), sizeof(Rgb) * NUM_LEDS, h);
    h = fnv1a(e.leds().pattern(), sizeof(Rgb) * NUM_LEDS, h);
    ++done;
  }
  return h;
}

const Scenario kSc[3] = {SC_MIX, SC_KICK120, SC_SILENCE};
// [mod][senaryo]
const uint32_t kBase[LM_COUNT][3] = {
    {0x110A8B1Fu, 0x35DBC0F2u, 0x2078C64Du},
    {0x1FA7A3D3u, 0x22CF2610u, 0xAB8E7BC7u},
    {0x95AEB6F4u, 0xFEFC073Bu, 0x833B53EDu},
    {0xFF2B53DDu, 0x80923F74u, 0x55389037u},
    {0x9C774705u, 0xCA216BD0u, 0x410DAA55u},
    {0xABC03741u, 0x99B807ADu, 0x64070F99u},
    {0x49E583FBu, 0x91E6D80Au, 0x11EB9E20u},
    {0x5C601B7Au, 0xA9A0F02Eu, 0xB971BC92u},
    {0x110A8B1Fu, 0x35DBC0F2u, 0x2078C64Du},
};
const uint32_t kVar[LM_COUNT][3] = {   // varyant 1,2,3 (MIX)
    {0x01E311F6u, 0x70C654D2u, 0xB9C27C94u},
    {0x61DE192Cu, 0xF49A538Au, 0xB4F5DA8Cu},
    {0x15E601D6u, 0x89386766u, 0x4E45C26Fu},
    {0x4B104F6Bu, 0x2BF5A552u, 0xE4307880u},
    {0x871F3E6Eu, 0xF980C960u, 0xBA02D7FBu},
    {0xC77E64E9u, 0xB7210665u, 0x00F1CD49u},
    {0xCADB93BFu, 0x2F011460u, 0x4DA4075Au},
    {0x57A9C4F0u, 0xC52123A9u, 0xE4EFF88Bu},
    {0x01E311F6u, 0x70C654D2u, 0x990BD2A2u},
};
}  // namespace

TEST(leds120_bit_identical) {
  const bool print = std::getenv("FV_PRINT_REGRESS") != nullptr;
  for (int m = 0; m < LM_COUNT; ++m) {
    uint32_t b[3], v[3];
    for (int s = 0; s < 3; ++s) b[s] = run(m, kSc[s], 0);
    for (int k = 0; k < 3; ++k) v[k] = run(m, SC_MIX, k + 1);
    if (print) {
      std::printf("REGRESS {0x%08Xu, 0x%08Xu, 0x%08Xu}, {0x%08Xu, 0x%08Xu, 0x%08Xu},  // %s\n", b[0], b[1], b[2], v[0], v[1], v[2], led_mode_name((uint8_t)m));
      continue;
    }
    for (int s = 0; s < 3; ++s) {
      if (b[s] != kBase[m][s]) std::printf("    mod %s senaryo %d: 0x%08X != 0x%08X\n", led_mode_name((uint8_t)m), s, b[s], kBase[m][s]);
      CHECK_EQ(b[s], kBase[m][s]);
    }
    for (int k = 0; k < 3; ++k) {
      if (v[k] != kVar[m][k]) std::printf("    mod %s varyant %d: 0x%08X != 0x%08X\n", led_mode_name((uint8_t)m), k + 1, v[k], kVar[m][k]);
      CHECK_EQ(v[k], kVar[m][k]);
    }
  }
}

FV_TEST_MAIN()

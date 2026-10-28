// test_anim: ramp/sweep mantığı (firmware/fftviz/anim.h; Arduino'dan bağımsız).
#include "fvtest.h"
#include "fftviz.h"
#include "../firmware/fftviz/anim.h"
using namespace fv;

TEST(ramp_linear_and_ends_exact) {
  anim::Table t;
  CHECK(t.ramp(P_BRIGHTNESS, 0, 200, 1000, 5000) == nullptr);
  int32_t v = -1;
  int calls = 0;
  t.tick(5000, [&](ParamId id, int32_t x) { CHECK_EQ((int)id, (int)P_BRIGHTNESS); v = x; ++calls; });
  CHECK_EQ(v, 0);
  t.tick(5500, [&](ParamId, int32_t x) { v = x; });
  CHECK_EQ(v, 100);
  CHECK_EQ(t.count(), 1);
  t.tick(6000, [&](ParamId, int32_t x) { v = x; });
  CHECK_EQ(v, 200);
  CHECK_EQ(t.count(), 0);                 // bitti: slot serbest
  t.tick(7000, [&](ParamId, int32_t) { ++calls; });
  CHECK_EQ(calls, 1);                      // artık çağrı yok
}

TEST(ramp_clamps_and_validates) {
  anim::Table t;
  CHECK(t.ramp(P_BRIGHTNESS, 100, 999, 1000, 0) == nullptr);   // hedef 255'e kırpılır
  CHECK_EQ((int)anim::slot_value(t.slot(0), 1000), 255);
  CHECK(t.ramp(P_BRIGHTNESS, 100, 0, 0, 0) != nullptr);        // süre 0 → hata
  CHECK(t.ramp(P_LEDS, 60, 120, 1000, 0) != nullptr);          // PF_NORAMP
  CHECK(t.ramp(P_LED_MODE, 0, 3, 1000, 0) != nullptr);
  CHECK(t.ramp(P_FMIN, 100, 300, 1000, 0) != nullptr);
}

TEST(ramp_down_and_wrap_safe) {
  anim::Table t;
  const uint32_t t0 = 0xFFFFFF00u;                              // millis() sarmalı
  CHECK(t.ramp(P_BRIGHTNESS, 255, 55, 1000, t0) == nullptr);
  CHECK_EQ((int)anim::slot_value(t.slot(0), t0 + 500u), 155);
  CHECK_EQ((int)anim::slot_value(t.slot(0), t0 + 1000u), 55);
}

TEST(sweep_triangle) {
  anim::Table t;
  CHECK(t.sweep(P_HUE_OFS, 0, 200, 2000, anim::SH_TRI, 0) == nullptr);
  const anim::Slot& s = t.slot(0);
  CHECK_EQ((int)anim::slot_value(s, 0), 0);
  CHECK_EQ((int)anim::slot_value(s, 500), 100);
  CHECK_EQ((int)anim::slot_value(s, 1000), 200);
  CHECK_EQ((int)anim::slot_value(s, 1500), 100);
  CHECK_EQ((int)anim::slot_value(s, 2000), 0);
  CHECK_EQ((int)anim::slot_value(s, 2500), 100);                // periyodik
  for (uint32_t ms = 0; ms < 6000; ms += 37) {
    const int32_t v = anim::slot_value(s, ms);
    CHECK(v >= 0 && v <= 200);
  }
  CHECK_EQ(t.count(), 1);                                       // sweep kendiliğinden bitmez
  t.tick(99999, [](ParamId, int32_t) {});
  CHECK_EQ(t.count(), 1);
}

TEST(sweep_sine_range_and_swap) {
  anim::Table t;
  CHECK(t.sweep(P_BRIGHTNESS, 250, 10, 1000, anim::SH_SIN, 0) == nullptr);   // alt/üst ters verildi → düzeltilir
  const anim::Slot& s = t.slot(0);
  CHECK_EQ((int)anim::slot_value(s, 0), 10);
  CHECK(anim::slot_value(s, 500) >= 249);
  CHECK(anim::slot_value(s, 250) > 100 && anim::slot_value(s, 250) < 160);
  for (uint32_t ms = 0; ms < 3000; ms += 13) { const int32_t v = anim::slot_value(s, ms); CHECK(v >= 10 && v <= 250); }
  CHECK(t.sweep(P_BRIGHTNESS, 0, 100, 50, anim::SH_TRI, 0) != nullptr);      // periyot < 100 ms
}

TEST(table_slots_and_stop) {
  anim::Table t;
  CHECK(t.sweep(P_BRIGHTNESS, 0, 255, 1000, anim::SH_TRI, 0) == nullptr);
  CHECK(t.sweep(P_HUE_OFS, 0, 255, 1000, anim::SH_TRI, 0) == nullptr);
  CHECK(t.ramp(P_HUE_SPD, 0, 50, 1000, 0) == nullptr);
  CHECK(t.sweep(P_FADE, 1, 100, 1000, anim::SH_TRI, 0) == nullptr);
  CHECK_EQ(t.count(), 4);
  CHECK(t.ramp(P_PAL_CYC, 0, 10, 1000, 0) != nullptr);          // tablo dolu
  CHECK(t.sweep(P_BRIGHTNESS, 5, 50, 500, anim::SH_SIN, 0) == nullptr);      // aynı parametre: slot yeniden kullanılır
  CHECK_EQ(t.count(), 4);
  CHECK(t.active(P_BRIGHTNESS) && !t.active(P_PAL_CYC));
  CHECK_EQ(t.stop(anim::K_RAMP, -1), 1);                         // yalnız ramp'ler
  CHECK_EQ(t.stop(anim::K_SWEEP, P_HUE_OFS), 1);
  CHECK_EQ(t.count(), 2);
  CHECK_EQ(t.stop(anim::K_NONE, -1), 2);
  CHECK_EQ(t.count(), 0);
}

FV_TEST_MAIN()

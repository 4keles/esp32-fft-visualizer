#include "fvtest.h"
#include "../firmware/fftviz/user_config.example.h"
#include "fixtures/v2/controls_harness.h"
using namespace fv;
TEST(CTRL_2_active_BOOT_release_boundaries_and_startup_suppression) {
  for (uint32_t duration : {1999u,2000u,2999u,3000u}) {
    fake::resetControls(); fake::gesture(PIN_BTN_BOOT,duration);
    CHECK_EQ(fake::selftests,duration>=2000?1:0);
    CHECK_EQ(g_engine.params.get(P_HUD),duration<2000?1:0);
    CHECK_EQ(fake::calibrations,0);
  }
  fake::resetControls(); fake::level[0]=LOW; controls::begin();
  fake::pollAt(5000); fake::edge(0,false,5100);
  CHECK_EQ(fake::selftests,0); CHECK_EQ(g_engine.params.get(P_HUD),0);
  fake::gesture(0,100); CHECK_EQ(g_engine.params.get(P_HUD),1);
  fake::resetControls(); auto t=fake::now;
  fake::level[0]=LOW; fake::pollAt(t); fake::level[0]=HIGH; fake::pollAt(t+24);
  fake::pollAt(t+500); CHECK_EQ(g_engine.params.get(P_HUD),0);
  for(int level : {LOW,HIGH}) {
    fake::level[4]=level; fake::pollAt(fake::now+100);
    CHECK_EQ(fake::reads[4],0);CHECK_EQ(fake::mode[4],INPUT_PULLDOWN);
  }
}
TEST(CTRL_7_defaults_saved_values_and_no_detector) {
  fake::resetControls(); CHECK_EQ(led_budget_ma(g_engine.params),2500);
  CHECK_EQ(g_engine.params.get(P_PSU_MA),3450); CHECK_EQ(g_engine.params.get(P_PSU_PCT),80);
  CHECK_EQ(g_engine.params.get(P_LED_MA),60);
  fake::nvs["ma"]=1700; fake::nvs["psu_ma"]=2200; fake::nvs["psu_pct"]=70;
  fake::resetControls(true); CHECK_EQ(led_budget_ma(g_engine.params),1540);
  CHECK_EQ(g_engine.params.get(P_MA_BUDGET),1700);
  powersrc::begin(); for(int i=0;i<100;++i) {fake::now+=20;powersrc::poll();}
  CHECK_EQ(fake::reads[35],0); CHECK_EQ(fake::mode[35],0);
  CHECK(!powersrc::enabled()); CHECK(!powersrc::usb()); CHECK_EQ(g_engine.params.usbPower,0);
  CHECK_EQ(fake::nvsWrites,0);
}
TEST(CTRL_3_actions_preserve_dirty_timer_and_automation) {
  fake::resetControls(); controls::setParam(P_BRIGHTNESS,120,false);
  CHECK(controls::s_dirty); uint32_t at=controls::s_dirtyAt;
  fake::now+=100; controls::setParam(P_BRIGHTNESS,120,false); CHECK_EQ(controls::s_dirtyAt,at);
  CHECK(controls::animTable().ramp(P_BRIGHTNESS,120,200,1000,fake::now)==nullptr);
  controls::brightnessDelta(16); CHECK(!controls::animTable().active(P_BRIGHTNESS));
  CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),136);
  fake::pollAt(controls::s_dirtyAt+4999); CHECK_EQ(fake::nvsWrites,0);
  fake::pollAt(controls::s_dirtyAt+5000); CHECK(fake::nvsWrites>0);
}
FV_TEST_MAIN()

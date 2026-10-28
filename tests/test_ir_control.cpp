#include "fvtest.h"
#include "fixtures/v2/controls_harness.h"
using namespace fv;
static void fresh(ir::Key key) { fake::press(key,fake::now+250); }
static void saveFlow(int stage, bool occupied=false, uint32_t start=1000) {
  fake::resetControls();
  if (occupied) { fake::nvs["rs2"]=7; fake::resetControls(true); }
  fake::press(ir::STAR,start); fake::release(start+181); fake::press(ir::STAR,start+500);
  if (stage>=1) fresh(ir::OK);
  if (stage>=2) fresh(ir::TWO);
}
TEST(CTRL_3_all_seventeen_fresh_keys_and_mode_only_recall) {
  for(unsigned key=0;key<ir::NONE;++key) {
    fake::resetControls();
    for(unsigned slot=0;slot<6;++slot) fake::nvs["rs"+std::to_string(slot+1)]=(slot+3)%LM_COUNT;
    fake::resetControls(true); g_engine.params.set(P_BRIGHTNESS,120);g_engine.params.set(P_THEME,2);
    fake::press(ir::Key(key),1000);
    if(key<=ir::SIX) CHECK_EQ(g_engine.params.get(P_LED_MODE),(key+3)%LM_COUNT);
    else CHECK_EQ(g_engine.params.get(P_LED_MODE),key==ir::LEFT?LM_COUNT-1:key==ir::RIGHT?1:0);
    CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),key==ir::UP?136:key==ir::DOWN?104:120);
    CHECK_EQ(g_engine.params.get(P_THEME),key==ir::OK?3:2);
    CHECK_EQ(g_engine.params.get(P_HUD),0);
    CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),key==ir::NINE?-52:-45);
    fake::release(1181);
    CHECK_EQ(g_engine.params.get(P_HUD),key==ir::ZERO?1:0);
    CHECK_EQ(fake::calibrations,0);CHECK_EQ(fake::nvsWrites,0);CHECK(!controls::standby());CHECK(fake::panelOn);
  }
  fake::resetControls();fresh(ir::ONE);CHECK(fake::feedback=="boş");CHECK(!controls::s_dirty);
  fresh(ir::UP);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),255);
  controls::setParam(P_BRIGHTNESS,5,false);fresh(ir::DOWN);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),0);
  controls::setParam(P_THEME,TH_COUNT-1,false);fresh(ir::OK);CHECK_EQ(g_engine.params.get(P_THEME),0);
}
TEST(CTRL_4_exact_repeat_release_boundaries_and_unsigned_wrap) {
  for(uint32_t start : {1000u,0xffffff00u}) {
    fake::resetControls();g_engine.params.set(P_BRIGHTNESS,100);
    fake::press(ir::UP,start);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),116);
    for(uint32_t t : {180u,349u}) fake::receive({true,true,false,0,0},start+t);
    CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),116);
    fake::receive({true,true,false,0,0},start+350);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),132);
    fake::receive(fake::key(ir::UP),start+459);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),132);
    fake::receive({true,true,false,0,0},start+460);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),148);
    fake::receive({true,true,false,0,0},start+640);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),164);
    fake::receive({true,true,false,0,0},start+821);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),164);
    fake::press(ir::DOWN,start+822);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),148);
    fake::press(ir::ZERO,start+1000);fake::release(start+1180);CHECK_EQ(g_engine.params.get(P_HUD),0);
    fake::release(start+1181);CHECK_EQ(g_engine.params.get(P_HUD),1);
  }
}
TEST(CTRL_2_3_hold_thresholds_are_validated_once_and_exclusive) {
  for(uint32_t start : {1000u,0xffffff00u}) for(auto key : {ir::ZERO,ir::SEVEN,ir::EIGHT,ir::HASH}) {
    const uint32_t threshold=key==ir::HASH?3000:2000;
    for(uint32_t duration : {threshold-1,threshold,threshold+100}) {
      fake::resetControls();fake::hold(key,start,duration);
      CHECK_EQ(fake::calibrations,key==ir::HASH && duration>=threshold?1:0);
      CHECK_EQ(controls::standby(),key==ir::SEVEN && duration>=threshold);
      CHECK_EQ(fake::panelOn,!(duration>=threshold && (key==ir::ZERO || key==ir::SEVEN)));
      CHECK(fake::feedback.empty());
      fake::release(start+duration+181);fake::release(start+duration+1000);
      CHECK_EQ(g_engine.params.get(P_HUD),key==ir::ZERO && duration<threshold?1:0);
      CHECK_EQ(fake::nvsWrites,key==ir::SEVEN && duration>=threshold?1:0);
      if(fake::calibrations) CHECK_EQ(fake::calFrames,2*FRAME_HZ);
    }
  }
  fake::resetControls();fake::press(ir::HASH,1000);fake::receive({true,true,false,0,0},4000);
  CHECK_EQ(fake::calibrations,0);
}
TEST(CTRL_4_invalid_unmapped_learning_and_stale_histories_are_inert) {
  for(int variant=0;variant<4;++variant) {
    fake::resetControls();fake::hold(ir::HASH,1000,2900);
    auto bad=fake::key(ir::UP);
    if(variant==0) bad={};else if(variant==1) bad.address^=1;else if(variant==2) bad.command=0xff;
    if(variant==3) { const char* args[]={"ir","learn","on"}; ir::command(3,args); }
    else fake::receive(bad,3950);
    fake::receive({true,true,false,0,0},4000);CHECK_EQ(fake::calibrations,0);
  }
  fake::resetControls();fake::press(ir::ZERO,1000);fake::receive({},1100);fake::release(1400);
  CHECK_EQ(g_engine.params.get(P_HUD),0);
  saveFlow(1); const char* args[]={"ir","learn","on"};ir::command(3,args);
  CHECK(fake::feedback.empty());CHECK_EQ(fake::nvsWrites,0);
  args[2]="off";ir::command(3,args);fresh(ir::TWO);CHECK(fake::feedback=="boş");
}
TEST(CTRL_10_double_STAR_500_501_and_intervening_press) {
  for(uint32_t start : {1000u,0xffffff00u}) for(uint32_t gap : {500u,501u}) {
    fake::resetControls();fake::press(ir::STAR,start);fake::release(start+181);fake::press(ir::STAR,start+gap);
    CHECK_EQ(fake::feedback=="Bu modu kaydet: OK",gap==500);CHECK_EQ(fake::nvsWrites,0);
  }
  fake::resetControls();fake::hold(ir::STAR,1000,2000);CHECK(fake::feedback.empty());CHECK_EQ(fake::nvsWrites,0);
  fake::resetControls();fake::press(ir::STAR,1000);fake::press(ir::OK,1200);fake::press(ir::STAR,1400);
  CHECK(fake::feedback.empty());CHECK_EQ(g_engine.params.get(P_THEME),1);
  fake::resetControls();fake::press(ir::STAR,1000);fake::receive({},1100);fake::press(ir::STAR,1400);
  CHECK(fake::feedback.empty());
}
TEST(CTRL_9_10_final_confirmation_is_the_only_slot_writer_and_captures_mode) {
  for(unsigned slot=0;slot<6;++slot) for(bool occupied : {false,true}) {
    fake::resetControls();if(occupied) {fake::nvs["rs"+std::to_string(slot+1)]=7;fake::resetControls(true);}
    controls::selectMode(4);fake::press(ir::STAR,1000);fake::release(1181);fake::press(ir::STAR,1500);
    CHECK(fake::feedback=="Bu modu kaydet: OK");CHECK_EQ(fake::nvsWrites,0);
    controls::selectMode(2);fresh(ir::OK);CHECK(fake::feedback=="Tuş seç (1–6)");
    fresh(ir::Key(slot));
    std::string expected=std::to_string(slot+1)+(occupied?" dolu (mod 7). Üzerine yaz: OK / iptal: ★":"'e kaydet: OK");
    CHECK(fake::feedback==expected);CHECK_EQ(fake::nvsWrites,0);CHECK_EQ(controls::remoteSlot(slot),occupied?7:-1);
    fresh(ir::OK);CHECK(fake::feedback=="Kaydedildi");CHECK_EQ(fake::nvsWrites,1);
    CHECK(fake::nvsKeys[0]=="rs"+std::to_string(slot+1));CHECK_EQ(controls::remoteSlot(slot),4);
    CHECK(fake::nvsTypes[0]=="int");
    g_engine.params.set(P_THEME,3);g_engine.params.set(P_BRIGHTNESS,82);fresh(ir::Key(slot));
    CHECK_EQ(g_engine.params.get(P_LED_MODE),4);CHECK_EQ(g_engine.params.get(P_THEME),3);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),82);
    CHECK_EQ(fake::nvsWrites,1);
  }
}
TEST(CTRL_10_every_modal_state_cancels_times_out_and_ignores_holds) {
  for(uint32_t start : {1000u,0xfffff000u}) for(int state=0;state<3;++state) for(bool occupied : {false,true}) {
    for(bool cancel : {false,true}) {
      saveFlow(state,occupied,start);uint32_t accepted=fake::now;
      auto before=g_engine.params;
      for(uint32_t d=100;d<9999;d+=100) fake::press(ir::HASH,accepted+d);
      fake::release(accepted+9999);CHECK(!fake::feedback.empty());CHECK_EQ(fake::calibrations,0);
      CHECK_EQ(memcmp(before.v,g_engine.params.v,sizeof(before.v)),0);
      if(cancel) fake::press(ir::STAR,accepted+9999);else fake::release(accepted+10000);
      CHECK(fake::feedback.empty());CHECK_EQ(fake::nvsWrites,0);CHECK_EQ(controls::remoteSlot(1),occupied?7:-1);
      fake::release(accepted+10200);fresh(ir::OK);CHECK_EQ(g_engine.params.get(P_THEME),1);
    }
  }
  saveFlow(0);fresh(ir::OK);uint32_t at=fake::now;
  fake::hold(ir::OK,at+100,1000);CHECK(fake::feedback=="Tuş seç (1–6)");CHECK_EQ(fake::nvsWrites,0);
  fresh(ir::TWO);at=fake::now;
  fake::release(at+9999);CHECK(!fake::feedback.empty());fake::release(at+10000);CHECK(fake::feedback.empty());
}
TEST(CTRL_13_failure_preserves_slot_and_general_saves_never_write_new_keys) {
  saveFlow(2,true);fake::failNvs=true;fresh(ir::OK);
  CHECK(fake::feedback=="Kayıt başarısız");CHECK_EQ(controls::remoteSlot(1),7);CHECK_EQ(fake::nvs["rs2"],7);CHECK_EQ(fake::nvsWrites,1);
  fake::failNvs=false;controls::selectMode(3);controls::saveNow(false);
  CHECK(fake::nvsKeys[0]=="rs2");
  for(size_t i=1;i<fake::nvsKeys.size();++i) CHECK(fake::nvsKeys[i]!="rs2" && fake::nvsKeys[i]!="standby");
  fake::resetControls();fake::nvs["bri"]=93;fake::nvs["p0"]=1;fake::nvs["p0_mode"]=6;
  fake::nvs["rs1"]=LM_COUNT;fake::nvs["rs2"]=-2;fake::nvs["rs3"]=8;fake::nvs["standby"]=1;
  fake::resetControls(true);CHECK_EQ(fake::nvsWrites,0);CHECK(controls::standby());CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),93);
  CHECK_EQ(controls::remoteSlot(0),-1);CHECK_EQ(controls::remoteSlot(1),-1);CHECK_EQ(controls::remoteSlot(2),8);
  CHECK(controls::presetLoad(0));CHECK_EQ(g_engine.params.get(P_LED_MODE),6);CHECK_EQ(controls::remoteSlot(2),8);
  CHECK(controls::presetSave(1));CHECK_EQ(fake::nvs["rs3"],8);
}
TEST(CTRL_11_12_13_display_standby_wake_authorization_and_boot_recovery) {
  fake::resetControls();fake::hold(ir::ZERO,1000,2000);CHECK(!fake::panelOn);CHECK(!controls::standby());CHECK_EQ(fake::nvsWrites,0);
  fake::hold(ir::SEVEN,4000,2000);CHECK(controls::standby());CHECK(fake::audioStandby);CHECK(!fake::panelOn);CHECK_EQ(fake::nvsWrites,1);
  const auto before=g_engine.params;
  for(unsigned key=0;key<ir::NONE;++key) {
    if(key==ir::SEVEN) continue;
    fake::hold(ir::Key(key),fake::now+250,key==ir::HASH?3000:2000);
  }
  CHECK(controls::standby());CHECK_EQ(memcmp(before.v,g_engine.params.v,sizeof(before.v)),0);CHECK_EQ(fake::nvsWrites,1);
  fake::hold(ir::SEVEN,fake::now+250,1999);CHECK(controls::standby());
  fake::hold(ir::SEVEN,fake::now+250,2000);CHECK(!controls::standby());CHECK(!fake::audioStandby);CHECK(!fake::panelOn);CHECK_EQ(fake::nvsWrites,2);
  fake::press(ir::OK,fake::now+100);CHECK_EQ(g_engine.params.get(P_THEME),0);
  fake::release(fake::now+181);fresh(ir::OK);CHECK_EQ(g_engine.params.get(P_THEME),1);
  fake::resetControls();fake::failNvs=true;fake::hold(ir::SEVEN,1000,2000);
  CHECK(!controls::standby());CHECK(!fake::audioStandby);CHECK(fake::panelOn);CHECK(fake::feedback=="Kayıt başarısız");CHECK_EQ(fake::nvsWrites,1);
  fake::failNvs=false;fake::hold(ir::SEVEN,4000,2000);CHECK(controls::standby());
  fake::resetControls(true);CHECK(controls::standby());CHECK_EQ(fake::nvsWrites,0);
  ir::setState(ir::ERROR);fake::gesture(0,3000);
  CHECK(!controls::standby());CHECK_EQ(fake::selftests,0);CHECK_EQ(g_engine.params.get(P_HUD),0);CHECK_EQ(fake::nvsWrites,1);
  CHECK(fake::panelOn);fake::resetControls(true);CHECK(!controls::standby());CHECK_EQ(fake::nvsWrites,0);
}
TEST(CTRL_12_parameter_animations_pause_without_reset_or_flash_writes) {
  fake::resetControls();controls::animTable().ramp(P_BRIGHTNESS,0,100,1000,1000);
  fake::pollAt(1400);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),40);
  controls::toggleStandby();fake::pollAt(9000);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),40);
  controls::toggleStandby();fake::pollAt(9100);CHECK_EQ(g_engine.params.get(P_BRIGHTNESS),50);
  CHECK_EQ(fake::nvsWrites,2);CHECK_EQ(fake::calibrations,0);
}
TEST(CTRL_8_INPUT_requires_fresh_OK_and_suppresses_all_normal_dispatch) {
  for(bool success : {false,true}) {
    fake::resetControls();fake::resetRmt();ir::begin();ir::setTesting(true);
    const uint32_t start=fake::now;
    fake::onDelay=[&](uint32_t) {
      const uint32_t age=fake::now-start;
      if(age==100 || age==200 || (success && age==500)) fake::press(ir::OK,fake::now);
      if(!success && age%100==0) fake::press(ir::OK,fake::now);
    };
    CHECK_EQ(ir::waitForOk(5000),success);fake::onDelay={};
    CHECK_EQ(g_engine.params.get(P_THEME),0);CHECK_EQ(fake::nvsWrites,0);
    fake::hold(ir::HASH,fake::now+250,3000);CHECK_EQ(fake::calibrations,0);
    ir::setTesting(false);fake::receive({true,true,false,0,0},fake::now+10);CHECK_EQ(fake::calibrations,0);
  }
  for(auto state : {ir::ERROR,ir::RX_DISABLED}) {
    fake::resetControls();ir::setState(state);ir::setTesting(true);CHECK(!ir::waitForOk(5000));
    CHECK_EQ(fake::reads[4],0);CHECK_EQ(fake::nvsWrites,0);
  }
}
TEST(CTRL_13_standby_bool_write_precedes_consumers_without_flushing_dirty_settings) {
  fake::resetControls();controls::selectMode(4);
  const uint32_t dirtyAt=controls::s_dirtyAt;
  const size_t entered=fake::trace.size();
  CHECK(controls::toggleStandby());CHECK_EQ(fake::nvsWrites,1);
  CHECK(fake::nvsTypes[0]=="bool");CHECK(fake::nvsKeys[0]=="standby");
  CHECK(fake::trace[entered]=="nvs:standby:1");
  CHECK(fake::trace[entered+1]=="audio:stop");CHECK(fake::trace[entered+2]=="display:off");
  CHECK(controls::s_dirty);CHECK_EQ(controls::s_dirtyAt,dirtyAt);
  fake::failNvs=true;const size_t before=fake::trace.size();
  CHECK(!controls::toggleStandby());CHECK_EQ(fake::trace.size(),before+1);
  CHECK(controls::standby());CHECK(fake::trace.back()=="nvs:standby:0");
}
TEST(CTRL_4_same_key_full_frames_hold_once_and_quiet_gaps_never_complete_holds) {
  for(auto key : {ir::ZERO,ir::SEVEN,ir::EIGHT,ir::HASH}) {
    fake::resetControls();fake::press(key,1000);
    const uint32_t threshold=key==ir::HASH?3000:2000;
    for(uint32_t t=1100;t<1000+threshold;t+=100) fake::press(key,t);
    fake::release(1000+threshold+181);
    CHECK_EQ(fake::calibrations,0);CHECK(!controls::standby());CHECK(fake::panelOn);
    CHECK_EQ(g_engine.params.get(P_HUD),key==ir::ZERO?1:0);
    CHECK_EQ(fake::nvsWrites,0);
    fake::resetControls();fake::press(key,1000);
    for(uint32_t t=1100;t<=6000;t+=100) fake::press(key,t);
    fake::release(6181);
    CHECK_EQ(fake::calibrations,key==ir::HASH?1:0);
    CHECK_EQ(fake::nvsWrites,key==ir::SEVEN?1:0);
    CHECK_EQ(g_engine.params.get(P_HUD),0);
    CHECK_EQ(controls::standby(),key==ir::SEVEN);
    CHECK_EQ(fake::panelOn,key!=ir::ZERO && key!=ir::SEVEN);
  }
}
TEST(CTRL_13_explicit_same_value_save_writes_once_and_failed_wake_is_not_applied) {
  fake::resetControls();fake::nvs["rs2"]=7;fake::resetControls(true);
  controls::selectMode(7);
  fake::press(ir::STAR,1000);fake::release(1181);fake::press(ir::STAR,1500);
  fresh(ir::OK);fresh(ir::TWO);CHECK_EQ(fake::nvsWrites,0);
  fresh(ir::OK);CHECK_EQ(fake::nvsWrites,1);CHECK_EQ(fake::nvs["rs2"],7);
  CHECK(fake::feedback=="Kaydedildi");CHECK(fake::nvsKeys[0]=="rs2");
  fake::resetControls();fake::hold(ir::SEVEN,1000,2000);
  CHECK(controls::standby());CHECK_EQ(fake::nvs["standby"],1);
  fake::failNvs=true;fake::hold(ir::SEVEN,4000,2000);
  CHECK(controls::standby());CHECK(fake::audioStandby);CHECK(!fake::panelOn);
  CHECK_EQ(fake::nvs["standby"],1);CHECK_EQ(fake::nvsWrites,2);
  CHECK(fake::feedback=="Kayıt başarısız");
  fake::release(10000);CHECK_EQ(fake::nvsWrites,2);
  fake::resetControls(true);CHECK(controls::standby());CHECK_EQ(fake::nvsWrites,0);
}
TEST(SENS_key_nine_cycles_normal_high_very_high_and_wraps) {
  fake::resetControls();
  CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),-45);
  const int32_t expected[]={-52,-60,-45,-52};
  const char* labels[]={"HASSASIYET: YUKSEK","HASSASIYET: COK YUKSEK","HASSASIYET: NORMAL","HASSASIYET: YUKSEK"};
  for(int i=0;i<4;++i) {
    fresh(ir::NINE);
    CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),expected[i]);CHECK(fake::hud==labels[i]);
  }
  CHECK_EQ(fake::nvsWrites,0);
}
TEST(SENS_key_nine_uses_the_parameter_setter_and_the_delayed_save) {
  fake::resetControls();controls::animTable().ramp(P_AGC_MIN_DB,-45,-10,1000,1000);
  CHECK_EQ(controls::animTable().count(),1);
  fresh(ir::NINE);
  CHECK_EQ(controls::animTable().count(),0);CHECK(controls::s_dirty);CHECK_EQ(fake::nvsWrites,0);
  const uint32_t at=fake::now;
  fake::pollAt(at+4999);CHECK_EQ(fake::nvsWrites,0);
  fake::pollAt(at+5000);
  CHECK_EQ(fake::nvsWrites,1);CHECK(fake::nvsKeys[0]=="agcmin");CHECK_EQ(fake::nvs["agcmin"],-52);CHECK(!controls::s_dirty);
  fake::resetControls(true);CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),-52);
}
TEST(SENS_key_nine_from_a_custom_agcmin_picks_the_next_more_sensitive_step) {
  struct Case {int32_t from,to;};
  for(const Case c : {Case{-10,-45},Case{-44,-45},Case{-46,-52},Case{-50,-52},Case{-52,-60},Case{-53,-60},Case{-59,-60},Case{-60,-45}}) {
    fake::resetControls();controls::setParam(P_AGC_MIN_DB,c.from,false);
    fresh(ir::NINE);CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),c.to);
  }
}
TEST(SENS_key_nine_is_one_step_per_press_and_ignored_in_standby_and_save_flow) {
  fake::resetControls();fake::hold(ir::NINE,1000,3000);
  CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),-52);
  fake::resetControls();CHECK(controls::toggleStandby());fresh(ir::NINE);
  CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),-45);CHECK(fake::hud.empty());
  saveFlow(1);fresh(ir::NINE);
  CHECK_EQ(g_engine.params.get(P_AGC_MIN_DB),-45);CHECK(fake::hud.empty());CHECK(fake::feedback=="Tuş seç (1–6)");
}
FV_TEST_MAIN()

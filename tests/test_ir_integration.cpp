#include "fvtest.h"
#include "fixtures/v2/cli_harness.h"
#include "fixtures/v2/nec.h"
#include "../firmware/fftviz/ws2812.cpp"
TEST(CTRL_6_asynchronous_lifecycle_and_LED_TX_coexistence) {
  fake::resetIr();fake::resetRmt();
  CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,120));ir::begin();
  CHECK_EQ(ir::state(),ir::READY);CHECK_EQ(fake::inits.size(),2);
  CHECK_EQ(fake::inits[0].mode,RMT_TX_MODE);CHECK_EQ(fake::inits[0].blocks,4);CHECK_EQ(fake::inits[0].hz,10000000);
  CHECK_EQ(fake::inits[1].mode,RMT_RX_MODE);CHECK_EQ(fake::inits[1].blocks,1);CHECK_EQ(fake::inits[1].hz,1000000);
  int arms=fake::arms;ir::poll();CHECK_EQ(fake::arms,arms);
  fake::capture(vectors::frame(CFG_IR_KEY_2));ir::poll();CHECK_EQ(fake::arms,arms+1);
  CHECK_EQ(*fake::rxCount,64); CHECK_EQ(g_engine.params.get(fv::P_LED_MODE),0);
  CHECK(fake::feedback == "boş");
  fake::capture(vectors::repeat());ir::poll();CHECK_EQ(g_engine.params.get(fv::P_LED_MODE),0);
  fv::Rgb pixels[120]={};CHECK_EQ(ws2812::show(pixels,120),1);CHECK_EQ(fake::tx,1);
  CHECK_EQ(g_stats.rmtFail,0);
  fake::command("ir learn on");Serial.capacity=0;
  fake::capture(vectors::frame());arms=fake::arms;ir::poll();ir::drain();CHECK_EQ(fake::arms,arms+1);
}
TEST(CTRL_6_initialization_and_arming_errors_latch) {
  for(int fail=1;fail<=4;++fail) {
    fake::resetIr();fake::resetRmt();fake::failAt=fail;ir::begin();CHECK_EQ(ir::state(),ir::ERROR);
    int calls=fake::rmtCalls;for(int i=0;i<100;++i)ir::poll();CHECK_EQ(fake::rmtCalls,calls);
    fake::gesture(PIN_BTN_BOOT,100); CHECK_EQ(g_engine.params.get(fv::P_HUD),1);
    fake::command("ir");CHECK(Serial.output.find("ERROR")!=std::string::npos);
  }
  fake::resetIr();fake::resetRmt();ir::begin();fake::capture(vectors::frame(CFG_IR_KEY_HASH));
  fake::failAt=fake::rmtCalls+2;ir::poll();CHECK_EQ(ir::state(),ir::ERROR);CHECK_EQ(fake::calibrations,0);
}
TEST(CTRL_6_disabled_build_has_no_pin_access) {
  CHECK_EQ(std::system("\"" FV_BINARY_DIR "/test_ir_disabled\""),0);
}
static uint32_t hashBytes(uint32_t h,const void* bytes,size_t n) {
  const uint8_t* p=(const uint8_t*)bytes;
  for(size_t i=0;i<n;++i) h=(h^p[i])*16777619u;
  return h;
}
static uint32_t engineHash(int traffic,int frames=240) {
  fv::Synth synth;synth.reset(fv::SC_MIX,42,0);
  fv::ScreenRenderer screen;uint16_t adc[fv::ADC_BLOCK],row[fv::SCREEN_W];
  uint32_t h=2166136261u;
  int appliedCal=0;
  for(int frame=0;frame<frames;++frame) {
    fake::now+=17;
    if(traffic>=2) {
      auto s=vectors::frame(traffic==3?0xff:CFG_IR_KEY_HASH);
      if(traffic==2) s[0].val=123;
      fake::capture(s);
    }
    ir::poll();ir::drain();controls::poll();powersrc::poll();
    if(fake::calibrations!=appliedCal) {
      g_engine.analyzer().calibrateNoise(fake::calFrames);appliedCal=fake::calibrations;
    }
    synth.render(adc,fv::ADC_BLOCK);g_engine.pushAdc(adc,fv::ADC_BLOCK);
    const auto& f=g_engine.features();
    h=hashBytes(h,f.level,sizeof(f.level));h=hashBytes(h,f.bar,sizeof(f.bar));
    h=hashBytes(h,f.cap,sizeof(f.cap));h=hashBytes(h,f.led,sizeof(f.led));
    h=hashBytes(h,&f.beat,sizeof(f.beat));h=hashBytes(h,&f.silent,sizeof(f.silent));
    h=hashBytes(h,&f.centroidHue,sizeof(f.centroidHue));h=hashBytes(h,&f.bpm,sizeof(f.bpm));
    h=hashBytes(h,g_engine.leds().out(),g_engine.leds().count()*sizeof(fv::Rgb));
    CHECK_EQ(ws2812::show(g_engine.leds().out(),g_engine.leds().count()),1);
    if((frame&7)==0) {
      fv::HudInfo hud{nullptr,0,0,0};screen.beginFrame(f,g_engine.params,hud,frame);
      for(int y=0;y<fv::SCREEN_H;++y) {screen.renderRows(y,1,row);h=hashBytes(h,row,sizeof(row));}
    }
  }
  return h;
}
TEST(SIM_1_inert_IR_preserves_features_LED_screen_and_calibration) {
  uint32_t baseline=0;
  for(int traffic=0;traffic<=4;++traffic) {
    fake::resetIr();fake::resetRmt();CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,120));ir::begin();
    if(traffic==0) ir::setState(ir::RX_DISABLED);
    if(traffic==4) {fake::command("ir learn on");Serial.capacity=0;}
    auto before=g_engine.params;uint32_t hash=engineHash(traffic);
    if(traffic==0) baseline=hash;else CHECK_EQ(hash,baseline);
    CHECK_EQ(memcmp(before.v,g_engine.params.v,sizeof(before.v)),0);
    CHECK_EQ(fake::calibrations,0);CHECK_EQ(fake::nvsWrites,0);CHECK(!controls::s_dirty);
    CHECK_EQ(g_stats.rmtFail,0);CHECK_EQ(fake::tx,240);
    if(traffic==4) CHECK(ir::s_dropped>0);
  }
}
static void existingAction(ir::Key key) {
  switch(key) {
    case ir::LEFT: controls::nextMode(-1);break;
    case ir::RIGHT: controls::nextMode();break;
    case ir::UP: fake::command("bri 136");break;
    case ir::DOWN: fake::command("bri 104");break;
    case ir::OK: controls::nextTheme();break;
    default:break;
  }
}
TEST(CTRL_3_existing_remote_actions_preserve_engine_output) {
  for(auto key : {ir::LEFT,ir::RIGHT,ir::UP,ir::DOWN,ir::OK}) {
    fv::Params params; bool dirty=false; uint32_t dirtyAt=0,hash=0;
    for(int remote=0;remote<2;++remote) {
      fake::resetIr();fake::resetRmt();CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,120));ir::begin();
      g_engine.params.set(fv::P_BRIGHTNESS,120);g_engine.params.set(fv::P_LED_MODE,8);
      CHECK(controls::animTable().ramp(fv::P_BRIGHTNESS,120,200,1000,fake::now)==nullptr);
      fake::now=1000;
      if(remote) {fake::capture(vectors::frame(ir::KEYS[key].command));ir::poll();}
      else existingAction(key);
      if(!remote) {
        params=g_engine.params;dirty=controls::s_dirty;dirtyAt=controls::s_dirtyAt;
        hash=engineHash(1,64);
      } else {
        CHECK_EQ(memcmp(params.v,g_engine.params.v,sizeof(params.v)),0);
        CHECK_EQ(controls::s_dirty,dirty);CHECK_EQ(controls::s_dirtyAt,dirtyAt);
        CHECK_EQ(engineHash(1,64),hash);
      }
      if(key==ir::UP || key==ir::DOWN) CHECK(!controls::animTable().active(fv::P_BRIGHTNESS));
    }
  }
}
FV_TEST_MAIN()

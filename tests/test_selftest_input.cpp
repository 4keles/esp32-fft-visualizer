#define FV_REAL_SELFTEST
#include "fvtest.h"
#include <new>
#include "fixtures/v2/controls_harness.h"
#include "../firmware/fftviz/selftest.cpp"
#include "../firmware/fftviz/st7789.cpp"
#include "../firmware/fftviz/ws2812.cpp"
namespace fake { inline bool rawAvailable=false;inline RawStats raw={}; }
namespace audio {
void ledOverride(const fv::Rgb*) {}
bool captureRaw(uint32_t,RawStats* out,uint32_t,uint16_t*) {*out=fake::raw;return fake::rawAvailable;}
uint32_t rawToMv(uint32_t raw) {return raw;}
bool caliAvailable() {return true;}
}
namespace display {
void showText(const char* const*,int) {}
void showTest(uint8_t) {}
void showNormal() {}
bool ok() {return true;}
uint32_t frames() {return 0;}
}
TEST(CTRL_8_production_INPUT_PASS_FAIL_SKIP_and_aggregate_denominator) {
  for(int outcome=0;outcome<3;++outcome) {
    fake::resetControls();fake::resetRmt();ir::begin();ir::setTesting(true);
    char detail[200];uint32_t start=fake::now;
    if(outcome==0) fake::onDelay=[&](uint32_t) {if(fake::now-start==500) fake::press(ir::OK,fake::now);};
    auto status=selftest::stepInput(detail,sizeof(detail),outcome!=2);
    CHECK_EQ(status,outcome==0?selftest::PASS:outcome==1?selftest::FAIL:selftest::SKIP);
    fake::onDelay={};int pass=0,total=0;selftest::report("INPUT",status,detail,&pass,&total);
    CHECK_EQ(pass,outcome==0?1:0);CHECK_EQ(total,outcome==2?0:1);
    CHECK(Serial.output.find(outcome==0?"TEST INPUT PASS":outcome==1?"TEST INPUT FAIL":"TEST INPUT SKIP")!=std::string::npos);
    CHECK_EQ(g_engine.params.get(fv::P_THEME),0);CHECK_EQ(fake::nvsWrites,0);CHECK_EQ(fake::reads[4],0);
  }
  for(auto state : {ir::ERROR,ir::RX_DISABLED}) {
    fake::resetControls();ir::setState(state);ir::setTesting(true);char detail[200];
    CHECK_EQ(selftest::stepInput(detail,sizeof(detail),true),selftest::FAIL);
  }
}
TEST(CTRL_8_unchanged_MIC_diagnosis_and_golden_DSP_comparison) {
  fake::resetControls();char detail[200];bool mic=false;
  fake::rawAvailable=true;fake::raw={1000,1650*16,100*16,1400,1900,4};
  CHECK_EQ(selftest::stepMic(detail,sizeof(detail),&mic),selftest::PASS);CHECK(mic);
  CHECK(std::string(detail).find("kazanç pot'unu azaltın")!=std::string::npos);
  fake::raw.mean=1300*16;
  CHECK_EQ(selftest::stepMic(detail,sizeof(detail),&mic),selftest::FAIL);CHECK(!mic);
  CHECK(std::string(detail).find("besleme/GND/kabloyu kontrol edin")!=std::string::npos);
  fake::rawAvailable=false;CHECK_EQ(selftest::stepMic(detail,sizeof(detail),&mic),selftest::FAIL);
  CHECK_EQ(selftest::stepDsp(detail,sizeof(detail)),selftest::PASS);
  CHECK(Serial.output.find("DSPHASH_ALL")!=std::string::npos);
}
FV_TEST_MAIN()

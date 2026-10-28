#include "fvtest.h"
#include "fixtures/v2/Arduino.h"
#include <new>
#include "../firmware/fftviz/display_task.cpp"
#include "../firmware/fftviz/st7789.cpp"
#include "../firmware/fftviz/audio_task.cpp"
#include "../firmware/fftviz/ws2812.cpp"
#include "../firmware/fftviz/controls.cpp"
#include "../firmware/fftviz/powersrc.cpp"
#include "../firmware/fftviz/ir_control.cpp"
#include "../firmware/fftviz/ir.cpp"
#include "../firmware/fftviz/ir_rmt.cpp"
#include "../firmware/fftviz/selftest.cpp"
#include "../firmware/fftviz/cli.cpp"
SnapshotLock g_snap;
Stats g_stats={};
fv::Engine g_engine;
struct StopTask {};
// The fake scheduler records the audio entry but never runs it. During an active begin()'s bounded wait, run the
// real audio::task entry up to its first ADC read (the task owns the initial start), then hand back to setup().
static bool beginAudio(TaskHandle_t displayTask,bool standby) {
  bool ran=false;
  fake::onDelay=[&](uint32_t) {
    if(ran) return;
    ran=true;CHECK(fake::taskEntry==audio::task);
    const auto previous=fake::onRead;
    fake::onRead=[] {throw StopTask{};};
    try {fake::taskEntry(nullptr);} catch(const StopTask&) {}
    fake::onRead=previous;
  };
  const bool started=audio::begin(displayTask,standby);
  fake::onDelay={};
  return started;
}
static void reset(bool persistedStandby) {
  fake::onDelay={};fake::onNotify={};fake::onRead={};fake::onCriticalEnter={};
  fake::now=0;fake::adcReads=0;fake::adcRaw=2048;fake::resetRmt();
  fake::nvs.clear();if(persistedStandby)fake::nvs["standby"]=1;
  fake::nvsWrites=0;fake::nvsKeys.clear();fake::nvsTypes.clear();fake::failNvs=false;
  fake::trace.clear();Serial={};g_stats={};g_snap.write(Snapshot{});
  controls::s_dirty=false;controls::s_anim={};controls::s_taint=0;
  for(auto& level:fake::level)level=HIGH;
  controls::begin();st7789::Config cfg;controls::load(&cfg);g_engine.reset(1);
  display::s_power=!persistedStandby;display::s_frames=0;
  display::s_cfgReq=false;display::s_feedback[0]=0;
  audio::s_out={};audio::s_ledOvr=nullptr;audio::s_winLim=audio::s_winFrames=0;
  ir::s_learning=false;ir::s_testing=false;ir::begin();cli::begin();
  CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,g_engine.leds().count()));
  static const fv::Rgb black[fv::LED_MAX]={};
  ws2812::show(black,g_engine.leds().count());
  CHECK(beginAudio(display::handle(),persistedStandby));
}
static void commandSelftest() {Serial.input="selftest\n";cli::poll();}
TEST(CTRL_12_CLI_selftest_cannot_light_persisted_or_runtime_standby) {
  for(bool persisted : {false,true}) {
    reset(persisted);
    if(!persisted)fake::onRead=[] {CHECK(controls::toggleStandby());};
    bool checked=false;
    fake::onNotify=[&](uint32_t ticks) {
      CHECK_EQ(ticks,portMAX_DELAY);CHECK(controls::standby());
      CHECK(!audio::s_running);CHECK(!fake::adcRunning);CHECK(!display::s_power);
      const auto lastLed=std::find_if(fake::trace.rbegin(),fake::trace.rend(),[](const std::string& e) {return e=="led:black" || e=="led:frame";});
      CHECK(lastLed!=fake::trace.rend());if(lastLed!=fake::trace.rend())CHECK(*lastLed=="led:black");
      const auto writes=fake::nvsWrites,tx=fake::tx,reads=fake::adcReads;
      const auto frames=g_stats.frames,at=fake::now;
      const auto params=g_engine.params;
      const size_t traceSize=fake::trace.size();
      commandSelftest();
      CHECK(Serial.output.find("ERR selftest: bekleme modu;")!=std::string::npos);
      CHECK(Serial.output.find("SELFTEST BAŞLA")==std::string::npos);
      CHECK_EQ(fake::nvsWrites,writes);CHECK_EQ(fake::tx,tx);CHECK_EQ(fake::adcReads,reads);
      CHECK_EQ(g_stats.frames,frames);CHECK_EQ(fake::now,at);CHECK_EQ(fake::trace.size(),traceSize);
      CHECK_EQ(memcmp(params.v,g_engine.params.v,sizeof(params.v)),0);
      CHECK(controls::standby());CHECK_EQ(fake::nvs["standby"],1);
      CHECK(!audio::s_running);CHECK(!fake::adcRunning);CHECK(!display::s_power);CHECK(!ir::s_testing);
      std::printf("STANDBY_SELFTEST %s error=1 extra_led_tx=0 extra_adc_reads=0 extra_nvs=0 elapsed_ms=0 black=1\n",persisted?"persisted":"runtime");
      checked=true;throw StopTask{};
    };
    try {audio::task(nullptr);} catch(const StopTask&) {}
    CHECK(checked);fake::onNotify={};fake::onRead={};
  }
}
TEST(CTRL_8_active_CLI_selftest_still_executes_all_existing_diagnostic_steps) {
  reset(false);const int writes=fake::nvsWrites;
  commandSelftest();
  CHECK(Serial.output.find("SELFTEST BAŞLA")!=std::string::npos);
  for(const char* step : {"DISPLAY","LED","MIC","FS","DSP","INTERFERENCE","INPUT"})
    CHECK(Serial.output.find(std::string("TEST ")+step+" ")!=std::string::npos);
  CHECK(Serial.output.find("TEST INPUT SKIP noninteractive:")!=std::string::npos);
  CHECK(Serial.output.find("TEST DSP PASS")!=std::string::npos);
  CHECK(Serial.output.find("ERR selftest:")==std::string::npos);
  CHECK(std::count(fake::trace.begin(),fake::trace.end(),"led:frame")>0);
  CHECK(!controls::standby());CHECK_EQ(fake::nvsWrites,writes);CHECK(!ir::s_testing);
  std::puts("ACTIVE_SELFTEST diagnostic_steps=7 colored_led_tx=1 DSP=PASS INPUT=SKIP extra_nvs=0");
}
FV_TEST_MAIN()

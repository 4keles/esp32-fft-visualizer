#include "fvtest.h"
#include "fixtures/v2/Arduino.h"
#include "../firmware/fftviz/display_task.cpp"
#include "../firmware/fftviz/st7789.cpp"
#include "../firmware/fftviz/audio_task.cpp"
#include "../firmware/fftviz/ws2812.cpp"
SnapshotLock g_snap;
Stats g_stats={};
fv::Engine g_engine;
struct StopTask {};
static size_t event(const std::string& name,size_t start=0) {
  for(size_t i=start;i<fake::trace.size();++i) if(fake::trace[i]==name) return i;
  return fake::trace.size();
}
static int count(const std::string& name,size_t start=0) {
  return std::count(fake::trace.begin()+start,fake::trace.end(),name);
}
static void reset() {
  fake::onDelay={};fake::onNotify={};fake::onRead={};fake::trace.clear();fake::now=0;fake::adcReads=0;
  fake::onCriticalEnter={};fake::adcRaw=2048;
  fake::resetRmt();g_stats={};g_engine.params.defaults();g_engine.reset(1);
  audio::s_out={};audio::s_winLim=audio::s_winFrames=0;
  display::s_hudLabel[0]=0;g_snap.write(Snapshot{});
}
// The fake scheduler records the audio entry but never runs it. During begin()'s bounded wait, run the real
// audio::task entry up to its first ADC read (the task owns the initial start), then hand back to setup().
static bool beginActive(TaskHandle_t displayTask) {
  bool ran=false;
  fake::onDelay=[&](uint32_t) {
    if(ran) return;
    ran=true;CHECK(fake::taskEntry==audio::task);
    const auto previous=fake::onRead;
    fake::onRead=[] {throw StopTask{};};
    try {fake::taskEntry(nullptr);} catch(const StopTask&) {}
    fake::onRead=previous;
  };
  const bool started=audio::begin(displayTask);
  fake::onDelay={};
  return started;
}
TEST(CTRL_12_audio_owner_stops_ADC_before_final_black_and_clears_backlog_on_resume) {
  reset();CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,120));CHECK(beginActive(nullptr));
  const int startupReads=fake::adcReads;
  int phase=0;uint32_t pausedFrames=0;
  fake::onRead=[&] {
    if(phase==0) {phase=1;audio::setStandby(true);}
    else {
      CHECK_EQ(phase,2);CHECK(audio::s_running);CHECK(fake::adcRunning);
      CHECK_EQ(count("adc:start"),2);CHECK_EQ(g_stats.frames,pausedFrames);
      throw StopTask{};
    }
  };
  fake::onNotify=[&](uint32_t ticks) {
    CHECK_EQ(ticks,portMAX_DELAY);CHECK(!audio::s_running);CHECK(!fake::adcRunning);
    const size_t stopped=event("adc:stop");
    CHECK(stopped<event("adc:flush",stopped));
    CHECK(event("adc:flush",stopped)<event("led:black",event("adc:flush",stopped)));
    pausedFrames=g_stats.frames;CHECK_EQ(fake::adcReads,startupReads+1);
    phase=2;audio::setStandby(false);
  };
  try {audio::task(nullptr);} catch(const StopTask&) {}
  CHECK_EQ(phase,2);CHECK_EQ(count("adc:stop"),1);CHECK_EQ(count("adc:flush"),3);
  CHECK_EQ(g_engine.features().ac.state,g_stats.acState);
  fake::onRead={};fake::onNotify={};
}
TEST(CTRL_12_cold_standby_does_not_start_ADC_or_process_a_frame) {
  reset();CHECK(audio::begin(nullptr,true));CHECK(!audio::s_running);CHECK_EQ(count("delay:1"),0);
  fake::onNotify=[&](uint32_t ticks) {
    CHECK_EQ(ticks,portMAX_DELAY);CHECK_EQ(fake::adcReads,0);CHECK_EQ(g_stats.frames,0);
    CHECK_EQ(count("adc:start"),0);CHECK_EQ(count("led:frame"),0);throw StopTask{};
  };
  try {audio::task(nullptr);} catch(const StopTask&) {}
  fake::onNotify={};
}
TEST(CTRL_12_active_begin_is_bounded_and_false_when_the_audio_task_never_reaches_running_ADC) {
  reset();CHECK(!audio::begin(nullptr));
  CHECK(!audio::s_running);CHECK_EQ(count("adc:start"),0);CHECK_EQ(count("delay:1"),1000);CHECK_EQ(fake::now,1000);
}
TEST(CTRL_12_early_BOOT_wake_still_quiesces_before_resuming_ADC) {
  reset();CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,120));CHECK(beginActive(nullptr));
  int reads=0;
  fake::onRead=[&] {
    if (++reads==1) {audio::setStandby(true);audio::setStandby(false);return;}
    const size_t stopped=event("adc:stop");
    const size_t black=event("led:black",stopped);
    const size_t restarted=event("adc:start",stopped);
    CHECK(stopped<black);CHECK(black<restarted);
    CHECK_EQ(count("adc:start"),2);CHECK_EQ(count("adc:stop"),1);
    CHECK_EQ(count("adc:flush"),3);throw StopTask{};
  };
  try {audio::task(nullptr);} catch(const StopTask&) {}
  CHECK_EQ(reads,2);fake::onRead={};
}
TEST(CTRL_11_display_task_drains_sleeps_blocks_wakes_redraws_then_lights) {
  reset();display::s_power=true;display::s_mode=display::DM_NORMAL;display::s_reqCfg={};
  display::s_feedback[0]=0;display::s_cfgReq=false;display::s_paused=false;display::s_frames=0;
  for(auto& p:display::s_buf) if(!p) p=(uint16_t*)std::malloc(display::STRIPE_PX*2);
  st7789::s_queued=st7789::s_done=st7789::s_err=0;
  int phase=0;size_t sleepAt=0,wakeAt=0;
  fake::onNotify=[&](uint32_t ticks) {
    if(phase==0) {CHECK_EQ(ticks,40);phase=1;return;}
    if(phase==1) {
      CHECK_EQ(display::frames(),1);CHECK_EQ(fake::level[PIN_TFT_BLK],HIGH);
      display::setPower(false);phase=2;return;
    }
    if(phase==2) {
      CHECK_EQ(ticks,portMAX_DELAY);sleepAt=event("cmd:16");
      CHECK(sleepAt<fake::trace.size());CHECK_EQ(fake::level[PIN_TFT_BLK],LOW);
      CHECK(event("dma:done")<sleepAt);CHECK_EQ(display::frames(),1);
      const size_t size=fake::trace.size();display::feedback("Kaydedildi");
      CHECK_EQ(fake::trace.size(),size);CHECK(!display::s_power);
      display::setPower(true);phase=3;return;
    }
    if(phase==3) {
      CHECK_EQ(ticks,40);wakeAt=event("cmd:17",sleepAt+1);
      CHECK(wakeAt<fake::trace.size());CHECK(event("delay:120",wakeAt)>wakeAt);
      CHECK_EQ(fake::level[PIN_TFT_BLK],LOW);phase=4;return;
    }
    CHECK_EQ(display::frames(),2);CHECK_EQ(fake::level[PIN_TFT_BLK],HIGH);
    CHECK_EQ(count("pixels",wakeAt),10);
    CHECK(event("pixels",wakeAt)<event("gpio:19:1",wakeAt));
    CHECK_EQ(st7789::s_done,st7789::s_queued);throw StopTask{};
  };
  try {display::task(nullptr);} catch(const StopTask&) {}
  CHECK_EQ(phase,4);fake::onNotify={};
}
TEST(CTRL_12_cold_standby_panel_stays_uninitialized_black_and_without_splash) {
  reset();display::s_power=false;display::s_mode=display::DM_NORMAL;display::s_frames=0;
  fake::onNotify=[&](uint32_t ticks) {
    CHECK_EQ(ticks,portMAX_DELAY);CHECK_EQ(display::frames(),0);CHECK_EQ(fake::level[PIN_TFT_BLK],LOW);
    CHECK_EQ(count("pixels"),0);CHECK_EQ(count("cmd:17"),0);CHECK_EQ(count("gpio:19:1"),0);throw StopTask{};
  };
  try {display::task(nullptr);} catch(const StopTask&) {}
  fake::onNotify={};
}
TEST(CTRL_11_complete_Turkish_prompts_wrap_without_changing_golden_renderer) {
  for(const char* text : {"Bu modu kaydet: OK","Tuş seç (1–6)","6 dolu (mod 8). Üzerine yaz: OK / iptal: ★","Kayıt başarısız","Yeni OK tuşuna basın"}) {
    display::ControlText page;page.layout(text);
    std::vector<uint32_t> expected,actual;
    const char* p=text;while(*p) {auto c=display::ControlText::next(p);if(c!=' ')expected.push_back(c);}
    for(auto& row:page.cells) for(auto c:row) if(c && c!=' ')actual.push_back(c);
    CHECK(expected==actual);
    uint16_t stripe[fv::SCREEN_W*24];page.render(0,24,stripe);
    CHECK(std::count(std::begin(stripe),std::end(stripe),0)<int(std::size(stripe)));
  }
  for(uint32_t code : {0xfcu,0xdcu,0x15fu,0xe7u,0x131u,0x2013u,0x2605u}) {
    bool visible=false,notQuestion=false;
    for(int x=0;x<5;++x) {
      visible|=display::ControlText::column(code,x)!=0;
      notQuestion|=display::ControlText::column(code,x)!=display::ControlText::column('?',x);
    }
    CHECK(visible);CHECK(notQuestion);
  }
}
TEST(CTRL_12_discarding_input_preserves_calibration_and_analyzer_state) {
  reset();uint16_t samples[399];for(auto& x:samples)x=2000;
  g_engine.pushAdc(samples,399);auto features=g_engine.features();auto params=g_engine.params;
  g_engine.discardInput();CHECK_EQ(memcmp(&features,&g_engine.features(),sizeof(features)),0);
  CHECK_EQ(memcmp(params.v,g_engine.params.v,sizeof(params.v)),0);
  uint16_t next[800];for(auto& x:next)x=2048;
  CHECK_EQ(g_engine.pushAdc(next,799),0);CHECK_EQ(g_engine.pushAdc(next,1),1);
}
TEST(CTRL_6_audio_warnings_publish_owned_snapshots_while_control_lock_is_contended) {
  for(bool limiter : {false,true}) {
    reset();
    if(limiter) {
      g_engine.params.set(fv::P_LEDS,fv::LED_MAX);
      g_engine.params.set(fv::P_MA_BUDGET,100);
      g_engine.reset(1);
    } else fake::adcRaw=4095;
    const char* expected=limiter?"AKIM SINIRI AKTIF":"KIRPMA: KAZANCI KIS";
    CHECK(ws2812::begin(PIN_LED,fv::LED_MAX,g_engine.leds().count()));
    CHECK(beginActive(nullptr));
    display::feedback("Bu modu kaydet: OK",true);
    int blockedEntries=0;
    bool published=false;
    Snapshot warning{};
    // Model the core-1 configuration/text lock being held throughout audio work.
    fake::onCriticalEnter=[&](void* mux) {
      if(mux==&display::s_cfgMux) {++blockedEntries;throw StopTask{};}
    };
    fake::onRead=[&] {
      if(g_snap.read(warning) && std::string(warning.warning)==expected) {
        published=true;throw StopTask{};
      }
      if(fake::adcReads>200) {CHECK(false);throw StopTask{};}
    };
    try {audio::task(nullptr);} catch(const StopTask&) {}
    fake::onCriticalEnter={};fake::onRead={};
    CHECK_EQ(blockedEntries,0);CHECK(published);
    if(limiter) CHECK(g_stats.limTrips>0);else CHECK(g_stats.clip1s>=audio::CLIP_WARN_SAMPLES);
    // A later writer-buffer change cannot mutate the already published label.
    memset(audio::s_out.warning,'x',sizeof(audio::s_out.warning)-1);
    audio::s_out.warning[sizeof(audio::s_out.warning)-1]=0;
    CHECK(g_snap.read(display::s_snap));
    CHECK(std::string(display::s_snap.warning)==expected);
    fv::HudInfo hud{};char label[24];
    display::hudInfo(hud,fake::now,label,sizeof(label));
    CHECK(std::string(hud.label?hud.label:"")==expected);
    fake::now+=10;
    char control[]="BRI 42%";hud_notify(control);memset(control,'x',sizeof(control)-1);
    display::hudInfo(hud,fake::now,label,sizeof(label));
    CHECK(std::string(hud.label?hud.label:"")=="BRI 42%");
    // Preserve the audio warning's original timestamp rather than refreshing it on read.
    display::s_hudLabel[0]=0;
    display::hudInfo(hud,warning.warningAt+display::HUD_MS,label,sizeof(label));
    CHECK(hud.label==nullptr);
    std::printf("AUDIO_WARNING %s cfg_lock_entries=%d copied=1 control_owned=1 expired=1\n",expected,blockedEntries);
  }
}
FV_TEST_MAIN()

#pragma once
#include "controls_harness.h"
#include "../../../firmware/fftviz/cli.cpp"
#include "../../../firmware/fftviz/st7789.cpp"
namespace audio {
void ledOverride(const fv::Rgb*) {}
bool captureRaw(uint32_t,RawStats*,uint32_t,uint16_t*) {return false;}
unsigned stackFree() {return 1000;}
void requestReset() {}
void setSynth(int) {}
int synth() {return -1;}
void setInject(uint32_t,int32_t) {}
}
namespace display {
TaskHandle_t handle() {return nullptr;}
void setPaused(bool) {}
void setSkip(bool) {}
}
namespace selftest {bool dspOnly() {return true;} void bench() {}}
namespace fake {
inline void command(const std::string& s) {Serial.input=s+"\n";cli::poll();}
inline void resetIr() {
  resetControls(); ir::s_learning=false;ir::s_pending=false;ir::s_sampled=false;
  ir::s_dropped=ir::s_truncated=ir::s_record=0;ir::setState(ir::READY);cli::begin();
}
}

#pragma once
#include "Arduino.h"
#include "Preferences.h"
#include "../../../firmware/fftviz/controls.cpp"
#include "../../../firmware/fftviz/powersrc.cpp"
#include "../../../firmware/fftviz/ir_control.cpp"
#include "../../../firmware/fftviz/ir.cpp"
#include "../../../firmware/fftviz/ir_rmt.cpp"
SnapshotLock g_snap;
Stats g_stats = {};
fv::Engine g_engine;
void hud_notify(const char* label) { fake::hud = label; }
namespace display {
void requestConfig(const st7789::Config&) {}
void setPower(bool on) { fake::panelOn = on; fake::trace.push_back(on ? "display:on" : "display:off"); }
void feedback(const char* text, bool) { fake::feedback = text ? text : ""; }
void requestBacklight(uint8_t, bool) {}
}
namespace audio {
void requestCalibrate(int frames) { ++fake::calibrations; fake::calFrames = frames; }
void setStandby(bool on) { fake::audioStandby = on; fake::trace.push_back(on ? "audio:stop" : "audio:start"); }
}
#ifndef FV_REAL_SELFTEST
namespace selftest { bool run(bool) { ++fake::selftests; return true; } }
#endif
namespace fake {
inline void resetControls(bool saved = false) {
  onDelay = {}; onNotify = {};
  now = 0; for(int i=0;i<40;++i) { level[i]=HIGH; mode[i]=0; reads[i]=0; }
  if (!saved) nvs.clear();
  nvsWrites=0; nvsKeys.clear(); nvsTypes.clear(); failNvs=false; calibrations=0; selftests=0;
  hud.clear(); feedback.clear(); trace.clear(); writes.clear(); pwm.clear(); Serial={};
  panelOn=true; audioStandby=false;
  controls::s_dirty=false; controls::s_taint=0; controls::s_anim={};
  g_stats={}; st7789::Config d; controls::load(&d); g_engine.reset(1); controls::begin();
  ir::s_learning=false; ir::s_testing=false; ir::s_pending=false; ir::s_sampled=false;
  ir::setState(ir::READY);
}
inline void pollAt(uint32_t t) { now=t; controls::poll(); }
inline void edge(int pin, bool down, uint32_t t) { level[pin]=down?LOW:HIGH; pollAt(t); pollAt(t+25); }
inline void gesture(int pin, uint32_t hold) {
  uint32_t t=now+50; edge(pin,true,t); edge(pin,false,t+hold);
}
inline ir::Decoded key(ir::Key k) {return {true,false,false,CFG_IR_ADDRESS,uint8_t(ir::KEYS[k].command)};}
inline void receive(const ir::Decoded& d, uint32_t t) { now=t; ir::received(d,nullptr,0,t); }
inline void press(ir::Key k, uint32_t t) { receive(key(k),t); }
inline void release(uint32_t t) { now=t; ir::tick(); }
inline void hold(ir::Key k, uint32_t t, uint32_t duration) {
  press(k,t);
  for (uint32_t d=100; d<duration; d+=100) receive({true,true,false,0,0},t+d);
  receive({true,true,false,0,0},t+duration);
}
}

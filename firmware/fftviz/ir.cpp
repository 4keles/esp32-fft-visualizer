#include "ir.h"
#include <Arduino.h>
#include <string.h>
namespace ir {
static State s_state = RX_DISABLED;
static bool s_learning = false;
static Controller s_controller;
static bool s_testing = false, s_checkArmed = false, s_checkDone = false;
static uint32_t s_inputAt = 0;
static uint32_t s_dropped=0, s_truncated=0, s_record=0, s_sampleAt=0;
static bool s_sampled=false, s_pending=false;
static Symbol s_raw[CAPACITY];
static Decoded s_decoded;
static size_t s_count=0, s_offset=0;
static bool s_header=false;

void setState(State state) { s_state = state; cancelFlow(); s_controller.clear(); }
State state() {return s_state;}
bool learning() {return s_learning;}
static const char* stateName() {
  return s_state==READY?"READY":s_state==ERROR?"ERROR":"DISABLED";
}
static void status() {
  Serial.printf("IR %s learn=%s address=0x%02X LSB-first normalized\n",stateName(),s_learning?"on":"off",CFG_IR_ADDRESS);
  Serial.printf("IR dropped=%lu truncated=%lu\n",(unsigned long)s_dropped,(unsigned long)s_truncated);
  for(unsigned i=0;i<NONE;++i) Serial.printf("IR %s=0x%02X\n",KEYS[i].label,KEYS[i].command);
  Serial.println("IR copy decoded bytes to CFG_IR_ADDRESS / CFG_IR_KEY_<label>");
  Serial.println("IR rebuild; flashing requires explicit user approval");
}
void command(int argc,const char* const* argv) {
  if(argc==1) {status();return;}
  if(argc!=3 || strcmp(argv[1],"learn") || (strcmp(argv[2],"on") && strcmp(argv[2],"off"))) {
    Serial.println("ERR ir: ir | ir learn on|off");return;
  }
  s_learning = !strcmp(argv[2], "on");
  s_controller.clear();
  cancelFlow();
  s_pending=false; s_sampled=false;
  Serial.printf("OK ir learn %s\n",s_learning?"on":"off");
}
void consumeUntilRelease() { s_controller.consume(millis()); }
static void dispatch(Input input) {
  if (!s_testing) { apply(input); return; }
  if (s_checkArmed && input.key == OK && input.gesture == PRESS) s_checkDone = true;
}
void tick() {
  const uint32_t now = millis();
  if (!s_learning) s_controller.tick(now, dispatch);
  if (!s_testing) controlTick(now);
}
void setTesting(bool active) {
  s_testing = active;
  s_checkArmed = s_checkDone = false;
  cancelFlow();
  s_controller.consume(millis());
}
bool waitForOk(uint32_t timeoutMs) {
  if (s_state != READY || s_learning) return false;
  s_checkArmed = s_checkDone = false;
  const uint32_t start = millis();
  s_inputAt = start;
  consumeUntilRelease();
  while (uint32_t(millis() - start) < timeoutMs) {
    poll();
    tick();
    if (uint32_t(millis() - s_inputAt) > 180) s_checkArmed = true;
    if (s_checkDone) { s_checkArmed = false; return true; }
    if (s_state != READY) break;
    delay(5);
  }
  s_checkArmed = false;
  return false;
}
void received(const Decoded& d,const void* symbols,size_t count,uint32_t now) {
  if(s_state!=READY) return;
  if(d.truncated) ++s_truncated;
  s_inputAt = now;
  if (!s_learning) { s_controller.accept(d, now, dispatch); return; }
  if(s_pending || (s_sampled && uint32_t(now-s_sampleAt)<200)) {++s_dropped;return;}
  s_sampleAt=now; s_sampled=true; s_pending=true; ++s_record;
  s_count=count>CAPACITY?CAPACITY:count;
  memcpy(s_raw,symbols,s_count*sizeof(Symbol));
  s_decoded=d; s_decoded.truncated=d.truncated || count>CAPACITY;
  s_offset=0; s_header=true;
}
void drain() {
  if(!s_pending) return;
  char line[64];
  int n;
  if(s_header) {
    n=snprintf(line,sizeof(line),"IR %lu %s a=%02X c=%02X rep=%u unk=%u n=%u t=%u\n",
      (unsigned long)s_record,s_decoded.valid?"NEC":"UNKNOWN",s_decoded.address,s_decoded.command,
      s_decoded.repeat,!s_decoded.valid,(unsigned)s_count,s_decoded.truncated);
  } else {
    Pulse p=pulse(s_raw,s_offset);
    n=snprintf(line,sizeof(line),"IR %lu %u/%u t=%u %c:%u\n",(unsigned long)s_record,
      (unsigned)s_offset,(unsigned)(s_count*2),s_decoded.truncated,p.high?'H':'L',p.us);
  }
  if(Serial.availableForWrite()<n) return;
  Serial.write((const uint8_t*)line,n);
  if(s_header) {s_header=false; if(!s_count) s_pending=false;}
  else if(++s_offset==s_count*2) s_pending=false;
}
}

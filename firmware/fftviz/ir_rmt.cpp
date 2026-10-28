#include <Arduino.h>
#include "pins.h"
#include "ir.h"

namespace ir {
static rmt_data_t s_capture[CAPACITY];
static size_t s_symbols=CAPACITY;
static bool arm() {
  s_symbols=CAPACITY;
  return rmtReadAsync(PIN_IR,s_capture,&s_symbols);
}
void begin() {
  if (PIN_IR == -1) { setState(RX_DISABLED); Serial.println("IR DISABLED"); return; }
  setState(ERROR);
  if(!rmtInit(PIN_IR,RMT_RX_MODE,RMT_MEM_NUM_BLOCKS_1,1000000) ||
     !rmtSetRxMinThreshold(PIN_IR,3) || !rmtSetRxMaxThreshold(PIN_IR,12000) ||
     !arm()) { Serial.println("IR ERROR"); return; }
  setState(READY);
  Serial.println("IR READY");
}
void poll() {
  if(state()!=READY || !rmtReceiveCompleted(PIN_IR)) return;
  const uint32_t now=millis();
  const size_t count=s_symbols;
  const Decoded d=decode(s_capture,count);
  const bool learn=learning();
  if(learn) received(d,s_capture,count,now);
  if (!arm()) { setState(ERROR); Serial.println("IR ERROR rearm"); return; }
  if(!learn) received(d,nullptr,count,now);
}
}

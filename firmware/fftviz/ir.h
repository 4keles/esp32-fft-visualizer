#pragma once
#include "ir_control.h"
namespace ir {
enum State : uint8_t { RX_DISABLED, READY, ERROR };
void setState(State state);
State state();
bool learning();
void command(int argc, const char* const* argv);
void received(const Decoded& decoded, const void* symbols, size_t count, uint32_t now);
void drain();
void begin();
void poll();
void tick();
void setTesting(bool active);
bool waitForOk(uint32_t timeoutMs);
}

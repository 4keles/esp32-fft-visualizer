#pragma once
#include "config.h"
#include "ir_decode.h"
namespace ir {
enum Key : uint8_t { ONE, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, ZERO,
                     LEFT, RIGHT, UP, DOWN, OK, STAR, HASH, NONE };
struct Mapping { const char* label; int command; };
constexpr Mapping KEYS[] = {
  {"1",CFG_IR_KEY_1},{"2",CFG_IR_KEY_2},{"3",CFG_IR_KEY_3},
  {"4",CFG_IR_KEY_4},{"5",CFG_IR_KEY_5},{"6",CFG_IR_KEY_6},
  {"7",CFG_IR_KEY_7},{"8",CFG_IR_KEY_8},{"9",CFG_IR_KEY_9},
  {"0",CFG_IR_KEY_0},{"LEFT",CFG_IR_KEY_LEFT},{"RIGHT",CFG_IR_KEY_RIGHT},
  {"UP",CFG_IR_KEY_UP},{"DOWN",CFG_IR_KEY_DOWN},{"OK",CFG_IR_KEY_OK},
  {"STAR",CFG_IR_KEY_STAR},{"HASH",CFG_IR_KEY_HASH}
};
constexpr bool validMap() {
  for (unsigned i=0;i<NONE;++i) {
    if(KEYS[i].command<0 || KEYS[i].command>255) return false;
    for(unsigned j=i+1;j<NONE;++j) if(KEYS[i].command==KEYS[j].command) return false;
  }
  return true;
}
static_assert(CFG_IR_ADDRESS >= 0 && CFG_IR_ADDRESS <= 255,"CFG_IR_ADDRESS requires an 8-bit NEC address");
static_assert(validMap(),"CFG_IR_KEY commands must be unique 8-bit values");
enum Gesture : uint8_t { PRESS, RELEASE, REPEAT, HOLD, INVALID };
struct Input { Key key; Gesture gesture; uint32_t at; };
class Controller {
 public:
  void clear() { key_ = NONE; blocked_ = false; held_ = false; }
  void consume(uint32_t now) { key_ = NONE; blocked_ = true; seen_ = now; }
  template<class Emit> void tick(uint32_t now, Emit emit) {
    if (uint32_t(now - seen_) <= 180) return;
    if (blocked_) { blocked_ = false; return; }
    if (key_ != NONE) {
      Input end{key_, RELEASE, now};
      key_ = NONE;
      if (!held_) emit(end);
    }
  }
  template<class Emit> void accept(const Decoded& d, uint32_t now, Emit emit) {
    if (blocked_) {
      if (uint32_t(now - seen_) <= 180) { seen_ = now; return; }
      blocked_ = false;
    }
    if (!d.valid) { clear(); emit({NONE, INVALID, now}); return; }
    Key key = NONE;
    if (d.repeat) {
      if (key_ == NONE || uint32_t(now - seen_) > 180) {
        clear(); emit({NONE, INVALID, now}); return;
      }
      key = key_;
    } else {
      if (d.address == CFG_IR_ADDRESS)
        for (unsigned i = 0; i < NONE; ++i)
          if (d.command == KEYS[i].command) { key = Key(i); break; }
      if (key == NONE) { clear(); emit({NONE, INVALID, now}); return; }
    }
    if (key != key_ || uint32_t(now - seen_) > 180) {
      if (key_ != NONE && !held_) emit({key_, RELEASE, now});
      if (blocked_) return;
      key_ = key;
      held_ = false;
      start_ = last_ = seen_ = now;
      emit({key, PRESS, now});
      return;
    }
    seen_ = now;
    const uint32_t duration = now - start_;
    if (!held_ && ((key == HASH && duration >= 3000) ||
        ((key == ZERO || key == SEVEN) && duration >= 2000))) {
      held_ = true;
      emit({key, HOLD, now});
    } else if ((key == UP || key == DOWN) && duration >= 350 &&
               uint32_t(now - last_) >= 110) {
      last_ = now;
      emit({key, REPEAT, now});
    }
  }
 private:
  Key key_ = NONE;
  bool held_ = false, blocked_ = false;
  uint32_t start_ = 0, last_ = 0, seen_ = 0;
};
void apply(Input input);
void controlTick(uint32_t now);
void cancelFlow();
void consumeUntilRelease();
}

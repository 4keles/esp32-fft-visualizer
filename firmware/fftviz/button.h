#pragma once
#include <stdint.h>

namespace controls {
enum Event : uint8_t { EV_NONE = 0, EV_SHORT, EV_LONG };
constexpr uint32_t DEBOUNCE_MS = 25, BOOT_LONG_MS = 2000;
struct Btn {
  bool raw = false, stable = false, down = false, hinted = false;
  uint32_t rawAt = 0, downAt = 0;
};
template<class Notify>
Event buttonStep(Btn& b, bool raw, uint32_t now, Notify notify) {
  if (raw != b.raw) { b.raw = raw; b.rawAt = now; }
  Event ev = EV_NONE;
  if (b.raw != b.stable && uint32_t(now - b.rawAt) >= DEBOUNCE_MS) {
    b.stable = b.raw;
    if (b.stable) {
      b.down = true;
      b.downAt = now;
      b.hinted = false;
    } else if (b.down) {
      b.down = false;
      ev = uint32_t(now - b.downAt) >= BOOT_LONG_MS ? EV_LONG : EV_SHORT;
    }
  }
  if (b.down && !b.hinted && uint32_t(now - b.downAt) >= BOOT_LONG_MS) {
    b.hinted = true;
    notify();
  }
  return ev;
}
}

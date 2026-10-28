#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ir {
constexpr size_t CAPACITY = 64;
struct Symbol { uint32_t val; };
struct Pulse { uint16_t us; bool high; };
struct Decoded {
  bool valid = false, repeat = false, truncated = false;
  uint8_t address = 0, command = 0;
};
template<class S> Pulse pulse(const S* symbols, size_t i) {
  uint32_t half = symbols[i / 2].val >> ((i % 2) * 16);
  return {(uint16_t)(half & 0x7fff), (half & 0x8000) != 0};
}
constexpr bool timing(uint32_t actual, uint32_t nominal) {
  return actual * 4 >= nominal * 3 && actual * 4 <= nominal * 5;
}
template<class S> Decoded decode(const S* symbols, size_t count) {
  Decoded d;
  d.truncated = count >= CAPACITY;
  if (!count || d.truncated) return d;
  size_t first = 0, end = count * 2;
  while (end && pulse(symbols,end-1).us == 0) --end;
  if (end && pulse(symbols,end-1).high && pulse(symbols,end-1).us >= 12000) --end;
  if (end && pulse(symbols,0).high && pulse(symbols,0).us >= 12000) ++first;
  size_t n = end - first;
  if (n != 3 && n != 67) return d;
  for (size_t i=0; i<n; ++i) {
    Pulse p=pulse(symbols,first+i);
    if (!p.us || p.high != (i % 2 == 1)) return d;
  }
  if (!timing(pulse(symbols,first).us,9000) || !timing(pulse(symbols,end-1).us,560)) return d;
  if (n == 3) {
    d.repeat = timing(pulse(symbols,first+1).us,2250);
    d.valid = d.repeat;
    return d;
  }
  if (!timing(pulse(symbols,first+1).us,4500)) return d;
  uint32_t bits=0;
  for(size_t b=0;b<32;++b) {
    if (!timing(pulse(symbols,first+2+b*2).us,560)) return d;
    uint16_t space=pulse(symbols,first+3+b*2).us;
    bool zero=timing(space,560), one=timing(space,1690);
    if (zero == one) return d;
    if (one) bits |= 1u << b;
  }
  uint8_t a=bits, ai=bits>>8, c=bits>>16, ci=bits>>24;
  if ((uint8_t)(a^ai) != 0xff || (uint8_t)(c^ci) != 0xff) return d;
  d.valid=true; d.address=a; d.command=c;
  return d;
}
}

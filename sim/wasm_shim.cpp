// wasm_shim.cpp — freestanding WASM için minimal libc parçaları (memcpy/memset/memmove/memcmp).
// -fno-builtin ile derlenir; derleyicinin ürettiği örtük çağrıları karşılar.
#include <stddef.h>
#include <stdint.h>

extern "C" {

void* memcpy(void* d, const void* s, size_t n) {
  uint8_t* dp = (uint8_t*)d;
  const uint8_t* sp = (const uint8_t*)s;
  while (n--) *dp++ = *sp++;
  return d;
}

void* memmove(void* d, const void* s, size_t n) {
  uint8_t* dp = (uint8_t*)d;
  const uint8_t* sp = (const uint8_t*)s;
  if (dp == sp || n == 0) return d;
  if (dp < sp) {
    while (n--) *dp++ = *sp++;
  } else {
    dp += n;
    sp += n;
    while (n--) *--dp = *--sp;
  }
  return d;
}

void* memset(void* d, int c, size_t n) {
  uint8_t* dp = (uint8_t*)d;
  while (n--) *dp++ = (uint8_t)c;
  return d;
}

int memcmp(const void* a, const void* b, size_t n) {
  const uint8_t* ap = (const uint8_t*)a;
  const uint8_t* bp = (const uint8_t*)b;
  for (size_t i = 0; i < n; ++i)
    if (ap[i] != bp[i]) return ap[i] - bp[i];
  return 0;
}

void __cxa_pure_virtual() { __builtin_trap(); }

}  // extern "C"

// fvtest.h — bağımlılıksız mini test harness.
// Kullanım:
//   #include "fvtest.h"
//   TEST(fft_snr) { CHECK(snr >= 60.0); CHECK_NEAR(a, b, 0.5); }
//   FV_TEST_MAIN()
// Her test dosyası tek bir executable; ctest bunları ayrı ayrı çalıştırır.
// Çıktı: her test için "[PASS] ad" / "[FAIL] ad: dosya:satır ifade", sonda özet.
// Ek metrikler: METRIC("snr_db", snr) → "METRIC test.ad değer" satırı (rapor için ayrıştırılır).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fvtest {
struct Case { const char* name; void (*fn)(); Case* next; };
inline Case*& head() { static Case* h = nullptr; return h; }
inline int& fails() { static int f = 0; return f; }
inline const char*& current() { static const char* c = ""; return c; }
struct Reg { Reg(Case* c) { c->next = head(); head() = c; } };
inline void fail(const char* file, int line, const char* expr) {
  ++fails();
  std::printf("  [FAIL] %s: %s:%d  %s\n", current(), file, line, expr);
}
inline int run_all() {
  // Kayıt sırası ters; kaynak sırasıyla çalıştırmak için çevir.
  Case* rev = nullptr;
  for (Case* c = head(); c;) { Case* n = c->next; c->next = rev; rev = c; c = n; }
  int total = 0, failed = 0;
  for (Case* c = rev; c; c = c->next) {
    ++total;
    current() = c->name;
    int before = fails();
    c->fn();
    bool ok = fails() == before;
    if (!ok) ++failed;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", c->name);
  }
  std::printf("SUMMARY %d/%d passed\n", total - failed, total);
  return failed ? 1 : 0;
}
}  // namespace fvtest

#define FV_CAT2(a, b) a##b
#define FV_CAT(a, b) FV_CAT2(a, b)
#define TEST(name)                                                         \
  static void FV_CAT(test_, name)();                                       \
  static fvtest::Case FV_CAT(case_, name) = {#name, FV_CAT(test_, name), nullptr}; \
  static fvtest::Reg FV_CAT(reg_, name)(&FV_CAT(case_, name));             \
  static void FV_CAT(test_, name)()

#define CHECK(expr) \
  do { if (!(expr)) fvtest::fail(__FILE__, __LINE__, #expr); } while (0)
#define CHECK_EQ(a, b)                                                               \
  do { auto _a = (a); auto _b = (b);                                                 \
    if (!(_a == _b)) { char _m[256]; std::snprintf(_m, sizeof _m, "%s == %s  (%lld vs %lld)", #a, #b, \
      (long long)_a, (long long)_b); fvtest::fail(__FILE__, __LINE__, _m); } } while (0)
#define CHECK_NEAR(a, b, tol)                                                        \
  do { double _a = (double)(a), _b = (double)(b);                                    \
    if (!(std::fabs(_a - _b) <= (tol))) { char _m[256]; std::snprintf(_m, sizeof _m,  \
      "|%s - %s| <= %s  (%g vs %g)", #a, #b, #tol, _a, _b); fvtest::fail(__FILE__, __LINE__, _m); } } while (0)
#define METRIC(key, val) std::printf("METRIC %s.%s %g\n", fvtest::current(), key, (double)(val))

#define FV_TEST_MAIN() int main() { return fvtest::run_all(); }

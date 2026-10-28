// anim.h — parametre otomasyonu: ramp (bir kez doğrusal geçiş) ve sweep (üçgen/sinüs salınım).
// Arduino'dan bağımsız saf mantık (zaman dışarıdan verilir) → tests/test_anim.cpp host'ta çalıştırır.
// Değerler fv::param_info aralığına kırpılır; çekirdeğe (core/) dokunmaz, firmware'de loop görevinde çalışır.
#pragma once
#include <fftviz.h>

namespace anim {

enum Kind : uint8_t { K_NONE = 0, K_RAMP, K_SWEEP };
enum Shape : uint8_t { SH_TRI = 0, SH_SIN };

struct Slot {
  uint8_t kind = K_NONE;
  uint8_t param = 0;
  uint8_t shape = SH_TRI;
  int32_t a = 0, b = 0;   // ramp: başlangıç, hedef | sweep: alt, üst
  uint32_t t0 = 0, dur = 0;  // başlangıç zamanı (ms), süre/periyot (ms)
};

constexpr int kSlots = 4;
constexpr uint32_t kMinPeriodMs = 100;

// t anındaki değer (saf). Ramp için süre dolunca hedef; sweep alt sınırdan başlar.
inline int32_t slot_value(const Slot& s, uint32_t now) {
  const uint32_t e = now - s.t0;   // uint32 sarmalı güvenli
  if (s.kind == K_RAMP) {
    if (e >= s.dur) return s.b;
    return s.a + (int32_t)(((int64_t)(s.b - s.a) * (int64_t)e) / (int64_t)s.dur);
  }
  const uint32_t ph = e % s.dur;
  int64_t w, q;   // w/q: 0 = alt .. 1 = üst
  if (s.shape == SH_SIN) {
    // alt sınırdan başlar: (1 − cos θ)/2
    const uint16_t ph16 = (uint16_t)(((uint64_t)ph << 16) / s.dur);
    w = (int64_t)fv::sin16((uint16_t)(ph16 + 49152u)) + 32767; q = 65534;
  } else {
    w = (ph * 2u < s.dur) ? (int64_t)ph * 2 : (int64_t)(s.dur - ph) * 2; q = s.dur;
  }
  return s.a + (int32_t)(((int64_t)(s.b - s.a) * w) / q);
}

class Table {
 public:
  // Başarıda nullptr, aksi halde Türkçe hata metni.
  const char* ramp(fv::ParamId id, int32_t from, int32_t target, uint32_t durMs, uint32_t now) {
    if (const char* e = check(id)) return e;
    if (durMs == 0) return "süre > 0 olmalı (ms)";
    const fv::ParamInfo& in = fv::param_info(id);
    Slot* s = take(id);
    if (!s) return "otomasyon tablosu dolu (anim ile bakın, ramp stop <param>)";
    *s = Slot{K_RAMP, (uint8_t)id, SH_TRI, clampv(from, in), clampv(target, in), now, durMs};
    return nullptr;
  }
  const char* sweep(fv::ParamId id, int32_t lo, int32_t hi, uint32_t periodMs, Shape sh, uint32_t now) {
    if (const char* e = check(id)) return e;
    if (periodMs < kMinPeriodMs) return "periyot en az 100 ms";
    const fv::ParamInfo& in = fv::param_info(id);
    lo = clampv(lo, in); hi = clampv(hi, in);
    if (lo > hi) { const int32_t t = lo; lo = hi; hi = t; }
    Slot* s = take(id);
    if (!s) return "otomasyon tablosu dolu (anim ile bakın, sweep stop <param>)";
    *s = Slot{K_SWEEP, (uint8_t)id, (uint8_t)sh, lo, hi, now, periodMs};
    return nullptr;
  }
  // kind = K_NONE → ikisi de; id < 0 → tüm parametreler. Durdurulan slot sayısını döner.
  int stop(Kind kind, int id) {
    int n = 0;
    for (Slot& s : slots_) {
      if (s.kind == K_NONE || (kind != K_NONE && s.kind != kind) || (id >= 0 && s.param != id)) continue;
      s.kind = K_NONE; ++n;
    }
    return n;
  }
  bool active(fv::ParamId id) const {
    for (const Slot& s : slots_) if (s.kind != K_NONE && s.param == id) return true;
    return false;
  }
  int count() const { int n = 0; for (const Slot& s : slots_) n += s.kind != K_NONE; return n; }
  const Slot& slot(int i) const { return slots_[i]; }
  void resume(uint32_t since, uint32_t now) {
    for (Slot& s : slots_) if (s.kind != K_NONE) {
      const uint32_t age = now - s.t0, paused = now - since;
      s.t0 += age < paused ? age : paused;
    }
  }
  // Her slotun değerini hesaplayıp apply(id, değer) çağırır; biten ramp'i serbest bırakır.
  template <class F> void tick(uint32_t now, F apply) {
    for (Slot& s : slots_) {
      if (s.kind == K_NONE) continue;
      apply((fv::ParamId)s.param, slot_value(s, now));
      if (s.kind == K_RAMP && now - s.t0 >= s.dur) s.kind = K_NONE;
    }
  }

 private:
  static const char* check(fv::ParamId id) {
    if (id >= fv::P_COUNT) return "bilinmeyen parametre";
    if (fv::param_info(id).flags & fv::PF_NORAMP) return "bu parametre otomasyona uygun değil (seçim/yeniden düzen gerektirir)";
    return nullptr;
  }
  static int32_t clampv(int32_t v, const fv::ParamInfo& in) { return v < in.min ? in.min : (v > in.max ? in.max : v); }
  Slot* take(fv::ParamId id) {   // aynı parametrenin slotunu yeniden kullan, yoksa boş slot
    for (Slot& s : slots_) if (s.kind != K_NONE && s.param == id) return &s;
    for (Slot& s : slots_) if (s.kind == K_NONE) return &s;
    return nullptr;
  }
  Slot slots_[kSlots];
};

}  // namespace anim

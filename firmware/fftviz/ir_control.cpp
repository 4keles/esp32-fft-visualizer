#include "ir_control.h"
#include "controls.h"
#include "display_task.h"
#include "shared.h"
namespace ir {
enum SaveState : uint8_t { IDLE, CONFIRM_MODE, SELECT_SLOT, CONFIRM_SLOT };
static SaveState s_save = IDLE;
static bool s_star = false;
static uint32_t s_starAt = 0, s_advancedAt = 0;
static uint8_t s_mode = 0, s_slot = 0;

void cancelFlow() {
  if (s_save != IDLE) { display::feedback(nullptr); consumeUntilRelease(); }
  s_save = IDLE;
  s_star = false;
}
void controlTick(uint32_t now) {
  if (s_save != IDLE && uint32_t(now - s_advancedAt) >= 10000) cancelFlow();
  if (s_star && uint32_t(now - s_starAt) > 500) s_star = false;
}
void apply(Input input) {
  const bool wasModal = s_save != IDLE;
  controlTick(input.at);
  if (wasModal && s_save == IDLE) return;
  const Key key = input.key;
  const Gesture gesture = input.gesture;
  if (gesture == INVALID) { s_star = false; return; }
  if (controls::standby()) {
    if (key == SEVEN && gesture == HOLD) controls::toggleStandby();
    return;
  }
  if (s_save != IDLE) {
    if (gesture != PRESS) return;
    if (key == STAR) { cancelFlow(); return; }
    if (s_save == CONFIRM_MODE && key == OK) {
      s_save = SELECT_SLOT;
      display::feedback("Tuş seç (1–6)", true);
    } else if (s_save == SELECT_SLOT && key <= SIX) {
      s_slot = key;
      s_save = CONFIRM_SLOT;
      char text[96];
      const int saved = controls::remoteSlot(s_slot);
      if (saved < 0) snprintf(text, sizeof(text), "%u'e kaydet: OK", s_slot + 1);
      else snprintf(text, sizeof(text), "%u dolu (mod %d). Üzerine yaz: OK / iptal: ★", s_slot + 1, saved);
      display::feedback(text, true);
    } else if (s_save == CONFIRM_SLOT && key == OK) {
      const bool saved = controls::saveRemoteSlot(s_slot, s_mode);
      s_save = IDLE;
      s_star = false;
      display::feedback(saved ? "Kaydedildi" : "Kayıt başarısız");
    } else return;
    s_advancedAt = input.at;
    return;
  }
  if (gesture == PRESS) {
    if (key == STAR) {
      if (s_star && uint32_t(input.at - s_starAt) <= 500) {
        s_star = false;
        s_mode = g_engine.params.get(fv::P_LED_MODE);
        s_save = CONFIRM_MODE;
        s_advancedAt = input.at;
        display::feedback("Bu modu kaydet: OK", true);
      } else { s_star = true; s_starAt = input.at; }
      return;
    }
    s_star = false;
    if (key <= SIX) {
      const int mode = controls::remoteSlot(key);
      if (mode < 0) display::feedback("boş");
      else controls::selectMode(mode);
      return;
    }
    switch (key) {
      case LEFT: controls::nextMode(-1); break;
      case RIGHT: controls::nextMode(); break;
      case UP: controls::brightnessDelta(16); break;
      case DOWN: controls::brightnessDelta(-16); break;
      case OK: controls::nextTheme(); break;
      case NINE: controls::cycleSensitivity(); break;
      default: break;
    }
  } else if (gesture == REPEAT) {
    if (key == UP) controls::brightnessDelta(16);
    else if (key == DOWN) controls::brightnessDelta(-16);
  } else if (gesture == HOLD) {
    if (key == HASH) controls::calibrate();
    else if (key == ZERO) controls::toggleDisplay();
    else if (key == SEVEN) controls::toggleStandby();
  } else if (gesture == RELEASE && key == ZERO) controls::toggleHud();
}
}

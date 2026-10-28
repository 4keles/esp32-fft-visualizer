#include "fvtest.h"
#include "fftviz.h"
TEST(params_defaults) {
  fv::Params p; p.defaults();
  CHECK_EQ(p.get(fv::P_BRIGHTNESS), 255);
  CHECK(p.set(fv::P_BRIGHTNESS, 100));
  CHECK(p.set(fv::P_BRIGHTNESS, 999));
  CHECK_EQ(p.get(fv::P_BRIGHTNESS), 255);
  fv::ParamId id; CHECK(fv::param_lookup("ma", &id)); CHECK_EQ((int)id, (int)fv::P_MA_BUDGET);
}
TEST(engine_links) {
  static fv::Engine e; e.params.defaults(); e.reset(1);
  static fv::ScreenRenderer r; static uint16_t buf[fv::SCREEN_W * fv::STRIPE_ROWS];
  fv::HudInfo h{nullptr, 0, 0, 0}; r.beginFrame(e.features(), e.params, h, 0); r.renderRows(0, fv::STRIPE_ROWS, buf);
  CHECK(true);
}
FV_TEST_MAIN()

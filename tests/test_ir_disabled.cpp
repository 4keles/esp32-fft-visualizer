#define CFG_PIN_IR -1
#include "fvtest.h"
#include "fixtures/v2/cli_harness.h"
TEST(CTRL_6_disabled_receiver_has_zero_HAL_access) {
  fake::resetIr();fake::resetRmt();ir::begin();
  for(int i=0;i<100;++i) ir::poll();
  CHECK_EQ(ir::state(),ir::RX_DISABLED);CHECK_EQ(fake::rmtCalls,0);CHECK(fake::inits.empty());
  fake::gesture(PIN_BTN_BOOT,100); CHECK_EQ(g_engine.params.get(fv::P_HUD),1);
  fake::command("ir");CHECK(Serial.output.find("DISABLED")!=std::string::npos);
}
FV_TEST_MAIN()

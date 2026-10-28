#include "fvtest.h"
#include "../firmware/fftviz/pins.h"
#include "fixtures/v2/controls_harness.h"
#include "../firmware/fftviz/st7789.cpp"
#include <cstdlib>
#include <string>

TEST(CTRL_1_GPIO4_short_circuit_regression) {
  CHECK(PIN_TFT_BLK != 4);
  CHECK_EQ(PIN_TFT_BLK, 19);
  for (int level : {LOW, HIGH}) {
    fake::resetControls();fake::level[4]=level;controls::begin();
    st7789::s_bus=false;CHECK(st7789::begin({}));st7789::backlightConst();
    CHECK_EQ(fake::mode[4],INPUT_PULLDOWN);CHECK_EQ(fake::reads[4],0);
    CHECK_EQ(fake::level[4],level);
  }
  CHECK_EQ(fake::mode[19],OUTPUT);CHECK_EQ(fake::level[19],HIGH);
  for(int pin:fake::writes) CHECK(pin!=4);
  for(int pin:fake::pwm) CHECK(pin!=4);
  st7789::backlight(100);st7789::backlightConst();
  for(int pin:fake::pwm) CHECK_EQ(pin,19);
  CHECK_EQ(fake::spiBus.miso_io_num,-1);
}
TEST(CTRL_1_canonical_pin_cross_check) {
  CHECK_EQ(PIN_TFT_SCK, 18); CHECK_EQ(PIN_TFT_MOSI, 23);
  CHECK_EQ(PIN_TFT_DC, 21); CHECK_EQ(PIN_TFT_RST, 22);
  CHECK_EQ(PIN_MIC, 34); CHECK_EQ(PIN_LED, 25);
  CHECK_EQ(PIN_IR, 16); CHECK_EQ(PIN_BTN_BOOT, 0);
  CHECK(!ADP_DETECT);
}
TEST(CTRL_1_invalid_configuration_is_rejected) {
  const char* invalid[] = {
    "-DCFG_PIN_TFT_BLK=4", "-DCFG_PIN_TFT_BLK=25",
    "-DCFG_PIN_LED=6", "-DCFG_PIN_IR=0", "-DCFG_PIN_TFT_RST=35",
    "-DCFG_PIN_MIC=25",
    "-DCFG_PIN_TFT_DC=33", "-DCFG_PIN_TFT_SCK=19", "-DCFG_PIN_BTN_BOOT=4",
    "-DCFG_PIN_LED=20", "-DCFG_PIN_IR=-2",
    "-DCFG_ADP_DETECT=1 -DCFG_PIN_ADP_OK=16"
  };
  const std::string base = std::string("c++ -std=c++17 -fsyntax-only -I\"") + FV_SOURCE_DIR +
    "/firmware/fftviz\" \"" + FV_SOURCE_DIR + "/tests/fixtures/v2/pins.cpp\" ";
  for (const char* flags : invalid) CHECK(std::system((base + flags).c_str()) != 0);
  CHECK_EQ(std::system((base + "-DCFG_PIN_IR=-1").c_str()), 0);
  CHECK_EQ(std::system((base + "-DCFG_ADP_DETECT=1").c_str()), 0);
}
FV_TEST_MAIN()

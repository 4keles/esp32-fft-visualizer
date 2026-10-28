#include "fvtest.h"
#include "fixtures/v2/cli_harness.h"
#include "fixtures/v2/nec.h"
static void receive(const std::vector<ir::Symbol>& s) {ir::received(ir::decode(s.data(),s.size()),s.data(),s.size(),fake::now);}
TEST(CTRL_5_commands_help_status_and_invalid_arguments) {
  fake::resetIr();fake::command("help ir");CHECK(Serial.output.find("CFG_IR_ADDRESS")!=std::string::npos);
  Serial.output.clear();fake::command("ir");CHECK(Serial.output.find("READY")!=std::string::npos);
  for(unsigned i=0;i<ir::NONE;++i) CHECK(Serial.output.find(std::string(ir::KEYS[i].label)+"=")!=std::string::npos);
  fake::command("ir learn on");CHECK(ir::learning());
  for(const char* s : {"ir learn","ir learn yes","ir on","ir learn off extra","ir extra"}) {
    Serial.output.clear();fake::command(s);CHECK(ir::learning());CHECK(Serial.output.find("ERR ir")!=std::string::npos);
  }
  fake::command("ir learn off");CHECK(!ir::learning());
  ir::setState(ir::RX_DISABLED);Serial.output.clear();fake::command("ir");CHECK(Serial.output.find("DISABLED")!=std::string::npos);
  ir::setState(ir::ERROR);Serial.output.clear();fake::command("ir");CHECK(Serial.output.find("ERROR")!=std::string::npos);
}
TEST(CTRL_5_learning_suppresses_all_actions_without_nvs_writes) {
  fake::resetIr();fake::command("ir learn on");auto before=g_engine.params;
  for(unsigned i=0;i<ir::NONE;++i) {fake::now+=250;receive(vectors::frame(ir::KEYS[i].command));}
  CHECK_EQ(memcmp(before.v,g_engine.params.v,sizeof(before.v)),0);
  CHECK_EQ(fake::calibrations,0);CHECK(!controls::s_dirty);CHECK_EQ(fake::nvsWrites,0);
  fake::command("ir learn off");receive(vectors::repeat());CHECK_EQ(fake::calibrations,0);
  receive(vectors::frame(CFG_IR_KEY_HASH));CHECK_EQ(fake::calibrations,0);
  fake::command("ir learn on");fake::command("ir learn off");receive(vectors::repeat());CHECK_EQ(fake::calibrations,0);
}
TEST(CTRL_5_raw_unknown_truncated_and_nonblocking_backpressure) {
  fake::resetIr();fake::command("ir learn on");Serial.output.clear();
  auto s=vectors::frame(0xaa,0x24);receive(s);
  Serial.capacity=0;for(int i=0;i<100;++i) {ir::drain();receive(s);}
  CHECK(Serial.output.empty());CHECK_EQ(ir::s_dropped,100);
  Serial.capacity=64;
  for(int i=0;i<150;++i) {size_t at=Serial.output.size();ir::drain();size_t n=Serial.output.size()-at;CHECK(n<=63);if(n)CHECK(Serial.output.back()=='\n');}
  CHECK(Serial.output.find("NEC a=24 c=AA")!=std::string::npos);
  CHECK(Serial.output.find("L:9000")!=std::string::npos);CHECK(Serial.output.find("H:4500")!=std::string::npos);
  fake::now+=200;s[0].val=123;receive(s);Serial.output.clear();ir::drain();CHECK(Serial.output.find("UNKNOWN")!=std::string::npos);
  fake::command("ir learn off");fake::command("ir learn on");s.resize(64);receive(s);
  Serial.output.clear();ir::drain();CHECK(Serial.output.find("t=1")!=std::string::npos);
  CHECK_EQ(ir::s_truncated,1);
  fake::command("ir");CHECK(Serial.output.find("dropped=100 truncated=1")!=std::string::npos);
}
TEST(CTRL_5_sampling_JSON_integrity_and_existing_dirty_save) {
  fake::resetIr();controls::setParam(fv::P_BRIGHTNESS,100,false);uint32_t dirtyAt=controls::s_dirtyAt;
  fake::command("ir learn on");auto s=vectors::frame();receive(s);
  for(int i=0;i<150;++i) ir::drain();
  receive(s);CHECK_EQ(ir::s_dropped,1);
  fake::now+=200;receive(s);Serial.output.clear();fake::command("get");ir::drain();
  auto end=Serial.output.find('\n');CHECK(Serial.output[0]=='{');CHECK(Serial.output[end-1]=='}');CHECK(Serial.output.find("IR ")==end+1);
  CHECK_EQ(controls::s_dirtyAt,dirtyAt);fake::pollAt(dirtyAt+5000);CHECK(fake::nvsWrites>0);
  fake::command("ir learn off");Serial.output.clear();receive(s);ir::drain();CHECK(Serial.output.empty());
}
FV_TEST_MAIN()

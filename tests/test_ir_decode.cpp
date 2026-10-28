#include "fvtest.h"
#include "fixtures/v2/nec.h"
static ir::Decoded decode(const std::vector<ir::Pulse>& p) {auto s=vectors::pack(p);return ir::decode(s.data(),s.size());}
TEST(CTRL_4_valid_nominal_and_integer_tolerance_boundaries) {
  auto p=vectors::full(0x45,0x12);auto d=decode(p);
  CHECK(d.valid);CHECK_EQ(d.address,0x12);CHECK_EQ(d.command,0x45);CHECK(!d.repeat);
  for(size_t i=0;i<p.size();++i) {
    auto q=p; auto n=p[i].us;
    q[i].us=(n*3+3)/4;CHECK(decode(q).valid); --q[i].us;CHECK(!decode(q).valid);
    q[i].us=n*5/4;CHECK(decode(q).valid); ++q[i].us;CHECK(!decode(q).valid);
  }
  auto r=vectors::repeat();CHECK(ir::decode(r.data(),r.size()).repeat);
  for(int n : {1688,2812}) {auto r=vectors::pack({{9000,false},{uint16_t(n),true},{560,false}}); CHECK(ir::decode(r.data(),r.size()).valid);}
}
TEST(CTRL_4_damaged_polarity_length_inverses_unsupported) {
  auto p=vectors::full();
  for(size_t i=0;i<p.size();++i) {auto q=p;q[i].high=!q[i].high;CHECK(!decode(q).valid);}
  for(size_t n=0;n<p.size();++n) {auto q=p;q.resize(n);CHECK(!decode(q).valid);}
  for(int i : {3,19,35,51}) {auto q=p;q[i].us=q[i].us==560?1690:560;CHECK(!decode(q).valid);}
  auto q=p;q.push_back({560,true});q.push_back({560,false});CHECK(!decode(q).valid);
  q=p;q[1].us=3000;CHECK(!decode(q).valid);
  auto s=vectors::frame();s.resize(64);auto d=ir::decode(s.data(),s.size());CHECK(!d.valid);CHECK(d.truncated);
  s.resize(65);CHECK(ir::decode(s.data(),s.size()).truncated);
}
TEST(CTRL_4_capture_boundary_idle_and_raw_representation) {
  auto p=vectors::full();p.insert(p.begin(),{12000,true});p.push_back({12000,true});
  auto s=vectors::pack(p);CHECK(ir::decode(s.data(),s.size()).valid);
  for(size_t i=0;i<p.size();++i) {auto v=ir::pulse(s.data(),i);CHECK_EQ(v.us,p[i].us);CHECK_EQ(v.high,p[i].high);}
  p[4].us=0;CHECK(!decode(p).valid);
}
FV_TEST_MAIN()

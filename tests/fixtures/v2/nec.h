#pragma once
#include "../../../firmware/fftviz/ir_decode.h"
#include <vector>
namespace vectors {
inline std::vector<ir::Symbol> pack(const std::vector<ir::Pulse>& pulses) {
  std::vector<ir::Symbol> s((pulses.size()+1)/2);
  for(size_t i=0;i<pulses.size();++i)
    s[i/2].val |= (uint32_t(pulses[i].us) | (pulses[i].high?0x8000u:0)) << ((i%2)*16);
  return s;
}
inline std::vector<ir::Pulse> full(uint8_t command=0x18,uint8_t address=0) {
  std::vector<ir::Pulse> p={{9000,false},{4500,true}};
  uint32_t bits=address|(uint32_t(uint8_t(~address))<<8)|(uint32_t(command)<<16)|(uint32_t(uint8_t(~command))<<24);
  for(int i=0;i<32;++i) {p.push_back({560,false});p.push_back({uint16_t((bits>>i)&1?1690:560),true});}
  p.push_back({560,false});return p;
}
inline std::vector<ir::Symbol> frame(uint8_t command=0x18,uint8_t address=0) { return pack(full(command,address)); }
inline std::vector<ir::Symbol> repeat() {return pack({{9000,false},{2250,true},{560,false}});}
}

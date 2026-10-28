#pragma once
#include <cstring>
#include <cstdlib>
#include <vector>
union rmt_data_t {uint32_t val; struct {uint32_t duration0:15,level0:1,duration1:15,level1:1;};};
enum {RMT_TX_MODE=0,RMT_RX_MODE=1,RMT_MEM_NUM_BLOCKS_1=1,RMT_MEM_NUM_BLOCKS_4=4};
namespace fake {
struct RmtInit {int pin, mode, blocks; uint32_t hz;};
inline std::vector<RmtInit> inits;
inline int rmtCalls=0, arms=0, tx=0, failAt=0;
inline bool complete=false, txComplete=true;
inline rmt_data_t* rxData=nullptr;
inline size_t* rxCount=nullptr;
inline int filter=0,idle=0;
inline bool rmtOk() {return ++rmtCalls!=failAt;}
inline void resetRmt() {inits.clear();rmtCalls=arms=tx=failAt=0;complete=false;txComplete=true;rxData=nullptr;rxCount=nullptr;}
template<class S> void capture(const std::vector<S>& data) {
  size_t n=data.size()<*rxCount?data.size():*rxCount;
  memcpy(rxData,data.data(),n*sizeof(rmt_data_t));*rxCount=n;complete=true;
}
}
inline bool rmtInit(int p,int m,int b,uint32_t hz) {fake::inits.push_back({p,m,b,hz});return fake::rmtOk();}
inline bool rmtSetRxMinThreshold(int,uint8_t t) {fake::filter=t;return fake::rmtOk();}
inline bool rmtSetRxMaxThreshold(int,uint16_t t) {fake::idle=t;return fake::rmtOk();}
inline bool rmtSetCarrier(int p,bool,bool,uint32_t,float) {
  bool rx=false;
  for(const auto& i:fake::inits) if(i.pin==p) rx=i.mode==RMT_RX_MODE;
  return fake::rmtOk() && !rx;
}
inline bool rmtReadAsync(int,rmt_data_t* d,size_t* n) {++fake::arms;fake::rxData=d;fake::rxCount=n;fake::complete=false;return *n==64 && fake::rmtOk() && fake::filter*1000<=3187;}
inline bool rmtReceiveCompleted(int) {++fake::rmtCalls;return fake::complete;}
inline bool rmtSetEOT(int,int) {return true;}
inline bool rmtTransmitCompleted(int) {return fake::txComplete;}
inline bool rmtWriteAsync(int,rmt_data_t* data,size_t n) {
  ++fake::tx;
  bool black=true;
  for(size_t i=0;i+1<n;++i) if(data[i].duration0>data[i].duration1) black=false;
  fake::trace.push_back(black?"led:black":"led:frame");
  return true;
}
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n,int) {return std::malloc(n);}
inline void heap_caps_free(void* p) {std::free(p);}
inline void* xSemaphoreCreateMutex() {return (void*)1;}
inline int xSemaphoreTake(void*,uint32_t) {return 1;}
inline void xSemaphoreGive(void*) {}
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)

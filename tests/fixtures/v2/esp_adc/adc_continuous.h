#pragma once
#include "Arduino.h"
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define SOC_ADC_DIGI_RESULT_BYTES 2
#define ADC_CHANNEL_4 4
#define ADC_ATTEN_DB_12 12
#define ADC_BITWIDTH_12 12
#define ADC_UNIT_1 1
#define ADC_CONV_SINGLE_UNIT_1 1
#define ADC_DIGI_OUTPUT_FORMAT_TYPE1 1
using esp_err_t = int;
using adc_channel_t = int;
using adc_continuous_handle_t = void*;
struct adc_continuous_evt_data_t {};
struct adc_continuous_handle_cfg_t { unsigned max_store_buf_size, conv_frame_size; struct { unsigned flush_pool; } flags; };
struct adc_digi_pattern_config_t { int atten,channel,unit,bit_width; };
struct adc_continuous_config_t { unsigned pattern_num; adc_digi_pattern_config_t* adc_pattern; unsigned sample_freq_hz; int conv_mode,format; };
struct adc_continuous_evt_cbs_t { bool (*on_pool_ovf)(adc_continuous_handle_t,const adc_continuous_evt_data_t*,void*); };
namespace fake {
inline bool adcRunning = false;
inline int adcReads = 0;
inline uint16_t adcRaw = 2048;
inline std::function<void()> onRead;
}
inline int adc_continuous_new_handle(const adc_continuous_handle_cfg_t*,void** h) {*h=(void*)1;return ESP_OK;}
inline int adc_continuous_config(void*,const adc_continuous_config_t*) {return ESP_OK;}
inline int adc_continuous_register_event_callbacks(void*,const adc_continuous_evt_cbs_t*,void*) {return ESP_OK;}
inline int adc_continuous_start(void*) {fake::trace.push_back("adc:start");fake::adcRunning=true;return ESP_OK;}
inline int adc_continuous_stop(void*) {fake::trace.push_back("adc:stop");fake::adcRunning=false;return ESP_OK;}
inline int adc_continuous_flush_pool(void*) {fake::trace.push_back("adc:flush");return ESP_OK;}
inline int adc_continuous_read(void*,uint8_t* dst,unsigned bytes,uint32_t* len,unsigned) {
  ++fake::adcReads;fake::trace.push_back("adc:read");
  if(fake::onRead) fake::onRead();
  auto* p=(uint16_t*)dst;
  for(unsigned i=0;i<bytes/2;++i) p[i]=(6<<12)|fake::adcRaw;
  *len=bytes;fake::now+=17;return ESP_OK;
}

#pragma once
#include "adc_continuous.h"
using adc_cali_handle_t = void*;
inline int adc_cali_raw_to_voltage(void*,int raw,int* mv) {*mv=raw*3100/4095;return ESP_OK;}

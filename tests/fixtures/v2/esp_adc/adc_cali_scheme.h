#pragma once
#include "adc_cali.h"
struct adc_cali_line_fitting_config_t {int unit_id,atten,bitwidth,default_vref;};
inline int adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t*,void** h) {*h=(void*)1;return ESP_OK;}

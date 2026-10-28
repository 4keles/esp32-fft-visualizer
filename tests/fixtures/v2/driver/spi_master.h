#pragma once
#include <stddef.h>
#include <stdint.h>
#define IRAM_ATTR
#define ESP_OK 0
#define SPI3_HOST 3
#define SPI_DMA_CH_AUTO 1
#define SPI_DEVICE_NO_DUMMY 1
#define SPI_TRANS_USE_TXDATA 1
#define portMAX_DELAY 0xffffffffu
using esp_err_t=int;
using spi_device_handle_t=void*;
struct spi_transaction_t {uint32_t flags;size_t length;uint8_t tx_data[4];const void* tx_buffer;void* user;};
struct spi_device_interface_config_t {int mode,clock_speed_hz,spics_io_num,queue_size,flags;void (*pre_cb)(spi_transaction_t*);};
struct spi_bus_config_t {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;};
namespace fake {inline spi_bus_config_t spiBus={};}
inline int spi_device_get_trans_result(void*,spi_transaction_t**,uint32_t) {fake::trace.push_back("dma:done");return ESP_OK;}
inline int spi_device_queue_trans(void*,spi_transaction_t* t,uint32_t) {
  if (!t->user) fake::trace.push_back("cmd:"+std::to_string(t->tx_data[0]));
  else if (t->length > 32) fake::trace.push_back("pixels");
  return ESP_OK;
}
inline int spi_bus_add_device(int,const spi_device_interface_config_t*,void** d) {*d=(void*)1;return ESP_OK;}
inline int spi_bus_initialize(int,const spi_bus_config_t* c,int) {fake::spiBus=*c;return ESP_OK;}
inline int spi_bus_remove_device(void*) {return ESP_OK;}

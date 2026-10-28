#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <functional>
#include <cmath>
#define LOW 0
#define HIGH 1
#define INPUT 1
#define OUTPUT 2
#define INPUT_PULLUP 3
#define INPUT_PULLDOWN 4
#define DISABLED 0x00
#define F(x) x
#define ESP_OK 0
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
namespace fake {
inline uint32_t now = 0;
inline int level[40] = {}, mode[40] = {}, reads[40] = {};
inline std::vector<int> writes, pwm;
inline std::string hud;
inline int calibrations = 0, calFrames = 0, selftests = 0;
inline std::string feedback;
inline bool panelOn = true, audioStandby = false;
inline std::vector<std::string> trace;
inline std::function<void(uint32_t)> onDelay, onNotify;
inline std::function<void(void*)> onCriticalEnter;
inline void (*taskEntry)(void*) = nullptr;
}
inline uint32_t millis() { return fake::now; }
inline uint32_t micros() { return fake::now * 1000; }
inline void delay(uint32_t ms) {
  fake::trace.push_back("delay:" + std::to_string(ms));
  fake::now += ms; if (fake::onDelay) fake::onDelay(ms);
}
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(int pin, int mode) { fake::mode[pin] = mode; }
inline int digitalRead(int pin) { ++fake::reads[pin]; return fake::level[pin]; }
inline void digitalWrite(int pin, int value) {
  fake::writes.push_back(pin); fake::level[pin] = value;
  fake::trace.push_back("gpio:" + std::to_string(pin) + ":" + std::to_string(value));
}
inline bool ledcAttach(int pin, int, int) { fake::pwm.push_back(pin); return true; }
inline bool ledcDetach(int) { return true; }
inline bool ledcWrite(int pin, int) { fake::pwm.push_back(pin); return true; }
template<class T> inline T constrain(T x, T lo, T hi) { return std::min(hi, std::max(lo, x)); }
struct FakeSerial {
  std::string input, output;
  int capacity = 4096;
  int available() { return input.size(); }
  int read() { int c = input[0]; input.erase(0,1); return c; }
  int availableForWrite() { return capacity; }
  size_t write(const uint8_t* s, size_t n) { output.append((const char*)s,n); return n; }
  void print(const char* s) { output += s; }
  void print(char c) { output += c; }
  void println(const char* s = "") { output += s; output += '\n'; }
  void printf(const char* fmt, ...) {
    char b[4096]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof(b),fmt,a); va_end(a); output += b;
  }
  void flush() {}
  void begin(int) {}
};
inline FakeSerial Serial;
struct FakeESP { int getFreeHeap() { return 100000; } int getMinFreeHeap() {return 90000;} void restart() {} };
inline FakeESP ESP;
inline uint32_t getCpuFrequencyMhz() { return 240; }
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) {return 1000;}
#include "rmt.h"
#define portMAX_DELAY 0xffffffffu
#define pdPASS 1
#define MALLOC_CAP_DMA 4
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE* mux) { if (fake::onCriticalEnter) fake::onCriticalEnter(mux); }
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
inline int xTaskCreatePinnedToCore(void (*entry)(void*),const char*,unsigned,void*,int,void** handle,int) {
  fake::taskEntry = entry; *handle = (void*)1; return pdPASS;
}
inline void xTaskNotifyGive(void*) {}
inline uint32_t ulTaskNotifyTake(int,uint32_t ticks) {
  if (fake::onNotify) fake::onNotify(ticks);
  return 1;
}
inline void vTaskDelay(uint32_t ticks) { delay(ticks); }
inline unsigned uxTaskPriorityGet(void*) { return 1; }
inline void vTaskPrioritySet(void*,unsigned) {}
using UBaseType_t = unsigned;
inline void* heap_caps_aligned_alloc(size_t align,size_t n,int) { return std::aligned_alloc(align,(n+align-1)/align*align); }
inline size_t heap_caps_get_largest_free_block(int) {return 100000;}

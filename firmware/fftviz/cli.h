// cli.h — 115200 baud satır tabanlı seri komut arayüzü.
#pragma once
#include <Arduino.h>

namespace cli {

void begin();
void poll();        // loop'tan çağrılır: satır okur, periyodik stats/stream basar
void printStats();  // tek satır JSON

}  // namespace cli

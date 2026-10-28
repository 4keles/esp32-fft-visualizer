// powersrc.h — güç kaynağı algılama (isteğe bağlı): adaptör mü USB mi? Yalnız config.h CFG_ADP_DETECT=1 iken
// bir şey yapar; kapalıyken hiçbir pin okunmaz ve `ma_usb` etkisizdir (Params::usbPower hep 0).
#pragma once
#include <Arduino.h>

namespace powersrc {

// LED başlatılmadan ÖNCE çağrılır: pini okur, belirsizse USB (güvenli) varsayar.
void begin();
// loop'tan sık çağrılır: 3 ardışık eş okuma ile debounce, durumu Params::usbPower'a yazar.
void poll();
bool enabled();   // algılama donanımı config'te açık mı
bool usb();       // şu an USB bütçesi mi (algılama kapalıysa hep false)

}  // namespace powersrc

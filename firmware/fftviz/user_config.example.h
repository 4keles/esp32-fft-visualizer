// user_config.example.h — kişisel ayar örneği. Kopyalayın: `cp user_config.example.h user_config.h`
// user_config.h git'e girmez (.gitignore); config.h onu otomatik okur. Yalnız değiştirmek istediklerinizi bırakın.
//
// Aşağıdaki değerler örnektir: 2 m / 120 LED WS2812B şerit, 5 V / 3,8 A güç kaynağı, adaptör algılama donanımı yok.
// LED akımı ölçülmedi: etiket hesabı ölçümle doğrulanmadığı için veri sayfası değeri (60 mA/LED) yazıldı.
#pragma once

#define CFG_LEDS     120    // 2 m @ 60 LED/m
#define CFG_LED_MA   60     // ölçüm yapılana kadar veri sayfası değeri; ölçüm formülü docs/cli.md (Power model)
#define CFG_PSU_MA   3450   // Calculated strip allocation: 3800 - 350 mA for controller/display
#define CFG_PSU_PCT  80     // min(2500, 3450 * 80 / 100) = 2500 mA model ceiling
#define CFG_MA_BUDGET 2500  // istediğiniz üst sınır (kaynak sınırının altında kalan değer geçerli olur)

// V2 has no adapter detector or GPIO35 wiring. Keep CFG_ADP_DETECT disabled.

// Optional boot autocalibration (default 1). Manual capture remains HASH held 3 s or `cal`.
// #define CFG_AUTOCAL_BOOT 0

// Default 17-key NEC table (address 0x00): 16 codes were captured from the author's handset, key 2 is not yet captured.
// For another handset use `ir learn on` to inspect normalized LSB-first address/command bytes.
// Copy decoded bytes below, rebuild, then flash only with explicit user approval.
// #define CFG_PIN_IR -1
// #define CFG_IR_ADDRESS 0x00
// #define CFG_IR_KEY_1 0x45
// #define CFG_IR_KEY_2 0x46
// #define CFG_IR_KEY_3 0x47
// #define CFG_IR_KEY_4 0x44
// #define CFG_IR_KEY_5 0x40
// #define CFG_IR_KEY_6 0x43
// #define CFG_IR_KEY_7 0x07
// #define CFG_IR_KEY_8 0x15
// #define CFG_IR_KEY_9 0x09
// #define CFG_IR_KEY_0 0x19
// #define CFG_IR_KEY_LEFT 0x08
// #define CFG_IR_KEY_RIGHT 0x5A
// #define CFG_IR_KEY_UP 0x18
// #define CFG_IR_KEY_DOWN 0x52
// #define CFG_IR_KEY_OK 0x1C
// #define CFG_IR_KEY_STAR 0x16
// #define CFG_IR_KEY_HASH 0x0D

// fv_screen.h — 240x240 şerit tabanlı renderer. Çıktı wire-order RGB565.
#pragma once
#include "fv_config.h"
#include "fv_params.h"
#include "fv_analyzer.h"

namespace fv {

struct HudInfo {
  const char* label;  // gösterilecek mod/tema adı (nullptr = yok)
  uint16_t fps10;     // ekran FPS × 10
  uint16_t bpm;
  uint8_t show;       // 0..255 HUD opaklığı (firmware zamanlamayı yönetir)
};

enum TestPattern : uint8_t { TP_COLORBARS = 0, TP_FRAME, TP_GRADIENT, TP_BOOT, TP_COUNT };

// Dither çifti: (x+y) çift → a, tek → b (wire RGB565). Renderer iç ayrıntısı.
struct ScreenDp { uint16_t a, b; };

class ScreenRenderer {
 public:
  // Kare başına bir kez: bar geometrisi ve renk LUT'larını hazırlar.
  void beginFrame(const Features& f, const Params& p, const HudInfo& hud, uint32_t tick);
  // y0..y0+nrows-1 satırlarını dst'ye yazar (240*nrows uint16, satır-ana).
  // Herhangi bir şerit bölünmesi tam kare ile bayt düzeyinde aynı sonucu vermelidir.
  void renderRows(int y0, int nrows, uint16_t* dst) const;
  // Test desenleri (beginFrame gerektirmez). TP_BOOT tick ile animasyonludur.
  void renderTest(uint8_t kind, uint32_t tick, int y0, int nrows, uint16_t* dst) const;
  // Basit metin sayfası (self-test sonuçları vb.), 5x7 font ×2 ölçek, satır yüksekliği 18 px.
  void renderText(const char* const* lines, int n, int y0, int nrows, uint16_t* dst) const;

 private:
  using Dp = ScreenDp;

  void renderRow(int y, uint16_t* row) const;
  void buildRamps(const Features& f, const Params& p);
  void buildBackground(const Features& f);
  void buildReflection(const Features& f);
  void hudRow(int y, uint16_t* row) const;

  static constexpr int kRamp = 32;  // bar rengi yükseklik kademesi sayısı (≈5.25 px/kademe)

  bool valid_ = false;
  uint32_t tick_ = 0;
  uint16_t topQ8_[NUM_BANDS];    // bar tepe yüksekliği, Q8 piksel
  uint16_t full_[NUM_BANDS];     // tam kaplı satır sayısı
  uint8_t frac_[NUM_BANDS];      // kısmi tepe satırı kesri (Q8)
  uint16_t haloS_[NUM_BANDS];    // hale ölçeği Q8
  int16_t capD_[NUM_BANDS];      // şapka alt satır derinliği (<0: yok)
  uint8_t rmax_[NUM_BANDS];      // yansımada geçerli satır sayısı
  Rgb col8_[NUM_BANDS][kRamp];      // yükseklik kademesine göre bar rengi
  Dp colP_[NUM_BANDS][kRamp];
  Dp gapP_[NUM_BANDS][kRamp];       // bar rengi %25'i + zemin (parlama aralığı)
  Rgb top8_[NUM_BANDS];          // hale rengi
  Dp capP_[NUM_BANDS];
  Dp refl_[NUM_BANDS][63];       // yansıma satırı rengi (yalnızca r < rmax_)
  Dp bgP_[SCREEN_H];
  Rgb bg8_[SCREEN_H];
  Dp lineP_;
  // HUD
  char hudLabel_[24];
  char hudRight_[20];
  uint8_t hudShow_ = 0;
  int16_t hudRightX_ = 0;
};

}  // namespace fv

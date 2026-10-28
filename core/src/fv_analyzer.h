// fv_analyzer.h — kare analizi: BFP + Hann + rfft + güç + 40 bant (log alanı) +
// tilt + gürültü tabanı + AGC + bar/led yumuşatma + cap + beat + onset + centroid + sessizlik.
#pragma once
#include "fv_config.h"
#include "fv_params.h"
#include "fv_bands.h"

namespace fv {

// Açılış otomatik kalibrasyonu (P_AUTOCAL). Durum makinesi çekirdektedir, tetik/yazdırma firmware'dedir.
enum AutoCalState : uint8_t {
  AC_OFF = 0,   // P_AUTOCAL = 0 (ya da hiç başlatılmadı)
  AC_WAIT,      // ilk denemeyi (açılıştan 3 s) ya da yeniden denemeyi (ret + 5 s) bekliyor
  AC_OBSERVE,   // 2 s'lik pencerede ölçüt topluyor (gözlem pasif: barlar/tavan etkilenmez)
  AC_OK,        // kabul edildi: gürültü tabanı elle kalibrasyonla aynı biçimde öğrenildi
  AC_GAVE_UP,   // AUTOCAL_MAX_TRIES ret sonra vazgeçildi: "kalibrasyon gerekli"
  AC_MANUAL,    // kullanıcı elle kalibre etti (buton/CLI): otomatik deneme sona erdi
};
enum AutoCalReason : uint8_t {
  ACR_NONE = 0,
  ACR_LEVEL,     // toplam bant gücü çok yüksek (ses/gürültü kaynağı var ya da ortam çok gürültülü)
  ACR_UNSTEADY,  // bant grupları pencere içinde durağan değil (müzik/konuşma/hareket)
  ACR_PEAKY,     // tek bir bant komşularından çok yüksek (kalıcı ton/uğultu)
};
struct AutoCalInfo {
  uint8_t state;     // AutoCalState
  uint8_t reason;    // son ret nedeni (AutoCalReason)
  uint8_t tries;     // tamamlanan ret sayısı
  uint8_t win;       // bu denemede ölçütleri geçen pencere sayısı (AUTOCAL_CONFIRM'e ulaşınca kabul)
  uint8_t seq;       // olay sayacı: başlama/kabul/ret/vazgeçme olduğunda artar (firmware satır yazar); reset'te sıfırlanmaz
  int16_t level10;   // son denemede görüntü bantlarının toplam gücü, dBFS×10
  int16_t bass10;    // son denemede bas kanallarının (47-150 Hz) toplam gücü, dBFS×10
  int16_t spread10;  // son denemede en kararsız grubun seviye sapması (std), dB×10
  int16_t peak10;    // son denemede bir bandın komşu ortancasından en büyük yükselişi, dB×10
};

struct Features {
  uint32_t frame;               // analiz edilen kare sayacı (ilk kare = 1)
  u16n level[NUM_BANDS];        // n: 70 dB pencerede normalize (CodePen byte/255 karşılığı)
  u16n bar[NUM_BANDS];          // ekran barları (eşik + attack/release)
  u16n cap[NUM_BANDS];          // tepe şapkaları
  uint8_t capHold[NUM_BANDS];   // kalan bekleme karesi (0 = düşüyor)
  u16n led[NUM_BANDS];          // LED yumuşatması (eşiksiz; eşik LED modunda uygulanır)
  u16n bass, mid, treble, loud; // bölge enerjileri (normalize, LED eşiği uygulanmış)
  u16n beatEnv;                 // beat zarfı: beat'te 65535, kare başına ×0.85
  uint8_t beat;                 // bu karede beat oldu mu (0/1)
  uint8_t onset;                // tiz onset (0/1)
  uint8_t silent;               // sessizlik/idle durumu (0/1)
  uint8_t centroidHue;          // spektral ağırlık merkezi → hue (0..200 aralığı, EMA)
  uint16_t bpm;                 // tempo tahmini (0 = bilinmiyor)
  int8_t bfpShift;              // blok kayan nokta kaydırması
  uint8_t beatStrength;         // 0..255, beat anındaki aşım şiddeti (beat olmayan karede 0)
  lvl ceiling;                  // AGC tavanı R (lvl, 0 = tam ölçek sinüs; dBFS = değer/85.04)
  lvl raw[NUM_BANDS];           // tilt sonrası ham bant seviyesi (lvl, aynı referans)
  lvl noise[NUM_BANDS];         // gürültü tabanı tahmini (lvl, aynı referans)
  lvl bassRaw, bassNoise;       // bas kanalları (kutu 47-150 Hz) toplamı (güç alanında): beat ve bass bölgesi; görüntü bantlarından bağımsız
  uint16_t edge[NUM_BANDS + 1]; // bant sınırları (FFT kutusu): bant b = [edge[b], edge[b+1]); fmin/fmax'a göre
  AutoCalInfo ac;               // açılış otomatik kalibrasyonu durumu (P_AUTOCAL = 0 iken hep AC_OFF)
};

class Analyzer {
 public:
  // Bant düzeni (fmin/fmax) ve tüm izleyiciler sıfırlanır. analyze() fmin/fmax değiştiğini fark edince
  // kendisi çağırır (gürültü tabanları ve kalibrasyon eski düzene ait olduğundan atılır; `cal` gerekir).
  void reset(const Params& p);
  // frame1024: en eski örnek önce, 24 kHz q15. Her çağrı bir kare üretir.
  void analyze(const q15* frame1024, const Params& p);
  const Features& features() const { return f_; }
  // Gürültü kalibrasyonu: sonraki n kare boyunca gürültü tabanını hızlı öğren (sessizlik varsayılır).
  void calibrateNoise(int frames);

  // --- Açılış otomatik kalibrasyonu (P_AUTOCAL=1): açılıştan 3 s sonra 2 s'lik GÖZLEM penceresi; gürültü durağan ve
  // düşük seviyeliyse elle kalibrasyonla aynı sonucu uygular (kabul), müzik/ses/ton varsa reddeder (tabanlar değişmez),
  // 5 s sonra yeniden dener, AUTOCAL_MAX_TRIES sonra vazgeçer. Elle kalibrasyon (calibrateNoise) her zaman önceliklidir.
  // Ölçüt eşikleri (AC_*) fv_analyzer.cpp'dedir; testler için burada açıktır (dB×10).
  static constexpr int AC_LEVEL_MAX10 = -150;    // görüntü bantları toplam gücü üst sınırı (dBFS×10); aşılırsa ACR_LEVEL
  static constexpr int AC_BASS_MAX10 = -160;     // bas kanalları toplam gücü üst sınırı (dBFS×10); aşılırsa ACR_LEVEL
  static constexpr int AC_SPREAD_MAX10 = 15;     // grup seviye sapması üst sınırı (dB×10); aşılırsa ACR_UNSTEADY
  static constexpr int AC_PEAK_MAX10 = 33;      // bant-komşu yükselişi üst sınırı (dB×10); aşılırsa ACR_PEAKY
  uint8_t autoCalState() const { return acState_; }

 private:
  void autoCalStep(const Params& p, uint32_t fr);
  void autoCalStartWindow();
  void autoCalFinish(uint32_t fr);
  void bandLevels(const q15* x, const Params& p);
  void beatOnset(const Params& p, const u16n* n, bool bassVis, int32_t thBar);
  lvl bassRaw_[BASS_BINS];      // bas kanalı ham seviyeleri (tilt dahil)
  lvl bassN_[BASS_BINS];        // bas kanalı gürültü tabanları

  // Gürültü izleyici dizileri NCH = NUM_BANDS + BASS_BINS uzunlukta: son BASS_BINS eleman tek kutulu
  // bas kanalları (görüntü bantlarından bağımsız; AGC tepesine ve kalibrasyon tabanına katılmaz,
  // yalnız beat kapısı ve bass bölge enerjisi için). fmin ≤ 47'de ilk bantlarla aynı kutulardır.
  static constexpr int NCH = NUM_BANDS + BASS_BINS;

  Features f_;
  BandMap bm_;                  // geçerli bant düzeni (fmin_/fmax_ için)
  int32_t fmin_, fmax_;         // bm_'nin üretildiği parametreler
  cq15 buf_[FFT_N / 2];         // pencerelenmiş giriş / rfft çıktısı
  int32_t ls_[NCH];             // gürültü izleyici girişi: güç alanında EMA(α=1/4) seviyesi (lvl)
  int32_t nq_[NCH];             // gürültü tabanı (Q8 lvl)
  int32_t ncal_[NCH];           // kalibrasyonla öğrenilmiş taban (lvl); tavanın alt sınırı
  int32_t ncr_[NCH];            // tavan payı (Q8 lvl, 0..~15 dB): gürültü kalıcı yükselirse tavan ncal+ncr'ye açılır
  int32_t gp_[NCH];             // gürültüsü çıkarılmış seviye G' (lvl)
  int32_t capV_[NUM_BANDS];     // şapka düşüş hızı (u16n/kare)
  u16n nPrev_[NUM_BANDS];       // önceki n (onset akısı)
  int32_t rq_;                  // AGC tavanı R (Q8 lvl)
  int32_t rqb_;                 // bas kanalının tavanı (Q8 lvl): tepe = max(bantlar, bas kanalı)
  lvl calFloor_;                // kalibrasyondan: max(Ncal) + 12 dB (AGC tabanı); yoksa LV_MIN
  int32_t eMean_, eDev_;        // beat: bas enerjisi EMA ortalama/sapma (Q8 lvl)
  int32_t ePrev_;               // önceki bas enerjisi (lvl)
  int32_t hueQ8_;               // centroid hue EMA (Q8)
  int32_t learn_;               // hızlı gürültü öğrenmesi için kalan kare
  uint16_t quiet_;              // ardışık sessiz kare
  uint16_t beatAgo_, onsetAgo_; // son olaydan beri kare
  uint8_t activeRun_;
  uint16_t uniform_;            // gürültü benzeri (spektral olarak düzgün) artışın süresi (kare); ≥ 10 s → pay kapısı açık
  int32_t lsS_[NUM_BANDS];      // bant seviyesinin yavaş ortalaması (Q8 lvl, ~4 s EMA): düzgünlük ölçütü
  uint8_t ivl_[8], ivlN_, ivlPos_;  // son beat aralıkları (kare)
  uint8_t nBass_, trebleStart_;     // görüntü bantlarının grupları: mid = [nBass_, trebleStart_), tiz = [trebleStart_, 40)
  bool calib_;
  // autocal-boot (hepsi yalnız AC_OBSERVE sırasında anlamlıdır; AC_OFF iken dokunulmaz)
  static constexpr int AC_GROUPS = 7;      // 5 grup × 8 bant + toplam + bas kanalları
  uint8_t acState_ = AC_OFF, acReason_ = ACR_NONE, acTries_ = 0, acSeq_ = 0, acWin_ = 0;
  uint16_t acN_ = 0;                        // penceredeki işlenen kare
  uint8_t acBlkN_ = 0, acNb_ = 0;           // blok içi kare sayacı, tamamlanan blok sayısı
  int32_t acBlk_[AC_GROUPS];                // grup seviyesi toplamı (geçerli blok)
  uint32_t acNext_ = 0;                     // AC_WAIT: bu kare numarasından itibaren dene
  int16_t acLevel10_ = 0, acBass10_ = 0, acSpread10_ = 0, acPeak10_ = 0;
  int32_t acNq_[NCH];                       // gölge gürültü izleyici (elle kalibrasyondaki hızlı öğrenme kuralıyla)
  int32_t acSum_[NUM_BANDS];                // bant seviyesi toplamı (lvl)
  int32_t acX0_[AC_GROUPS], acS1_[AC_GROUPS];  // grup seviyesi: ilk kare, (x−ilk) toplamı
  int32_t acS2_[AC_GROUPS];                 // (x−ilk)² toplamı (sapma kırpılı)
};

}  // namespace fv

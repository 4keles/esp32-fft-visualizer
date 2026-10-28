// fv_analyzer.cpp — kare analizi. Tüm hat log alanında (lvl = 256·log2 güç),
// karekök yok. Seviye referansı: 0 lvl = tam ölçek sinüsün bant gücü (LVL_FS), yani dBFS·85.04.
//
// Gürültü tabanı / AGC etkileşimi (iki küçük ekleme, gerekçe testlerde):
//  * Tavan N ≤ max(R − 35 dB, Ncal) yalnız R, AGC tabanının (agcmin) üstündeyken, yani sinyal
//    varken uygulanır. Sessizlikte R tabandadır; tavan uygulansaydı tabanın 35 dB altından yüksek
//    gürültü (geniş üst bantlar ≈ −62 dBFS) hiç öğrenilemez ve barlarda görünürdü.
//    İstisna (quiet-music-recovery): kalibreyken agcmin kalibre tabanın (max Ncal + 12 dB) üstündeyse ve düzgün artış kapısı kapalıysa
//    tavan, görüntü bantlarında R tabandayken de N'yi Ncal + ncr'de tutar; yoksa agcmin altındaki sürekli müzik 1 dB/s tırmanan izleyiciyle ~15 s'de silinirdi.
//    The bass channels are excluded from this exception: the beat gate (E > N + 10 dB) relies on stationary bass being absorbed by the floor;
//    a held bass floor makes a steady low-level bass tone or hum produce false beats from noise jitter (tools/audio_behavior_evidence.cpp, beat tone100).
//  * Gürültü izleyicinin girişi ham seviye değil, güç alanında α=1/4 EMA'sıdır. 1 kutulu dar
//    bantlarda ham seviye üstel dağılır (log std ≈ 5.6 dB); "hızlı düş / yavaş çık" izleyici
//    ham girişle ortalamanın ~13 dB altına oturur ve gürültü sıçramaları görünür olurdu.
//  * Bant başına alt sınır: bot = max(R − 70 dB, N + 3 dB − (R − Rb)); n = (G' − bot)/(R − bot).
//    Rb = AGC tabanı = max(agcmin, kalibrasyon tabanı). Müzikte (R ≫ Rb) bot = R − 70 dB, yani n
//    CodePen eşlemesinin aynısıdır; sessizlikte (R = Rb) ölçek gürültü tabanından
//    başlar ve gürültü dalgalanmaları bar üretmez.
//  * Tavanın kalibre tabana kilitlenmesi (cal-drift): sinyal varken (R > rLo + 1 dB) N ≤ max(R − 35, ncal)
//    tavanı N'yi ncal'e çiviler. Sessizlikte gürültü kalıcı olarak ncal'in üstüne çıkarsa (ortam, ısınma,
//    ekran) barlar yanar → R rLo'nun üstüne çıkar → tavan N'yi ncal'de tutar → barlar sönmez (kilitlenme).
//    Çözüm: tavan = max(R − 35, ncal + ncr); "pay" ncr yalnız GÜRÜLTÜ BENZERİ artışta büyür. Ölçüt =
//    spektral düzgünlük: bant başına yavaş ortalama seviyenin ncal'e göre artışı (4 s EMA) bantların ≥ 30'unda
//    medyandan ±4 dB içinde (ekran/ADC gürültüsü tüm bantlara aynı dB'yi ekler: kartta drive 3 için p25–p75
//    = 1,2 dB) ve medyan 1,5..15 dB, bu durum 10 s sürmüş. Müzik/ton/pembe gürültü eğimli ve tepeli artış
//    verir (p25–p75 ≥ 5,5 dB) → kapı kapalı, davranış eskisiyle bit-aynı. Kapı kapalıyken pay yalnız iner.
//    Pay N'nin kullandığından fazla olamaz; rLo payla yükselir (ncal+ncr'nin en yükseği + 12 dB).
//  * calibrateNoise sonunda kalibrasyon tabanı = max_b(N_b) + 12 dB: agcmin'den yüksek (gerçek
//    donanım) gürültüde dar bant sıçramalarının R'yi yukarı itip barlarda görünmesini engeller.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "fv_analyzer.h"
#include "fv_fft.h"
#include "fv_math.h"
#include "fv_tables.h"
#include "fv_bands.h"

namespace fv {

namespace {

constexpr lvl W70 = dB(70);              // görünür pencere
constexpr lvl LV_MIN = -dB(150);         // "−∞" tabanı
constexpr lvl LV_OFFSET = 512 - 5120 - LVL_FS;  // log2_l8(S) → dBFS-lvl (BFP/sh düzeltmesi hariç)
constexpr int32_t NOISE_UP = 363;        // 1 dB/s, Q8 lvl/kare
constexpr int32_t NOISE_UP_FAST = 7256;  // 20 dB/s (ilk 2 s ve kalibrasyon)
constexpr int32_t NOISE_CREEP = 182;     // 0.5 dB/s, Q8 lvl/kare: kapı açıkken tavan payının büyüme hızı
constexpr int32_t NOISE_CREEP_MAX = dB(15) * 256;  // pay üst sınırı (Q8 lvl)
constexpr int GATE_EVERY = 8;            // düzgünlük kapısı her 8 karede bir değerlendirilir
constexpr int GATE_FRAMES = 10 * FRAME_HZ;       // düzgün artış bu kadar sürerse kapı açılır
constexpr lvl GATE_TOL = dB(3);          // medyan artıştan en çok ±3 dB sapan bant "düzgün" sayılır
constexpr int GATE_MIN_BANDS = 36;       // 40 bandın 36'sı düzgün → artış gürültü benzeri (30 ve altı: değil)
constexpr int GATE_MAX_BANDS = 30;
constexpr lvl GATE_MED_MIN = 128;        // ≈ 1,5 dB: daha küçük artış zaten izleyici tarafından izlenir
constexpr int32_t AGC_REL = 2177;        // 6 dB/s, Q8 lvl/kare
constexpr int AC_BLOCK = 8;              // autocal: durağanlık ölçümü blok uzunluğu (kare)
constexpr int32_t AC_DEV_CLAMP = 2560;   // autocal: grup seviyesi sapması bu kadar (≈ 30 dB) kırpılır (kareler toplamı int32'ye sığsın)
constexpr int LEARN_FRAMES = 2 * FRAME_HZ;
constexpr int FILL_FRAMES = 3;           // ilk pencere dolana kadar (1024/400 hop)
// Sessizlik algısı ("etkin kare"): görünür bantlardan en az SILENT_MIN_BANDS tanesi bar eşiğini aşar YA DA bas kanalı
// görünür ve gürültü tabanının SILENT_BASS_DB üstündedir. Tek bant yetmez: ekran/ADC gürültüsünde bir bant
// (özellikle tek kutulu düşük bantlar) rastgele eşiği aşar; eski kural (>= 1 bant) bu yüzden `silent`'ı hiç kaldırmıyordu.
constexpr int SILENT_MIN_BANDS = 2;
constexpr lvl SILENT_BASS_DB = dB(8);
constexpr int BEAT_REFRACT = 15, ONSET_REFRACT = 4;
constexpr uint16_t ENV_DECAY = 55705;    // 0.85 (Q16)
constexpr int32_t CAP_EPS = 512;         // şapka-bar "değme" toleransı (≈1.3 px)

inline lvl lmax(lvl a, lvl b) { return a > b ? a : b; }
// Tamsayı dB → lvl (85.0403·d, yuvarlanmış); dB() ile aynı sonucu verir.
inline lvl db_int(int32_t d) { return (d * 850403 + (d >= 0 ? 5000 : -5000)) / 10000; }

// a ⊕ b = log2(2^a + 2^b) (lvl)
inline lvl logadd(lvl a, lvl b) {
  if (a < b) { lvl t = a; a = b; b = t; }
  lvl d = a - b;
  if (d >= 1016) return a;
  int i = d >> 3, fr = d & 7;
  return a + tab::logAdd[i] + (((tab::logAdd[i + 1] - tab::logAdd[i]) * fr) >> 3);
}

// Tamsayı karekök (taban).
inline uint32_t isqrt64(uint64_t v) {
  uint64_t r = 0, bit = 1ull << 62;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return (uint32_t)r;
}
// lvl → dB×10 (1 dB = 85.0403 lvl).
inline int32_t lvl_to_db10(int32_t l) { return (int32_t)(((int64_t)l * 10000) / 85040); }

// g ⊖ m = log2(2^g − 2^m); g ≤ m → LV_MIN.
inline lvl logsub(lvl g, lvl m) {
  lvl d = g - m;
  if (d <= 0) return LV_MIN;
  lvl corr;
  if (d < 1016) {
    int i = d >> 3, fr = d & 7;
    corr = tab::noiseSub[i] + (((tab::noiseSub[i + 1] - tab::noiseSub[i]) * fr) >> 3);
  } else {
    corr = -(lvl)((369u * exp2_l8(4096 - d)) >> 16);  // −2^-d/ln2 kuyruğu
  }
  return lmax(g + corr, LV_MIN);
}

// T(a, th) = sat((a − th)/(1 − th)), u16n
inline u16n thresh(u16n a, int32_t th16) {
  if (a <= th16) return 0;
  return (u16n)(((uint32_t)(a - th16) * 65535u) / (uint32_t)(65535 - th16));
}

inline int32_t smooth(int32_t y, int32_t x, int32_t atk, int32_t rel) {
  int32_t c = x > y ? atk : rel;
  y += ((x - y) * c) >> 8;  // aritmetik kaydırma = floor → düşüşte sıfıra ulaşır
  if (x == 0 && y < 256) y = 0;  // görünmez kuyruğu kes
  return y;
}

}  // namespace

void Analyzer::reset(const Params& p) {
  f_ = Features();
  fmin_ = p.get(P_FMIN);
  fmax_ = p.get(P_FMAX);
  make_band_map(fmin_, fmax_, &bm_);
  for (int i = 0; i <= NUM_BANDS; ++i) f_.edge[i] = bm_.edge[i];
  for (int b = 0; b < NCH; ++b) {
    ls_[b] = LV_MIN;
    nq_[b] = LV_MIN * 256;
    ncal_[b] = LV_MIN;
    ncr_[b] = 0;
    gp_[b] = LV_MIN;
    if (b < NUM_BANDS) lsS_[b] = LV_MIN * 256;
  }
  for (int b = 0; b < NUM_BANDS; ++b) {
    capV_[b] = 0;
    nPrev_[b] = 0;
  }
  rq_ = rqb_ = db_int(p.get(P_AGC_MIN_DB)) * 256;
  calFloor_ = LV_MIN;
  uniform_ = 0;
  acState_ = AC_OFF;            // acSeq_ sıfırlanmaz: firmware olay sayacı reset'ten bağımsız artar
  acReason_ = ACR_NONE;
  acTries_ = 0;
  acWin_ = 0;
  acN_ = 0;
  acNext_ = 0;
  acLevel10_ = acBass10_ = acSpread10_ = acPeak10_ = 0;
  eMean_ = eDev_ = 0;
  ePrev_ = LV_MIN;
  hueQ8_ = 100 * 256;
  learn_ = LEARN_FRAMES;
  calib_ = false;
  quiet_ = 0;
  beatAgo_ = onsetAgo_ = 1000;
  activeRun_ = 0;
  ivlN_ = ivlPos_ = 0;
  // Görüntü bant grupları: bas bölgesinde kalan bantlar (üst kutu ≤ 150 Hz; fmin ≥ 150 ise yok) mid'e
  // girmez; tiz = alt kutu ≥ 4 kHz (f = k·FS/FFT_N). Bas enerjisi/beat ayrı bas kanalından gelir.
  nBass_ = 0;
  trebleStart_ = NUM_BANDS;
  for (int b = 0; b < NUM_BANDS; ++b) {
    if ((int32_t)(bm_.edge[b + 1] - 1) * FS <= BASS_F_MAX * FFT_N) nBass_ = (uint8_t)(b + 1);
    if (trebleStart_ == NUM_BANDS && (int32_t)bm_.edge[b] * FS >= TREBLE_F_MIN * FFT_N) trebleStart_ = (uint8_t)b;
  }
  f_.centroidHue = 100;
}

void Analyzer::calibrateNoise(int frames) {
  learn_ = frames;
  calib_ = true;
  acState_ = AC_MANUAL;   // elle kalibrasyon otomatik denemenin yerini alır (P_AUTOCAL=0 iken sonraki karede AC_OFF'a döner)
}

// BFP + Hann + rfft + bant güçleri → f_.raw (tilt dahil).
FV_HOT void Analyzer::bandLevels(const q15* x, const Params& p) {
  // BFP: max|x|·2^s ≤ 2^15 olacak en büyük s ∈ [0,10]; pencere çarpımı >>16 ile tepe ≤ 2^14.
  int32_t mx = 0;
  for (int i = 0; i < FFT_N; ++i) {
    int32_t v = x[i] < 0 ? -x[i] : x[i];
    if (v > mx) mx = v;
  }
  int s = 0;
  while (s < 10 && (mx << (s + 1)) <= 32768) ++s;
  f_.bfpShift = (int8_t)s;
  const int32_t mul = 1 << s;
  for (int i = 0; i < FFT_N; i += 2) {
    const int32_t w0 = tab::hann1024[i <= FFT_N / 2 ? i : FFT_N - i];
    const int32_t w1 = tab::hann1024[i + 1 <= FFT_N / 2 ? i + 1 : FFT_N - i - 1];
    buf_[i >> 1].re = (int16_t)((x[i] * mul * w0 + 32768) >> 16);
    buf_[i >> 1].im = (int16_t)((x[i + 1] * mul * w1 + 32768) >> 16);
  }
  const int sh = rfft_q15(buf_, FFT_LOG2N);
  const lvl off = LV_OFFSET - 512 * s + 512 * sh;
  const int32_t tilt = p.get(P_TILT);
  for (int b = 0; b < NUM_BANDS; ++b) {
    uint64_t acc = 0;
    for (int k = bm_.edge[b]; k < bm_.edge[b + 1]; ++k) {
      int32_t re = buf_[k].re, im = buf_[k].im;
      acc += (uint32_t)(re * re) + (uint32_t)(im * im);
    }
    lvl L = acc ? log2_l8(acc) + off : LV_MIN;
    f_.raw[b] = lmax(L, LV_MIN) + bm_.tilt[b] * tilt / 15;
  }
  // Bas kanalları: görüntü bantlarından bağımsız, doğrudan 47-150 Hz kutuları (tek kutulu).
  for (int i = 0; i < BASS_BINS; ++i) {
    const int k = BASS_BIN_LO + i;
    int32_t re = buf_[k].re, im = buf_[k].im;
    const uint64_t acc = (uint32_t)(re * re) + (uint32_t)(im * im);
    const lvl L = acc ? log2_l8(acc) + off : LV_MIN;
    bassRaw_[i] = lmax(L, LV_MIN) + bm_.bassTilt[i] * tilt / 15;
  }
}

FV_HOT void Analyzer::analyze(const q15* frame1024, const Params& p) {
  if (p.get(P_FMIN) != fmin_ || p.get(P_FMAX) != fmax_) reset(p);  // yeni bant düzeni: tabanlar sıfırdan
  const uint32_t fr = f_.frame;  // bu kareden önce analiz edilen kare sayısı
  bandLevels(frame1024, p);

  const bool autoGain = p.get(P_GAIN_DB) == -128;
  // Kalibrasyon tabanı payla birlikte yükselir (ncr = 0 iken calFloor_ ile aynıdır).
  lvl calEff = calFloor_;
  {
    lvl mx = LV_MIN;
    for (int b = 0; b < NUM_BANDS; ++b) mx = lmax(mx, (lvl)((ncal_[b] * 256 + ncr_[b]) >> 8));
    calEff = lmax(calEff, vmin<lvl>(mx + dB(12), dB(3)));
  }
  const lvl rLo = lmax(db_int(p.get(P_AGC_MIN_DB)), calEff);
  lvl R = rq_ >> 8;
  const lvl Rb = rqb_ >> 8;  // bas kanalının kendi tavanı (görünmeyen bas AGC'yi sürmez ama gürültü tavanını korur)
  const bool learning = learn_ > 0;
  // Düzgünlük kapısı: kalibre ve öğrenme dışında, 8 karede bir (bant artışı yavaş ortalamaları üzerinden).
  if (calFloor_ > LV_MIN && !learning && (fr & (GATE_EVERY - 1)) == 0) {
    lvl e[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; ++b) {
      lvl x = (lvl)(lsS_[b] >> 8) - ncal_[b];
      int j = b;
      for (; j > 0 && e[j - 1] > x; --j) e[j] = e[j - 1];  // ekleme sıralaması
      e[j] = x;
    }
    const lvl med = e[NUM_BANDS / 2];
    int within = 0;
    for (int b = 0; b < NUM_BANDS; ++b) within += (e[b] >= med - GATE_TOL && e[b] <= med + GATE_TOL);
    if (within >= GATE_MIN_BANDS && med >= GATE_MED_MIN && med <= dB(15)) {
      uniform_ = (uint16_t)vmin(uniform_ + GATE_EVERY, 65535);
    } else if (within <= GATE_MAX_BANDS || med < GATE_MED_MIN || med > dB(15)) {
      uniform_ = 0;
    }
  }
  const bool gate = uniform_ >= GATE_FRAMES;
  // While agcmin lies above the calibrated floor (rLo = agcmin) and the uniform-rise gate is closed, a sustained signal is not tracked as "noise":
  // although R is clamped to agcmin (quiet distant music, R <= rLo + 1 dB), the display bands' cap keeps N at ncal + ncr. Unchanged when the
  // calibrated floor binds (rLo = calEff) or the gate is open (noise-like rise). Bass channels keep absorbing (beat gate), see the header.
  const bool held = calFloor_ > LV_MIN && rLo > calEff && !gate;
  const bool capOn = (R > rLo + dB(1) || held) && !(learning && calib_);
  const bool capOnB = Rb > rLo + dB(1) && !(learning && calib_);

  // --- gürültü tabanı ve gürültü çıkarma ---
  lvl peak = LV_MIN, peakB = LV_MIN;  // peakB: bas kanallarının tepesi (yalnız bas tavanı için)
  for (int b = 0; b < NCH; ++b) {
    const lvl r = b < NUM_BANDS ? f_.raw[b] : bassRaw_[b - NUM_BANDS];
    // EMA: 0.75·2^ls + 0.25·2^r  (log2(0.75)·256 = −106, log2(0.25)·256 = −512)
    const lvl ls = fr < (uint32_t)FILL_FRAMES ? r : logadd(ls_[b] - 106, r - 512);
    ls_[b] = ls;
    if (b < NUM_BANDS) lsS_[b] = fr < (uint32_t)FILL_FRAMES ? ls * 256 : lsS_[b] + ((ls * 256 - lsS_[b]) >> 8);
    int32_t nq = nq_[b];
    const int32_t r8 = ls * 256;
    if (fr < (uint32_t)FILL_FRAMES) nq = r8 - dB(6) * 256;  // pencere dolarken başlangıç tahmini
    else if (r8 < nq) nq += (r8 - nq) >> 3;               // düşüş: α = 1/8
    else {
      const int32_t up = learning ? NOISE_UP_FAST : NOISE_UP;
      const int32_t dd = r8 - nq;
      nq += dd < up ? dd : up;
    }
    const int32_t used = nq - ncal_[b] * 256;  // N'nin kalibre değerin üstünde fiilen kullandığı pay
    if (b < NUM_BANDS ? capOn : capOnB) {
      const int32_t capR = ((b < NUM_BANDS ? R : Rb) - dB(35)) * 256;
      const int32_t capC = ncal_[b] * 256 + ncr_[b];
      const int32_t cap = capR > capC ? capR : capC;
      const bool sat = nq >= cap;
      if (nq > cap) nq = cap;
      // Pay, N'nin kullandığından büyük olamaz (gürültü inince pay da iner).
      const int32_t u = nq - ncal_[b] * 256;
      if (ncr_[b] > u) ncr_[b] = u > 0 ? u : 0;
      // N tavana dayandı, ham seviye üstte ve artış gürültü benzeri: payı aç.
      if (gate && sat && r8 > nq && ncr_[b] < NOISE_CREEP_MAX) ncr_[b] += NOISE_CREEP;
    } else if (gate) {  // serbest izleme + gürültü benzeri artış: pay N'yi izler (rLo birlikte yükselir)
      const int32_t u = vclamp<int32_t>(used, 0, NOISE_CREEP_MAX);
      if (u > ncr_[b]) ncr_[b] = u;
    } else if (ncr_[b] > used) {
      ncr_[b] = used > 0 ? used : 0;
    }
    nq_[b] = nq;
    const lvl N = nq >> 8;
    gp_[b] = logsub(r, N + dB(3));
    if (b < NUM_BANDS) {
      f_.noise[b] = N;
      peak = lmax(peak, gp_[b]);  // bas kanalı AGC'yi sürmez (görünmeyen bas, görünen bantların ölçeğini bozmasın)
    } else {
      bassN_[b - NUM_BANDS] = N;
      peakB = lmax(peakB, gp_[b]);
    }
  }
  if (learn_ > 0 && --learn_ == 0 && calib_) {
    lvl mx = LV_MIN;
    for (int b = 0; b < NCH; ++b) {
      ncal_[b] = nq_[b] >> 8;
      ncr_[b] = 0;
      if (b < NUM_BANDS) mx = lmax(mx, ncal_[b]);
    }
    uniform_ = 0;
    calFloor_ = vmin<lvl>(mx + dB(12), dB(3));
    calib_ = false;
  }
  autoCalStep(p, fr);

  // --- AGC tavanı R ---
  if (autoGain) {
    const int32_t t8 = (peak + dB(3)) * 256;
    if (t8 > rq_) rq_ += (t8 - rq_) >> 2;
    else rq_ = lmax(rq_ - AGC_REL, t8);
    rq_ = vclamp<int32_t>(rq_, rLo * 256, dB(3) * 256);
    // Bas kanalı tavanı: aynı yasa, tepe = max(görünür bantlar, bas kanalı). fmin ≤ 150 iken eski
    // davranışla (bas bantları da tavanı sürüyordu) aynıdır.
    const int32_t tb8 = (lmax(peak, peakB) + dB(3)) * 256;
    if (tb8 > rqb_) rqb_ += (tb8 - rqb_) >> 2;
    else rqb_ = lmax(rqb_ - AGC_REL, tb8);
    rqb_ = vclamp<int32_t>(rqb_, rLo * 256, dB(3) * 256);
  } else {
    rq_ = rqb_ = db_int(p.get(P_GAIN_DB)) * 256;
  }
  R = rq_ >> 8;
  f_.ceiling = R;

  // --- normalize seviye, bar, led, cap ---
  const int32_t thBar = p.get(P_BAR_THRESH) * 65535 / 256;
  const int32_t thLed = p.get(P_LED_THRESH) * 65535 / 256;
  const int32_t bAtk = p.get(P_BAR_ATTACK), bRel = p.get(P_BAR_RELEASE);
  const int32_t lAtk = p.get(P_LED_ATTACK), lRel = p.get(P_LED_RELEASE);
  const int32_t hold = p.get(P_CAP_HOLD), grav = p.get(P_CAP_GRAVITY);
  const lvl fade = R - rLo;
  u16n n[NUM_BANDS];
  int nActive = 0;   // bar eşiğini aşan bant sayısı (sessizlik algısı)
  uint32_t sumBar = 0, sumBarB = 0;
  for (int b = 0; b < NUM_BANDS; ++b) {
    const lvl bot = lmax(R - W70, f_.noise[b] + dB(3) - fade);
    const lvl den = R - bot;
    int32_t v = 0;
    if (den > dB(1)) {
      lvl num = vclamp<lvl>(gp_[b] - bot, 0, den);
      v = (num * 65535) / den;  // num ≤ den ≤ dB(70)·… → int32'ye sığar (donanım bölmesi)
    }
    n[b] = (u16n)v;
    f_.level[b] = n[b];
    nActive += v > thBar;

    const int32_t bar = smooth(f_.bar[b], thresh(n[b], thBar), bAtk, bRel);
    f_.bar[b] = (u16n)bar;
    f_.led[b] = (u16n)smooth(f_.led[b], n[b], lAtk, lRel);

    // Şapka: bara değiyorsa (≤ CAP_EPS altında, kararlı tonda bar hedefe yukarıdan yaklaşır)
    // bar ile birlikte durur ve bekleme yeniden kurulur; aksi halde bekler, sonra ivmeyle düşer.
    int32_t cap = f_.cap[b];
    if (bar + CAP_EPS >= cap) {
      cap = bar;
      f_.capHold[b] = (uint8_t)hold;
      capV_[b] = 0;
    } else if (f_.capHold[b] > 0) {
      --f_.capHold[b];
    } else {
      capV_[b] = vmin(capV_[b] + grav, (int32_t)65535);
      cap -= capV_[b];
      if (cap <= bar) { cap = bar; capV_[b] = 0; }  // bara oturdu
    }
    f_.cap[b] = (u16n)cap;
    sumBar += (uint32_t)bar;
    sumBarB += (uint32_t)bar * (uint32_t)b;
  }

  // --- bas kanalları: görüntü bantlarıyla aynı normalizasyon (bas tavanı Rb, aynı pencere) ---
  u16n nBassCh[BASS_BINS];
  bool bassVis = false;
  {
    const lvl Rbn = rqb_ >> 8;
    const lvl fadeB = Rbn - rLo;
    for (int i = 0; i < BASS_BINS; ++i) {
      const lvl bot = lmax(Rbn - W70, bassN_[i] + dB(3) - fadeB);
      const lvl den = Rbn - bot;
      int32_t v = 0;
      if (den > dB(1)) v = (vclamp<lvl>(gp_[NUM_BANDS + i] - bot, 0, den) * 65535) / den;
      nBassCh[i] = (u16n)v;
      bassVis |= v > thBar;
    }
  }

  // --- bölge enerjileri: (max + ortalama)/2, LED eşiği sonrası; bas = bas kanalları ---
  {
    uint32_t mx = 0, sum = 0;
    for (int i = 0; i < BASS_BINS; ++i) {
      const uint32_t t = thresh(nBassCh[i], thLed);
      mx = t > mx ? t : mx;
      sum += t;
    }
    f_.bass = (u16n)((mx + sum / BASS_BINS) >> 1);
  }
  {
    const int lo[3] = {nBass_, trebleStart_, 0};
    const int hi[3] = {trebleStart_, NUM_BANDS, NUM_BANDS};
    u16n* out[3] = {&f_.mid, &f_.treble, &f_.loud};
    for (int g = 0; g < 3; ++g) {
      uint32_t mx = 0, sum = 0;
      for (int b = lo[g]; b < hi[g]; ++b) {
        uint32_t t = thresh(n[b], thLed);
        mx = t > mx ? t : mx;
        sum += t;
      }
      const int cnt = hi[g] - lo[g];
      *out[g] = cnt > 0 ? (u16n)((mx + sum / (uint32_t)cnt) >> 1) : 0;
    }
  }

  // --- centroid → hue (EMA α ≈ 0.1) ---
  if (sumBar > 0) {
    const int32_t target = (int32_t)(((uint64_t)sumBarB * 200u * 256u) / ((uint64_t)sumBar * (NUM_BANDS - 1)));
    hueQ8_ += ((target - hueQ8_) * 26) >> 8;
  }
  f_.centroidHue = (uint8_t)((hueQ8_ + 128) >> 8);

  beatOnset(p, n, bassVis, thBar);   // f_.bassRaw / f_.bassNoise (bas kanalı toplamı) burada güncellenir

  // --- sessizlik: 240 sessiz kare → silent; iki ardışık aktif kare → çıkış ---
  // Aktif = >= SILENT_MIN_BANDS görünür bant eşiği aşıyor ya da bas kanalı gürültünün SILENT_BASS_DB üstünde (yalnız bas içeren
  // müzik görünür bantları aşmayabilir; fmin = 250 Hz'de kick bantlarda yoktur).
  const bool bassAct = bassVis && f_.bassRaw > f_.bassNoise + SILENT_BASS_DB;
  const bool active = nActive >= SILENT_MIN_BANDS || bassAct;
  activeRun_ = active ? (uint8_t)vmin(activeRun_ + 1, 255) : 0;
  if (activeRun_ >= 2) {
    quiet_ = 0;
    f_.silent = 0;
  } else {
    if (quiet_ < 65535) ++quiet_;
    if (quiet_ >= SILENCE_FRAMES) f_.silent = 1;
  }

  f_.ac = AutoCalInfo{acState_, acReason_, acTries_, acWin_, acSeq_, acLevel10_, acBass10_, acSpread10_, acPeak10_};
  f_.frame = fr + 1;
}

// --- Açılış otomatik kalibrasyonu (autocal-boot) ---
// 7 grup seviyesi (lvl, güç alanı toplamı): 5 grup × 8 görüntü bandı, [5] = tüm görüntü bantları, [6] = bas kanalları
// (kutu 47-150 Hz; fmin 250 Hz iken görüntü bantlarının altında kalır, beat için ayrı taban öğrenir: bas müzik
// tabanını bozmasın diye ayrıca denetlenir). ls = güç alanında EMA seviyeleri (NCH uzunluk).
static void autoCalGroups(const int32_t* ls, lvl* out) {
  lvl all = LV_MIN;
  for (int g = 0; g < 5; ++g) {
    lvl a = LV_MIN;
    for (int b = g * 8; b < g * 8 + 8; ++b) a = logadd(a, ls[b]);
    out[g] = a;
    all = logadd(all, a);
  }
  out[5] = all;
  lvl bs = LV_MIN;
  for (int b = NUM_BANDS; b < NUM_BANDS + BASS_BINS; ++b) bs = logadd(bs, ls[b]);
  out[6] = bs;
}

// Her karede (analyze içinde, ls_ güncellendikten sonra) çağrılır. P_AUTOCAL=0 iken yalnız durumu AC_OFF'a çeker:
// başka hiçbir durum okunmaz/yazılmaz, yani golden ve diğer tüm çıktılar kapalıyken bit-aynıdır.
void Analyzer::autoCalStep(const Params& p, uint32_t fr) {
  if (p.get(P_AUTOCAL) == 0) { acState_ = AC_OFF; return; }
  if (acState_ == AC_OFF) {   // etkinleştirildi (açılış/reset ya da çalışma zamanında `autocal 1`)
    acState_ = AC_WAIT;
    acTries_ = 0;
    acWin_ = 0;
    acReason_ = ACR_NONE;
    acNext_ = fr + 1 > (uint32_t)AUTOCAL_START_FRAMES ? fr + 1 : (uint32_t)AUTOCAL_START_FRAMES;
  }
  if (acState_ == AC_WAIT && fr >= acNext_) {
    acState_ = AC_OBSERVE;
    acWin_ = 0;
    for (int b = 0; b < NCH; ++b) acNq_[b] = nq_[b];   // gölge izleyici yalnız deneme başında kurulur; onay pencerelerinde sürer
    autoCalStartWindow();
  }
  if (acState_ != AC_OBSERVE) return;
  // Gölge izleyici: elle kalibrasyonun öğrenme kuralı (hızlı yükseliş 20 dB/s, düşüş α=1/8), ama gerçek izleyiciye dokunmadan.
  for (int b = 0; b < NCH; ++b) {
    const int32_t r8 = ls_[b] * 256;
    int32_t nq = acNq_[b];
    if (r8 < nq) nq += (r8 - nq) >> 3;
    else {
      const int32_t dd = r8 - nq;
      nq += dd < NOISE_UP_FAST ? dd : NOISE_UP_FAST;
    }
    acNq_[b] = nq;
  }
  lvl gl[AC_GROUPS];
  autoCalGroups(ls_, gl);
  for (int g = 0; g < AC_GROUPS; ++g) acBlk_[g] += gl[g];
  for (int b = 0; b < NUM_BANDS; ++b) acSum_[b] += ls_[b];
  // Durağanlık 8 karelik (133 ms) blok ortalamaları üzerinden ölçülür: kart gürültüsünün hızlı geniş bant dalgalanması (ekran SPI,
  // tek kutulu dar bantlar) bloklarda ortalanır; vuruş hızındaki (≤ 4 Hz) müzik değişimleri korunur.
  if (++acBlkN_ >= AC_BLOCK) {
    for (int g = 0; g < AC_GROUPS; ++g) {
      const int32_t m = acBlk_[g] / AC_BLOCK;
      if (acNb_ == 0) acX0_[g] = m;
      const int32_t d = vclamp<int32_t>(m - acX0_[g], -AC_DEV_CLAMP, AC_DEV_CLAMP);
      acS1_[g] += d;
      acS2_[g] += d * d;
      acBlk_[g] = 0;
    }
    acBlkN_ = 0;
    ++acNb_;
  }
  if (++acN_ >= (uint16_t)AUTOCAL_WINDOW_FRAMES) autoCalFinish(fr);
}

// Yeni 2 s'lik pencere (deneme başı ya da onay penceresi): istatistikleri sıfırlar ve olay yayar.
void Analyzer::autoCalStartWindow() {
  ++acSeq_;
  acN_ = acBlkN_ = acNb_ = 0;
  for (int b = 0; b < NUM_BANDS; ++b) acSum_[b] = 0;
  for (int g = 0; g < AC_GROUPS; ++g) acS1_[g] = acS2_[g] = acBlk_[g] = 0;
}

void Analyzer::autoCalFinish(uint32_t fr) {
  const int32_t n = acN_;          // pencere kare sayısı (bant ortalamaları)
  const int32_t nb = acNb_;        // tamamlanan blok sayısı (seviye/sapma istatistiği)
  // (1) seviye: görüntü bantlarının ve bas kanallarının toplam gücünün pencere ortalaması (blok ortalamalarının ortalaması)
  acLevel10_ = (int16_t)lvl_to_db10(acX0_[5] + acS1_[5] / nb);
  acBass10_ = (int16_t)lvl_to_db10(acX0_[6] + acS1_[6] / nb);
  // (2) durağanlık: her grubun blok seviyelerinin pencere içindeki standart sapması; en kararsız grup belirler
  uint64_t maxVar = 0;
  for (int g = 0; g < AC_GROUPS; ++g) {
    const int64_t v = ((int64_t)acS2_[g] - (int64_t)acS1_[g] * acS1_[g] / nb) / nb;
    if (v > 0 && (uint64_t)v > maxVar) maxVar = (uint64_t)v;
  }
  acSpread10_ = (int16_t)lvl_to_db10((int32_t)isqrt64(maxVar));
  // (3) tepelilik: bir bandın (pencere ortalaması) ±3 komşusunun ortancasından en büyük yükselişi
  int32_t mean[NUM_BANDS];
  for (int b = 0; b < NUM_BANDS; ++b) mean[b] = acSum_[b] / n;
  int32_t maxDev = 0;
  for (int b = 0; b < NUM_BANDS; ++b) {
    int32_t v[7];
    int cnt = 0;
    for (int j = b - 3; j <= b + 3; ++j) {
      if (j < 0 || j >= NUM_BANDS) continue;
      int32_t x = mean[j];
      int k = cnt++;
      for (; k > 0 && v[k - 1] > x; --k) v[k] = v[k - 1];
      v[k] = x;
    }
    const int32_t dev = mean[b] - v[cnt / 2];
    if (dev > maxDev) maxDev = dev;
  }
  acPeak10_ = (int16_t)lvl_to_db10(maxDev);

  uint8_t reason = ACR_NONE;
  if (acLevel10_ > AC_LEVEL_MAX10 || acBass10_ > AC_BASS_MAX10) reason = ACR_LEVEL;
  else if (acSpread10_ > AC_SPREAD_MAX10) reason = ACR_UNSTEADY;
  else if (acPeak10_ > AC_PEAK_MAX10) reason = ACR_PEAKY;

  if (reason == ACR_NONE && ++acWin_ < AUTOCAL_CONFIRM) {
    autoCalStartWindow();   // pencere geçti ama onay bekleniyor: hemen bir sonraki 2 s'lik pencere
    return;
  }
  ++acSeq_;
  if (reason == ACR_NONE) {
    // Kabul: elle kalibrasyonun bitişiyle aynı durum (tabanlar = gölge izleyici).
    lvl mx = LV_MIN;
    for (int b = 0; b < NCH; ++b) {
      nq_[b] = acNq_[b];
      ncal_[b] = acNq_[b] >> 8;
      ncr_[b] = 0;
      if (b < NUM_BANDS) mx = lmax(mx, ncal_[b]);
    }
    uniform_ = 0;
    calFloor_ = vmin<lvl>(mx + dB(12), dB(3));
    acReason_ = ACR_NONE;
    acState_ = AC_OK;
    return;
  }
  acReason_ = reason;
  acWin_ = 0;
  if (++acTries_ >= AUTOCAL_MAX_TRIES) {
    acState_ = AC_GAVE_UP;
  } else {
    acState_ = AC_WAIT;
    acNext_ = fr + 1 + (uint32_t)AUTOCAL_RETRY_FRAMES;
  }
}

void Analyzer::beatOnset(const Params& p, const u16n* n, bool bassVis, int32_t thBar) {
  const uint32_t fr = f_.frame;
  // --- beat: bas kanalının (kutu 47-150 Hz) enerjisi; görüntü bantlarından (fmin) bağımsız ---
  lvl E = LV_MIN, Nb = LV_MIN;
  for (int i = 0; i < BASS_BINS; ++i) {
    E = logadd(E, bassRaw_[i]);
    Nb = logadd(Nb, bassN_[i]);
  }
  f_.bassRaw = E;
  f_.bassNoise = Nb;
  if (fr == 0) { eMean_ = E * 256; eDev_ = dB(3) * 256; ePrev_ = E; }
  const int32_t dev8 = E * 256 - eMean_;
  const int32_t thr8 = vmax((int32_t)dB(2) * 256, (int32_t)(((int64_t)p.get(P_BEAT_SENS) * eDev_) >> 8));
  // Kapı: E > N + 10 dB (taban henüz yokken, ilk pencere dolarken atlanır) ve bas kanalı
  // görünür (sessizlikte sahte beat yok).
  const bool gate = (E > Nb + dB(10) || fr < (uint32_t)FILL_FRAMES) && bassVis;
  const bool beat = gate && fr >= 1 && dev8 > thr8 && E - ePrev_ >= dB(1) && beatAgo_ >= BEAT_REFRACT;
  eMean_ += dev8 >> 6;
  eDev_ += ((dev8 < 0 ? -dev8 : dev8) - eDev_) >> 6;
  ePrev_ = E;

  if (beatAgo_ < 65535) ++beatAgo_;
  f_.beatEnv = (u16n)(((uint32_t)f_.beatEnv * ENV_DECAY) >> 16);
  f_.beat = 0;
  f_.beatStrength = 0;
  if (beat) {
    f_.beat = 1;
    f_.beatEnv = U16N_ONE;
    int32_t st = 64 + (int32_t)(((int64_t)(dev8 - thr8) * 191) / (dB(12) * 256));
    f_.beatStrength = (uint8_t)vclamp(st, (int32_t)0, (int32_t)255);
    if (beatAgo_ <= 120) {  // 30..240 BPM aralığındaki aralıklar
      ivl_[ivlPos_] = (uint8_t)beatAgo_;
      ivlPos_ = (uint8_t)((ivlPos_ + 1) & 7);
      if (ivlN_ < 8) ++ivlN_;
    }
    beatAgo_ = 0;
    // BPM: son ≤8 aralığın sıralı ortadaki yarısının ortalaması (medyan + çözünürlük)
    if (ivlN_ >= 3) {
      uint8_t s[8];
      for (int i = 0; i < ivlN_; ++i) s[i] = ivl_[i];
      for (int i = 1; i < ivlN_; ++i)
        for (int j = i; j > 0 && s[j - 1] > s[j]; --j) { uint8_t t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
      const int a = ivlN_ / 4, z = ivlN_ - ivlN_ / 4;
      uint32_t sum = 0;
      for (int i = a; i < z; ++i) sum += s[i];
      const uint32_t cnt = (uint32_t)(z - a);
      f_.bpm = (uint16_t)((60u * FRAME_HZ * cnt * 2u + sum) / (2u * sum));
    }
  } else if (beatAgo_ > 3 * FRAME_HZ) {
    ivlN_ = ivlPos_ = 0;
    f_.bpm = 0;
  }

  // --- onset: tiz bantlarda pozitif akı (n cinsinden, 70 dB = 65535) ≥ 6 dB ---
  uint32_t flux = 0;
  for (int b = trebleStart_; b < NUM_BANDS; ++b) {
    if (n[b] > nPrev_[b]) flux += (uint32_t)(n[b] - nPrev_[b]);
  }
  for (int b = 0; b < NUM_BANDS; ++b) nPrev_[b] = n[b];
  if (onsetAgo_ < 65535) ++onsetAgo_;
  f_.onset = 0;
  if (fr >= (uint32_t)FILL_FRAMES && flux * (uint32_t)W70 >= 65535u * (uint32_t)dB(6) && onsetAgo_ >= ONSET_REFRACT) {
    f_.onset = 1;
    onsetAgo_ = 0;
  }
}

}  // namespace fv

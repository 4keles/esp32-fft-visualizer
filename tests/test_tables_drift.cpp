// test_tables_drift: gen_tables çıktısı commit'teki fv_tables_gen.cpp ile bayt bayt aynı mı + tablo sağlamlığı.
#include <string>
#include "fvtest.h"
#include "fftviz.h"

using namespace fv;

static std::string slurp(FILE* f) {
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  return s;
}

TEST(tables_drift) {
  FILE* p = popen(FV_BINARY_DIR "/gen_tables", "r");
  CHECK(p != nullptr);
  if (!p) return;
  std::string gen = slurp(p);
  CHECK_EQ(pclose(p), 0);
  FILE* c = std::fopen(FV_SOURCE_DIR "/core/src/fv_tables_gen.cpp", "rb");
  CHECK(c != nullptr);
  if (!c) return;
  std::string committed = slurp(c);
  std::fclose(c);
  CHECK(gen.size() > 1000);
  if (gen != committed) std::printf("    yeniden üretin: %s/gen_tables > core/src/fv_tables_gen.cpp\n", FV_BINARY_DIR);
  CHECK(gen == committed);
}

TEST(tables_sane) {
  using namespace fv::tab;
  // Hann: w[0]=0, w[N/2]=1, monoton artan
  CHECK_EQ(hann1024[0], 0);
  CHECK_EQ(hann1024[FFT_N / 2], 32767);
  int bad = 0;
  for (int i = 1; i <= FFT_N / 2; ++i) bad += hann1024[i] < hann1024[i - 1];
  CHECK_EQ(bad, 0);
  // çeyrek sinüs
  CHECK_EQ(sin_q15[0], 0);
  CHECK_EQ(sin_q15[FFT_N / 4], 32767);
  CHECK_EQ(sin_q15[128], 23170);
  // bit-reverse involüsyon
  bad = 0;
  for (int i = 0; i < FFT_N / 2; ++i) bad += bitrev512[bitrev512[i]] != i;
  CHECK_EQ(bad, 0);
  // log2frac / exp2frac
  CHECK_EQ(log2frac[0], 0);
  CHECK_EQ(log2frac[128], 150);  // 256*log2(1.5) = 149.75
  CHECK_EQ(exp2frac[0], 0);
  CHECK_EQ(exp2frac[128], 27146);  // 65536*(√2-1) = 27146.0
  // halfband: DC kazancı 1, merkez 0.5
  long sum = halfband[12];
  for (int j = 0; j < 12; ++j) sum += 2 * halfband[j];
  CHECK_EQ(sum, 32768L);
  CHECK_EQ(halfband[12], 16384);
  // gürültü çıkarma: log2(1-2^-d) negatif, d ile artan; log toplama: [0]=256, azalan
  for (int i = 1; i < 128; ++i) CHECK(noiseSub[i] >= noiseSub[i - 1] && noiseSub[i] < 0);
  CHECK_EQ(logAdd[0], 256);
  for (int i = 1; i < 128; ++i) CHECK(logAdd[i] <= logAdd[i - 1] && logAdd[i] >= 0);
}

FV_TEST_MAIN()

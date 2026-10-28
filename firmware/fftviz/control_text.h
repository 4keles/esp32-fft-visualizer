#pragma once
#include <fftviz.h>
#include <fv_vis_tables.h>
namespace display {
struct ControlText {
  static constexpr int COLS = 19, ROWS = 13;
  uint32_t cells[ROWS][COLS] = {};
  static uint32_t next(const char*& s) {
    uint32_t c = (uint8_t)*s++;
    if (c < 128) return c;
    if (c < 224) { c = ((c & 31) << 6) | ((uint8_t)*s++ & 63); }
    else { c = ((c & 15) << 12) | (((uint8_t)*s & 63) << 6); ++s; c |= (uint8_t)*s++ & 63; }
    return c;
  }
  void layout(const char* s) {
    for (auto& row : cells) for (auto& c : row) c = 0;
    int row = 0, col = 0;
    while (*s && row < ROWS) {
      uint32_t word[COLS];
      int n = 0;
      while (*s && *s != ' ' && n < COLS) word[n++] = next(s);
      if (col && col + 1 + n > COLS) { ++row; col = 0; }
      if (row == ROWS) break;
      if (col) cells[row][col++] = ' ';
      for (int i = 0; i < n; ++i) cells[row][col++] = word[i];
      while (*s == ' ') ++s;
      if (col == COLS) { ++row; col = 0; }
    }
  }
  static uint8_t column(uint32_t c, int x) {
    static constexpr uint8_t extra[][5] = {
      {0x38,0x45,0x44,0x45,0x38}, // ü
      {0x3d,0x40,0x40,0x40,0x3d}, // Ü
      {0x48,0x54,0x54,0x74,0x20}, // ş
      {0x0c,0x52,0x52,0x72,0x24}, // Ş
      {0x18,0x24,0x64,0x24,0x08}, // ç
      {0x1e,0x21,0x61,0x21,0x12}, // Ç
      {0x00,0x44,0x7c,0x40,0x00}, // ı
      {0x08,0x08,0x08,0x08,0x08}, // en dash
      {0x14,0x18,0x7f,0x18,0x14}  // star
    };
    static constexpr uint32_t codes[] = {0xfc,0xdc,0x15f,0x15e,0xe7,0xc7,0x131,0x2013,0x2605};
    for (unsigned i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i) if (c == codes[i]) return extra[i][x];
    return fv::vtab::font5x7[c >= 32 && c <= 127 ? c - 32 : '?' - 32][x];
  }
  void render(int y0, int rows, uint16_t* out) const {
    const uint16_t ink = fv::rgb565w({255,255,255});
    for (int y = y0; y < y0 + rows; ++y) {
      uint16_t* line = out + (y - y0) * fv::SCREEN_W;
      for (int x = 0; x < fv::SCREEN_W; ++x) line[x] = 0;
      int row = y / 18, gy = (y % 18) / 2;
      if (row >= ROWS || gy >= 7) continue;
      for (int col = 0; col < COLS; ++col) if (cells[row][col]) {
        for (int x = 0; x < 5; ++x) if ((column(cells[row][col], x) >> gy) & 1) {
          int px = 6 + col * 12 + x * 2;
          line[px] = line[px + 1] = ink;
        }
      }
    }
  }
};
}

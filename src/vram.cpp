#include "vram.h"

#include "debug.h"

Vram::Vram(int width_px, int height) : width_px_(width_px), height_(height) {
  ASSERT_CMP(width_px % 2, ==, 0);
  ASSERT_CMP(width_px, >, 0);
  ASSERT_CMP(width_px, <=, kMaxLinePixelCount);
  ASSERT_CMP(height, >, 0);
  ASSERT_CMP(height, <=, kMaxLineCount);
}

void Vram::Clear(Vram::Color color) {
  ASSERT_CMP(color, <, 16);
  memset(buffer_, (color << 16) | color, sizeof(buffer_));
}

std::span<uint8_t> Vram::Line(int y) {
  if (!ASSERT_CMP(y, >=, 0) || !ASSERT_CMP(y, <, height_)) {
    return std::span(buffer_[0], width_px_ / 2);
  }
  return std::span(buffer_[y], width_px_ / 2);
}

void Vram::SetPixel(int x, int y, Vram::Color color) {
  if (!ASSERT_CMP(color, <, 16)
      || !ASSERT_CMP(x, >=, 0)
      || !ASSERT_CMP(x, <, width_px_)
      || !ASSERT_CMP(y, >=, 0)
      || !ASSERT_CMP(y, <, height_)) {
    return;
  }
  uint8_t* byte_ptr = &buffer_[y][x / 2];
  if (x % 2 == 0) {
    *byte_ptr = (*byte_ptr & 0xF0) | color;
  } else {
    *byte_ptr = (*byte_ptr & 0x0F) | (color << 4);
  }
}

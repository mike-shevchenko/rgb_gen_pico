#include "vram.h"

#include <cassert>

Vram::Vram(int width_px, int height) : width_px_(width_px), height_(height) {
  assert(width_px % 2 == 0);
  assert(width_px > 0);
  assert(width_px <= kMaxLinePixelCount);
  assert(height > 0);
  assert(height <= kMaxLineCount);
}

void Vram::Clear(Vram::Color color) {
  assert(color < 16);
  memset(buffer_, (color << 16) | color, sizeof(buffer_));
}

std::span<uint8_t> Vram::Line(int y) {
  assert(y >= 0);
  assert(y < height_);
  return std::span(buffer_[y], width_px_ / 2);
}

void Vram::SetPixel(int x, int y, Vram::Color color) {
  assert(color < 16);
  assert(x >= 0);
  assert(x < width_px_);
  assert(y >= 0);
  assert(y < height_);
  uint8_t* byte_ptr = &buffer_[y][x / 2];
  if (x % 2 == 0) {
    *byte_ptr = (*byte_ptr & 0xF0) | color;
  } else {
    *byte_ptr = (*byte_ptr & 0x0F) | (color << 4);
  }
}

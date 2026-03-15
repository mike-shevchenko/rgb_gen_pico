#include "agat7_renderer.h"

#include "agat7_font.h"
#include "debug.h"

void Agat7Renderer::InitTextBuffer() {
  for (int row = 0; row < kTextHeight; row++) {
    for (int column = 0; column < kTextWidth; column++) {
      text_buffer_[row][column] = ' ';
      text_color_[row][column] = Vram::kWhite;
    }
  }
}

void Agat7Renderer::RenderTextBuffer() {
  for (int text_y = 0; text_y < kTextHeight; text_y++) {
    for (int text_x = 0; text_x < kTextWidth; text_x++) {
      const uint8_t c = text_buffer_[text_y][text_x];
      if (!ASSERT_CMP(c, >=, 32) || !ASSERT_CMP(c, <=, 127)) {
        continue;
      }
      static_assert(sizeof(agat7_font()) == (128 - 32) * kCharHeight);

      const int base_pixel_x = kTextAreaMargin + (text_x * kCharWidth);
      const int base_pixel_y = text_y * kCharHeight;

      for (int row = 0; row < kCharHeight; row++) {
        const uint8_t char_line = agat7_font()[c - 32][row];

        for (int column = 0; column < kCharWidth; column++) {
          if ((char_line & (1 << (7 - column))) != 0) { // Test the bit (MSb first).
            const int screen_x = base_pixel_x + column;
            const int screen_y = base_pixel_y + row;
            vram_->SetPixel(screen_x, screen_y, text_color_[text_y][text_x]);
          }
        }
      }
    }
  }
}

void Agat7Renderer::PrintAt(
    int text_x, int text_y, const char* str, Vram::Color color, PrintMode print_mode) {
  if (!ASSERT_CMP(text_y, >=, 0) || !ASSERT_CMP(text_y, <, kTextHeight)
      || !ASSERT_CMP(text_x + strlen(str), <=, kTextWidth)) {
    return;
  }

  int column = text_x;
  const char* ptr = str;

  while (*ptr) {
    uint8_t c = *ptr;
    ASSERT_CMP(c, >=, 32);
    ASSERT_CMP(c, <=, 127);
    switch (print_mode) {
      case PrintMode::kAssertNoRussian: {
        ASSERT_CMP(c, <=, 95);
      } break;
      case PrintMode::kAllowRussian: {
      } break;
      case PrintMode::kToUpperCase: {
        if (c >= 96) {
          ASSERT_CMP(c, >=, 'a');
          ASSERT_CMP(c, <=, 'z');
          c = c - 'a' + 'A';
        }
      } break;
    }
    text_buffer_[text_y][column] = c;
    text_color_[text_y][column] = color;
    ++column;
    ++ptr;
  }
}

void Agat7Renderer::DrawHorzLineMgr(int x_half, int y_half, int len_half, Vram::Color color) {
  if (!ASSERT_CMP(color, <, 16)
      || !ASSERT_CMP(x_half, >=, 0)
      || !ASSERT_CMP(len_half, >, 0)
      || !ASSERT_CMP(x_half + len_half, <=, kGraphWidth / 2)
      || !ASSERT_CMP(y_half, >=, 0)
      || !ASSERT_CMP(y_half, <, kGraphHeight / 2)
    ) {
    return;
  }
  const uint8_t byte = (color << 4) | color;
  for (int byte_idx = x_half; byte_idx < x_half + len_half; ++byte_idx) {
    vram_->Line(y_half * 2)[byte_idx] = byte;  // Fill two consecutive pixels.
    vram_->Line(y_half * 2 + 1)[byte_idx] = byte;  // Fill two consecutive pixels.
  }
}

void Agat7Renderer::DrawVertLineMgr(int x_half, int y_half, int len_half, Vram::Color color) {
  if (!ASSERT_CMP(color, <, 16)
      || !ASSERT_CMP(x_half, >=, 0)
      || !ASSERT_CMP(len_half, >, 0)
      || !ASSERT_CMP(x_half, <, kGraphWidth / 2)
      || !ASSERT_CMP(y_half, >=, 0)
      || !ASSERT_CMP(y_half + len_half, <=, kGraphHeight / 2)) {
    return;
  }
  const uint8_t byte = (color << 4) | color;
  for (int y = y_half * 2; y < (y_half + len_half) * 2; ++y) {
    vram_->Line(y)[x_half] = byte;  // Fill two consecutive pixels.
  }
}


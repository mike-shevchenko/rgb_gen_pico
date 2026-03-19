#pragma once

#include <vram.h>

#include <stdint.h>

// Draws graphics and prints text in Agat-7 style into the given Vram.
class Agat7Renderer {
 public:
  static constexpr Vram::Color kColors[]{  // Colors in the order of R, G, B bits in Agat-7 VRAM.
      Vram::kBlack,
      Vram::kRed,
      Vram::kGreen,
      Vram::kYellow,
      Vram::kBlue,
      Vram::kMagenta,
      Vram::kCyan,
      Vram::kWhite,
      Vram::kBrightBlack,  // On Agat-7, shown only on B&W output.
      Vram::kBrightRed,
      Vram::kBrightGreen,
      Vram::kBrightYellow,
      Vram::kBrightBlue,
      Vram::kBrightMagenta,
      Vram::kBrightCyan,
      Vram::kBrightWhite,
  };
  static_assert(sizeof(kColors) == 16);

  static constexpr int kGraphWidth = 256;
  static constexpr int kGraphHeight = 256;
  static constexpr int kTextWidth = 32;
  static constexpr int kTextHeight = 32;
  static constexpr int kCharWidth = 7;
  static constexpr int kCharHeight =8;
  static_assert(kGraphWidth % 2 == 0);
  static_assert(kGraphHeight % 2 == 0);
  static_assert(kTextWidth * kCharWidth <= kGraphWidth);
  static_assert(kTextHeight * kCharHeight == kGraphHeight);

  Agat7Renderer(Vram& vram): vram_(&vram) {
    InitTextBuffer();
  }

  // Clear the internal text mode buffer - characters and attributes (each is a Vram::Color).
  void InitTextBuffer();

  // Agat-7 7-bit font has Russian letters at 96..127, so here is how PrintAt() handles it.
  // NOTE: The font shows '$' as a Currency Sign (U+00A4).
  enum class PrintMode{
    kAssertNoRussian,  // Assert that no chars in range 96..127 are printed; the default mode.
    kAllowRussian,  // Allow chars in range 96..127 - print them as Russian.
    kToUpperCase,  // Assert there are only 'a'..'z' in range 96..127 and print them as upper-case.
  };

  // Print the string at the given text coordinates (0..31, 0..31) into the internal text buffer.
  void PrintAt(
      int text_x, int text_y, const char* str, Vram::Color color = Vram::kGreen,
      PrintMode print_mode = PrintMode::kAssertNoRussian);

  // Render the internal text mode buffer to Vram with the proper margins - only non-black pixels
  // of the text layer are copied to Vram, black text pixels become effectively transparent.
  void RenderTextBuffer();

  // Draw a horizontal line 2 pixels thick; coords are specified in two-pixel units - MGR mode.
  void DrawHorzLineMgr(int x_half, int y_half, int len_half, Vram::Color color);

  // Draw a vertical line 2 pixels thick; coords are specified in two-pixel units - MGR mode.
  void DrawVertLineMgr(int x_half, int y_half, int len_half, Vram::Color color);

  // Set a single pixel of the HGR (256x256) mode.
  void PlotHgr(int x, int y, Vram::Color color);

 private:
  static constexpr int kTextAreaWidthPx = kTextWidth * kCharWidth;
  static_assert(kTextAreaWidthPx == 224);
  static constexpr int kTextAreaMargin = (kGraphWidth - kTextAreaWidthPx) / 2;  // To center.

  uint8_t text_buffer_[kTextHeight][kTextWidth];
  Vram::Color text_color_[kTextHeight][kTextWidth];

  Vram* vram_;
};

#pragma once

#include <array>
#include <span>
#include <stdint.h>
#include <string.h>

// Frame buffer representing a visible image of a retro computer.
//
// Each pixel takes 4 bits - a byte in the buffer represents two pixels, so 16 colors per pixel.
//
// The maximum size which fits all known retro computers of interest is defined via constants.
// The maximum-sized buffer is allocated in order to avoid OOM surprises.
class Vram {
 public:
  // TODO: Change the colors to ZX Spectrum; adjust the palette.
  enum Color: uint8_t{
      kBlack = 0,
      kRed = 0x04,
      kGreen = 0x02,
      kBlue = 0x01,
      kMagenta = kRed | kBlue,
      kYellow = kRed | kGreen,
      kCyan = kBlue | kGreen,
      kWhite = kRed | kGreen | kBlue,
      kBright = 8,  // To be added to other colors.
      kBrightBlack = kBlack | kBright,  // Shown as black by some retro computers.
      kBrightRed = kRed | kBright,
      kBrightGreen = kGreen | kBright,
      kBrightBlue = kBlue | kBright,
      kBrightMagenta = kMagenta | kBright,
      kBrightYellow = kYellow | kBright,
      kBrightCyan = kCyan | kBright,
      kBrightWhite = kWhite | kBright,
  };

  Vram(int width_px, int height);

  int line_size_bytes() const { return width_px_ / 2; };
  int width_px() const { return width_px_; };
  int height() const { return height_; }

  void Clear(Color color = kBlack);
  std::span<uint8_t> LineBytes(int y);
  void SetPixel(int x, int y, Color color);

 private:
  const int width_px_;
  const int height_;

  static constexpr int kMaxLineCount = 320;  // Pentagon-128 holds the record.
  static constexpr int kMaxLinePixelCount = 640;  // ATM Turbo holds the record.
  static_assert(kMaxLinePixelCount % 2 == 0);

  // 4 bits per pixel.
  uint8_t buffer_[kMaxLinePixelCount / 2][kMaxLineCount];
};

#include "agat7_picture.h"

#include <stdio.h>

#include "config.h"  // For printing the configuration values on the test image.
#include "debug.h"

using enum Vram::Color;

Agat7Picture::Agat7Picture(Agat7Renderer& renderer): r_{&renderer} {
}

namespace {

constexpr Vram::Color kRgbLineColors[]{
    kRed,
    kGreen,
    kBlue,
};

Vram::Color RgbLineColor(int index) {
  return kRgbLineColors[index % std::size(kRgbLineColors)];
}

}  // namespace

void Agat7Picture::DrawGrid() {
  // Draw a 2-pixel-wide frame around the screen, except over the `** AGAT **` text.
  constexpr int stroke_width_half = 8;
  for (int stroke = 0; stroke < 16; ++stroke) {
    if (stroke < 4 || stroke > 9) {  // Make room around `** AGAT **` at the top.
      r_->DrawHorzLineMgr(
          stroke_width_half * stroke, 0, stroke_width_half, RgbLineColor(stroke));
    }
    r_->DrawHorzLineMgr(
        stroke_width_half * stroke, 127, stroke_width_half, RgbLineColor(stroke));
  }
  constexpr int stroke_height_half = 8;
  for (int stroke = 0; stroke < 16; ++stroke) {
    if (stroke == 1) {  // Make room for the horizontal pixel ruler.
      continue;
    }
    r_->DrawVertLineMgr(
        0, stroke * stroke_height_half, stroke_height_half, RgbLineColor(stroke));
    r_->DrawVertLineMgr(
        127, stroke * stroke_height_half, stroke_height_half, RgbLineColor(stroke));
  }

  for (int x = 0; x < 256; x += 2) {  // Draw a horizontal pixel ruler to see the pixel clock.
    r_->PlotHgr(x, 16, kWhite);
  }
}

void Agat7Picture::DrawColorBars() {
  for (int bar = 0; bar < 16; ++bar) {
    for (int x_half = 0; x_half < 7; ++x_half) {  // Each bar will be 14 pixels wide.
      r_->DrawVertLineMgr(8 + bar * 7 + x_half, 44, 8, Agat7Renderer::kColors[bar]);
    }
  }
}

void Agat7Picture::PrintText(const VideoMode& video_mode) {
  using enum Agat7Renderer::PrintMode;

  // NOTE: The top AGAT label and the lines marked as "Can be typed in Monitor" allow to physically
  // match this test picture with the image from the original machine.

  int y = -1;
  r_->PrintAt(8, ++y, "**      **");  // This label is also not centered on the real Agat-7.
  r_->PrintAt(11, y, "agat", kRed, kAllowRussian);
  r_->PrintAt(0, ++y, "*1234567890123456789012345678901", kWhite); // Can be typed in Monitor.
  ++y;
  r_->PrintAt(0, ++y, "256*256; 32*32: 7*8=>224*256");
  ++y;
  // Print the complete font - 96 chars (32 x 3).
  for (int line = 0; line < 3; ++line) {
    ++y;
    for (int x = 0; x < 32; ++x) {
      const char s[] = {(char) (32 + line * 32 + x), '\0'};
      r_->PrintAt(x, y, s, kGreen, kAllowRussian);
    }
  }
  ++y;
  r_->PrintAt(0, ++y, "/----NORMAL----\\/----BRIGHT----\\");
  r_->PrintAt(0, ++y, "0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7");
  y += 2;  // Reserve the space for the color bars.
  r_->PrintAt(0, ++y, "K R G Y B M C W K R G Y B M C W");  // Color names.
  ++y;

  char params[r_->kTextWidth + 1];
  snprintf(params, sizeof(params), "SYNC=%s, BOARD=%s",
      Config::to_string(Config::kSync).c_str(), Config::to_string(Config::kBoard).c_str());
  r_->PrintAt(0, ++y, params, kCyan, kToUpperCase);
  ASSERT_CMP(video_mode.sys_freq, <, 1'000'000, "Has to fit in 3 digits");
  snprintf(params, sizeof(params), "VS %d+%d+%d+%d=%d, CPU %.0f MHZ",
      video_mode.v_front_porch, video_mode.v_sync_pulse, video_mode.v_back_porch,
      video_mode.v_visible_area, video_mode.whole_frame,
      video_mode.sys_freq / 1000.0);
  r_->PrintAt(0, ++y, params, kYellow);
  ASSERT_CMP(video_mode.pixel_freq, <, 100'000'000, "Int part has to fit in 2 digits");
  snprintf(params, sizeof(params), "HS %d+%d+%d+%d=%d, PX %.2f MHZ",
      video_mode.h_front_porch, video_mode.h_sync_pulse, video_mode.h_back_porch,
      video_mode.h_visible_area, video_mode.whole_line,
      video_mode.pixel_freq / 1'000'000.0);
  r_->PrintAt(0, ++y, params, kWhite);

  ++y;
  r_->PrintAt(0, ++y, "HTTPS://GITHUB.COM/");
  r_->PrintAt(0, ++y, "  MIKE-SHEVCHENKO/RGB_GEN_PICO");
  ++y;
  r_->PrintAt(0, ++y, "-> ZX-RGBI-TO-VGA-HDMI-PICOSDK");
  r_->PrintAt(0, ++y, "  BY OLEKSANDR SEMENYUK");
  ++y;
  r_->PrintAt(0, ++y, "-> ZX-RGBI2VGA-HDMI");
  r_->PrintAt(0, ++y, "  BY ALEX-EKB");
  ++y;
  r_->PrintAt(0, ++y, "INSPIRED BY HTTP://MURMULATOR.RU");

  r_->PrintAt(0, 30, "00000000000000000000000000000000", kWhite);  // Can be typed in Monitor.

  r_->RenderTextBuffer();
}

void Agat7Picture::DrawPicture(const VideoMode& video_mode) {
  PrintText(video_mode);
  DrawGrid();
  DrawColorBars();
}

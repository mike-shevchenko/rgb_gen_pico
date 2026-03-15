#pragma once

#include "agat7_renderer.h"

#include "video_mode.h"  // To print video mode parameters on the picture.

// Draws a static test picture imitating the Agat-7 video mode.
class Agat7Picture {
 public:
  Agat7Picture(Agat7Renderer& renderer);
  void DrawPicture(const VideoMode& video_mode);

 private:
  enum class MiddleStrokes { kPresent, kAbsent };

  void DrawHorzRgbLine(int y_half, MiddleStrokes draw_middle_strokes = MiddleStrokes::kPresent);
  void DrawVertRgbLine(int half_x);
  void DrawGrid();
  void DrawColorBars();
  void PrintText(const VideoMode& video_mode);

  Agat7Renderer* r_;
};

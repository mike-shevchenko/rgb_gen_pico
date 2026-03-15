#pragma once

#include <stdint.h>

struct VideoMode {
  uint32_t sys_freq;  // CPU core clock in kHz.
  float pixel_freq;  // Pixel clock in Hz.
  uint16_t h_visible_area;  // Number of visible pixels per line.
  uint16_t v_visible_area;  // Number of visible lines per frame.
  uint16_t whole_line;  // Total pixels per line (v_visible_area + v_porches + v_sync).
  uint16_t whole_frame;  // Total lines per frame.
  uint8_t h_front_porch;  // Horizontal front porch, in pixels.
  uint8_t h_sync_pulse;  // Horizontal sync pulse width, in pixels.
  uint8_t h_back_porch;  // Horizontal back porch, in pixels.
  uint8_t v_front_porch;  // Vertical front porch, in TV lines (64 us each).
  uint8_t v_sync_pulse;  // Vertical sync pulse, in TV lines (64 us each).
  uint8_t v_back_porch;  // Vertical back porch, in TV lines (64 us each).
  uint8_t sync_polarity;  // Bit mask having 1 in positions to be inverted for a negative sync.
  uint8_t div;  // Coefficient of scan-doubling/tripling/etc.
};

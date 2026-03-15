#pragma once

#include <string>

class Config {
 public:
  //-----------------------------------------------------------------------------------------------
  // Choose the hardware version: -DBOARD=rgb2vga or -DBOARD=murmulator.
  #if !defined(BOARD)
    #define BOARD rgb2vga
  #endif
  enum class Board { rgb2vga, murmulator };
  static constexpr auto kBoard = Board::BOARD;

  static std::string to_string(const Board value) {
    return
      (value == Board::rgb2vga) ? "rgb2vga" :
      (value == Board::murmulator) ? "murmulator" :
      (/*compile-time error*/abort() /*operator comma*/, "Unexpected BOARD");
  }

  //-----------------------------------------------------------------------------------------------
  // Choose the video mode: -DMODE=agat7 or -DMODE=vga.
  #if !defined(MODE)
    #define MODE agat7
  #endif
  enum class Mode { agat7, vga };
  static constexpr auto kMode = Mode::MODE;

  //-----------------------------------------------------------------------------------------------
  // Choose the sync polarity: -DSYNC=pos or -DSYNC=neg.
  #if !defined(SYNC)
    #define SYNC neg
  #endif
  enum class Sync { neg, pos };
  static constexpr auto kSync = Sync::SYNC;

  static std::string to_string(Sync value) {
    return
      (value == Sync::neg) ? "neg" :
      (value == Sync::pos) ? "pos" :
      (/*compile-time error*/abort() /*operator comma*/, "Unexpected SYNC");
  }
};

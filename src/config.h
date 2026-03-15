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
  // Choose the LED type: -DLED=pico (RPi Pico LED on GPIO25) or -DLED=rgb (typical RP2040-Zero
  // with RGB LED on GPIO16) or -DLED=grb (some RP2040-Zero with GRB LED on GPIO16).
  #if !defined(LED)
    #define LED rgb
  #endif
  enum class Led { pico, rgb, grb };
  static constexpr auto kLed = Led::LED;

  static std::string to_string(const Led value) {
    return
      (value == Led::pico) ? "pico" :
      (value == Led::rgb) ? "rgb" :
      (value == Led::grb) ? "grb" :
      (/*compile-time error*/abort() /*operator comma*/, "Unexpected LED");
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
  //-----------------------------------------------------------------------------------------------
  // Choose whether an assertion failure must panic (flash the LED and hang): -DFAILURE=log or
  // -DFAILURE=panic.

  #if !defined(FAILURE)
    #define FAILURE log
  #endif
  enum class Failure { log, panic };
  static constexpr auto kFailure = Failure::FAILURE;

  static std::string to_string(Failure value) {
    return
      (value == Failure::log) ? "log" :
      (value == Failure::panic) ? "panic" :
      (/*compile-time error*/abort() /*operator comma*/, "Unexpected FAILURE");
  }
};

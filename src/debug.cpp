#include "debug.h"

#include "config.h"

#include <optional>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <hardware/structs/sio.h>

namespace debug {
namespace detail {

static constexpr int kDurationUnitMs = 75;  // 25 words per minute - a tribute to Nokia SMS.

// Panic functions are RAM-resident to ensure they work if Flash/XIP is the cause of the panic.

enum class Rp2040ZeroLedType{ kRgb, kGrb };

std::optional<Rp2040ZeroLedType> Rp2040ZeroLedTypeFromConfig() {
  switch (Config::kLed) {
    case Config::Led::pico: return std::nullopt;
    case Config::Led::rgb: return Rp2040ZeroLedType::kRgb;
    case Config::Led::grb: return Rp2040ZeroLedType::kGrb;
  }
  return std::nullopt;  // Gives a warning.
}

// NOTE: Not using PIO because needs to work on panic.
void __no_inline_not_in_flash_func(SetRp2040ZeroLed)(
    uint8_t r, uint8_t g, uint8_t b, Rp2040ZeroLedType led_type) {
  // Initialization - done on every call to avoid dependencies.
  static constexpr uint32_t kWs2812Gpio = 16;
  gpio_init(kWs2812Gpio);
  gpio_set_dir(kWs2812Gpio, GPIO_OUT);
  gpio_put(kWs2812Gpio, 0);
  const uint32_t freq_mhz = clock_get_hz(clk_sys) / 1000000;
  const uint32_t c0h = (350 * freq_mhz) / 1000;
  const uint32_t c0l = (800 * freq_mhz) / 1000;
  const uint32_t c1h = (700 * freq_mhz) / 1000;
  const uint32_t c1l = (600 * freq_mhz) / 1000;
  busy_wait_us(300);  // Reset latch.

  uint32_t data = 0;
  switch (led_type) {
    case Rp2040ZeroLedType::kRgb: {
      data = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8);
    } break;
    case Rp2040ZeroLedType::kGrb: {
      data = ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
    } break;
  }

  const uint32_t interrupts = save_and_disable_interrupts();
  for (int i = 0; i < 24; i++) {
    bool bit = data & 0x80000000;
    data <<= 1;
    sio_hw->gpio_set = (1u << kWs2812Gpio);  // Set the GPIO high.
    busy_wait_at_least_cycles(bit ? c1h : c0h);
    sio_hw->gpio_clr = (1u << kWs2812Gpio);  // Reset the GPIO low.
    busy_wait_at_least_cycles(bit ? c1l : c0l);
  }
  restore_interrupts(interrupts);

  busy_wait_us(300);  // Mandatory reset period > 50us (300us is safer for all clones)
}

void __no_inline_not_in_flash_func(SetRpiPicoLed)(bool is_on) {
  constexpr uint32_t kLedGpio = 25;
  gpio_init(kLedGpio);
  gpio_set_dir(kLedGpio, GPIO_OUT);
  gpio_put(kLedGpio, is_on);
}

void __force_inline SetPanicLed(bool is_on) {
  if (const std::optional<Rp2040ZeroLedType> led_type = Rp2040ZeroLedTypeFromConfig()) {
    if (is_on) {
      SetRp2040ZeroLed(16, 0, 16, *led_type);  // Magenta non-bright.
    } else {
      SetRp2040ZeroLed(0, 0, 0, *led_type);  // Off.
    }
  } else {
    SetRpiPicoLed(is_on);
  }
}

[[noreturn]] void __no_inline_not_in_flash_func(FlashBuiltInLedOnPanic)() {
  // Durations, in kDurationUnitMs units.
  static constexpr int kDot = 1;
  static constexpr int kDash = 3;
  static constexpr int kInterElement = 1;
  static constexpr int kInterLetter = 3;
  static constexpr int kInterWord = 7;

  auto flash_and_wait = [](
      int flash_duration_units, int wait_duration_units) {
    SetPanicLed(true);
    busy_wait_ms(flash_duration_units * kDurationUnitMs);
    SetPanicLed(false);
    busy_wait_us(wait_duration_units * kDurationUnitMs * 1000);
  };

  for (;;) {
    // SOS Morse: ... --- ...
    flash_and_wait(kDot, kInterElement);
    flash_and_wait(kDot, kInterElement);
    flash_and_wait(kDot, kInterLetter);
    flash_and_wait(kDash, kInterElement);
    flash_and_wait(kDash, kInterElement);
    flash_and_wait(kDash, kInterLetter);
    flash_and_wait(kDot, kInterElement);
    flash_and_wait(kDot, kInterElement);
    flash_and_wait(kDot, kInterWord);
  }
}

std::string AssertionMessage(const char* fmt, va_list args) {
  if (!fmt) {
    return {};
  }
  return "\n  Message: " + nx::kit::utils::vformat(fmt, args);
}

void __no_inline_not_in_flash_func(PrintBriefAssertionFailureMessage)(
    const char* const file_basename, int line) {
  if (!file_basename || file_basename[0] == '\0') {
    puts("\nEmpty __FILE__");  // Not much we can do here.
    return;
  }
  printf("\nASSERT %s:%d\n", file_basename, line);
}

void HandleAssertFailed(const char* complete_message) {
  switch (Config::kFailure) {
    case Config::Failure::log: {
      printf("*** ERROR *** (non-fatal)\n%s\n", complete_message);
      stdio_flush();
    } break;
    case Config::Failure::panic: {
      panic("%s", complete_message);  // NOTE: panic() works unreliably with a complex fmt string.
    } break;
  }
}

bool __no_inline_not_in_flash_func(AssertFailed)(
    const char* condition_str, const char* file, int line, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  HandleAssertFailed(nx::kit::utils::format(
      "Assertion failed at %s:%d: %s%s\n", file, line, condition_str,
      AssertionMessage(fmt, args).c_str()).c_str());
  va_end(args);
  return false;
}

bool __no_inline_not_in_flash_func(AssertCmpFailed)(
    const char* lhs_str, const char* op_str, const char* rhs_str,
    const char* lhs_val, const char* rhs_val, const char* file, int line, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  HandleAssertFailed(nx::kit::utils::format(
      "Assertion failed at %s:%d: %s %s %s\n  Actual values: %s %s %s%s\n",
      file, line, lhs_str, op_str, rhs_str, lhs_val, op_str, rhs_val,
      AssertionMessage(fmt, args).c_str()).c_str());
  va_end(args);
  return false;
}

} using namespace detail;

void SetBuiltInLed(bool is_on) {
  if (const std::optional<Rp2040ZeroLedType> led_type = Rp2040ZeroLedTypeFromConfig()) {
    if (is_on) {
      SetRp2040ZeroLed(0, 16, 0, *led_type);  // Green non-bright.
    } else {
      SetRp2040ZeroLed(0, 0, 0, *led_type);  // Off.
    }
  } else {
    SetRpiPicoLed(is_on);
  }
}

} using namespace debug;

extern "C" {

[[noreturn]] void CustomPanic(const char *fmt, ...) {
  // Print an almost standard panic message.
  printf("\n*** PANIC *** (custom handler)\n");
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }
  printf("\n");
  stdio_flush();

  FlashBuiltInLedOnPanic();
}

}  // extern "C"

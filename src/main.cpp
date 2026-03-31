#include <array>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <pico.h>
#include <pico/time.h>
#include <pico/multicore.h>
#include <pico/stdio_usb.h>
#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/structs/pll.h>
#include <hardware/structs/systick.h>

#include "../build/programs.pio.h"

#include "agat7_picture.h"
#include "agat7_renderer.h"
#include "config.h"
#include "debug.h"
#include "video_mode.h"
#include "vram.h"

// TODO:
// - Fix coding style:
//   - Use Unix newlines.
//   - Rename all functions in PascalCase.
//   - Rename all enum class items with the `k` prefix.
// - Draw ASCII diagrams for all modes.
// - Add comments explaining the TV scan principles.
// - Add a PAL/SECAM Field 1 standard mode: 52 us, 313 lines, 288 visible lines.
// - Refactor to use a complete time frame buffer starting with v-front-porch, lines starting with
//   h-front-porch.
//   - In VGA mode, draw the entire buffer in the middle, visualizing the porches/pulses amd
//     printing all the parameters.
//   - For drawing, supply a line-start-array, each ptr shifted after the h-back-porch.

//-------------------------------------------------------------------------------------------------

constexpr int kRedGpioShift = 0;
constexpr int kGreenGpioShift = 2;
constexpr int kBlueGpioShift = 4;
constexpr uint8_t kNoSyncGpioByte = 0b00000000;
constexpr uint8_t kVSyncGpioByte = 0b10000000;
constexpr uint8_t kHSyncGpioByte = 0b01000000;
constexpr uint8_t kVHSyncGpioByte = 0b11000000;
constexpr uint8_t kSyncPolatiryMaskPositive = kNoSyncGpioByte;  // Invert no bits.
constexpr uint8_t kSyncPolatiryMaskNegative = kVHSyncGpioByte;  // Invert both H and V sync bits.

constexpr VideoMode kVideoModeVga640x480x60{
    .sys_freq = 252'000,
    .pixel_freq = 25'200'000.0,  // The VGA standard is 25.175, but we want to avoid jitter.
    .h_visible_area = 640,
    .v_visible_area = 480,
    .whole_line = 800,
    .whole_frame = 525,
    .h_front_porch = 16,
    .h_sync_pulse = 96,
    .h_back_porch = 48,
    .v_front_porch = 10,
    .v_sync_pulse = 2,
    .v_back_porch = 33,
    .sync_polarity = kSyncPolatiryMaskNegative,
    .h_scale = 2,
    .v_scale = 2,
};

// PAL/SECAM standard:
// Frame freq: 50 Hz (20 ms). Pulse width: 8 * 64 us = 512 us.
// Line freq: 15625 Hz. Pulse width: ~= 4.7 us.
// Hsync pulse starts synchronously with the vsync pulse.
//
// Agat-7:
// Frame freq: 50.08 Hz (19.97 ms). Pulse width: 4 * 64 us = 256 us.
// Line freq: 15625 Hz. Pulse width: 16 * (1 / pixel_clock) ~= 3.047 us.
// hsync pulse starts 3 us (16 pixels) after the vsync pulse.
//
// TODO: The current code makes the hsync pulse starts 11 us before the vsync pulse. The buffer
//     layouts must be changed to allow adjusting this value, because currently the code can
//     start the vsync pulse no earlier than the first visible pixel of a line.
constexpr VideoMode kVideoModeAgat7{
    .sys_freq = 252'000,
    .pixel_freq = 5'250'000.0,
    .h_visible_area = 256,
    .v_visible_area = 256,
    .whole_line = 336,
    .whole_frame = 312,
    .h_front_porch = 20,
    .h_sync_pulse = 16,
    .h_back_porch = 44,
    .v_front_porch = 28,
    .v_sync_pulse = 4,
    .v_back_porch = 24,
    .sync_polarity =
        (Config::kSync == Config::Sync::neg) ? kSyncPolatiryMaskNegative :
        (Config::kSync == Config::Sync::pos) ? kSyncPolatiryMaskPositive :
        printf/*compile-time error*/("Unexpected SYNC\n"),
    .h_scale = 1,
    .v_scale = 1,
};
static_assert(
    kVideoModeAgat7.h_front_porch
    + kVideoModeAgat7.h_sync_pulse
    + kVideoModeAgat7.h_back_porch
    + kVideoModeAgat7.h_visible_area
    == kVideoModeAgat7.whole_line);
static_assert(
    kVideoModeAgat7.v_front_porch
    + kVideoModeAgat7.v_sync_pulse
    + kVideoModeAgat7.v_back_porch
    + kVideoModeAgat7.v_visible_area
    == kVideoModeAgat7.whole_frame);

constexpr VideoMode kVideoModePentagon128{
    .sys_freq = 252'000,
    .pixel_freq = 7'000'000.0,
    .h_visible_area = /* left border */ 72 + 256 + /* right border */ 56,
    .v_visible_area =
        /* invisible top border */ 16 + /* top border */ 48 + 192 + /* bottom border */ 48,
    .whole_line = /* hsync */ 64 + 72 + 256 + 56,
    .whole_frame = /* vsync */ 16 + 16 + 48 + 192 + 48 /* = 320 */,
    .h_front_porch = 16,  // TBD
    .h_sync_pulse = 16,  // TBD
    .h_back_porch = 32,  // TBD
    .v_front_porch = 6,  // TBD
    .v_sync_pulse = 4,  // TBD
    .v_back_porch = 6,  // TBD
    .sync_polarity =
        (Config::kSync == Config::Sync::neg) ? kSyncPolatiryMaskNegative :
        (Config::kSync == Config::Sync::pos) ? kSyncPolatiryMaskPositive :
        printf/*compile-time error*/("Unexpected SYNC\n"),
    .h_scale = 1,
    .v_scale = 1,
};
static_assert(
    kVideoModePentagon128.h_front_porch
    + kVideoModePentagon128.h_sync_pulse
    + kVideoModePentagon128.h_back_porch
    + kVideoModePentagon128.h_visible_area
    == kVideoModePentagon128.whole_line);
static_assert(
    kVideoModePentagon128.v_front_porch
    + kVideoModePentagon128.v_sync_pulse
    + kVideoModePentagon128.v_back_porch
    + kVideoModePentagon128.v_visible_area
    == kVideoModePentagon128.whole_frame);
static Vram vram(/*width_px=*/256, /*height=*/256);
static Agat7Renderer agat7_renderer(vram);

//-------------------------------------------------------------------------------------------------
// Video output

// For each two Vram pixels (1 byte), produces two bytes, each mapped to the video output GPIOs.
class Palette {
 public:
  void Init(const VideoMode& video_mode) {
    using enum Vram::Color;
    const uint8_t sync_gpio_byte = kNoSyncGpioByte ^ video_mode.sync_polarity;
    // TODO: Investigate the proper way to support the "bright black" option.
    auto to_gpio_byte = [&video_mode, sync_gpio_byte](Vram::Color color) -> uint8_t {
      const uint8_t value = 0b10 | ((color & kBright) != 0);
      const uint8_t r = (color & kRed) ? value : 0;
      const uint8_t g = (color & kGreen) ? value : 0;
      const uint8_t b = (color & kBlue) ? value : 0;
      return sync_gpio_byte |
          (r << kRedGpioShift) | (g << kGreenGpioShift) | (b << kBlueGpioShift);
    };
    for (int hi = 0; hi < kColorCount; ++hi) {
      const uint8_t hi_gpio_byte = to_gpio_byte(static_cast<Vram::Color>(hi));
      for (int lo = 0; lo < kColorCount; ++lo) {
        map_[hi * kColorCount + lo] =
            (hi_gpio_byte << 8) | to_gpio_byte(static_cast<Vram::Color>(lo));
      }
    }
  }

  uint16_t operator[](uint8_t byte) const { return map_[byte]; }

 private:
  std::array<uint16_t, 256> map_;
};

static Palette palette;

static VideoMode video_mode;

// Position of Vram insode the VGA visible rectangle.
struct VgaParams {
  int16_t h_visible_area;  // In Vram pixels; effectively is the Vram line length in pixels.
  int16_t h_margin;  // In Vram pixels; left and right margins are the same.
  int16_t v_visible_area;  // In VGA lines.
  int16_t v_margin;  // In VGA lines; top and bottom margins are the same.
};

static VgaParams vga_params;

// 4 buffers, each represents a full video-out line (with porches), including invisible lines.
constexpr int kDmaBufBlank = 0;
constexpr int kDmaBufVsync = 1;
constexpr int kDmaBufImageA = 2;
constexpr int kDmaBufImageB = 3;
static uint32_t* dma_bufs[4];

static uint32_t* agat7_dma_bufs[256];  // One buffer per line.

static int dma_ch0;
static int dma_ch1;

void __not_in_flash_func(convert_vram_line_to_vga_dma_buf)(
    const VgaParams& vga_params, uint32_t* dma_buf, const Vram& vram, int vga_y) {
  uint16_t* dma_buf_word_ptr = (uint16_t*)dma_buf;

  // Left margin.
  for (int i = vga_params.h_margin / /* two pixels */ 2; i != 0; --i) {
    *dma_buf_word_ptr++ = palette[0];
  }

  ASSERT_CMP(vga_y, >=, vga_params.v_margin);
  const auto vram_line_bytes = vram.LineBytes((vga_y - vga_params.v_margin) / video_mode.v_scale);

// TODO: Investigate whether the new safe C++-style algorithm lowers performance.
#if 1  // Optimized unsafe C-style algorithm.
  const uint8_t* vram_byte_ptr = &vram_line_bytes[0];
  int vram_x = 0;
  while (vram_x + 8 <= vga_params.h_visible_area) {
    *dma_buf_word_ptr++ = palette[*(vram_byte_ptr + 0)];
    *dma_buf_word_ptr++ = palette[*(vram_byte_ptr + 1)];
    *dma_buf_word_ptr++ = palette[*(vram_byte_ptr + 2)];
    *dma_buf_word_ptr++ = palette[*(vram_byte_ptr + 3)];
    vram_byte_ptr += 4;
    vram_x += 8;
  }
  while (vram_x < vga_params.h_visible_area) {
    *dma_buf_word_ptr++ = palette[*vram_byte_ptr++];
    vram_x += 4;
  }
#elif 0  // Safe naive algorithm using Span::operator[] - requires disabling the assertion in Span.
  for (int vram_byte_idx = 0; vram_byte_idx < vga_params.h_visible_area / 2; ++vram_byte_idx) {
    *dma_buf_word_ptr++ = palette[vram_line_bytes[vram_byte_idx]];
  }
#else  // Safe C++-style algorithm using Span::CopyTo() - requires disabling the assertion in Span.
  // TODO: Investigate whether the assertion drops the performance.
  //ASSERT_CMP(vga_params.h_visible_area, <=, vram_line_bytes.size());
  vram_line_bytes.CopyTo(dma_buf_word_ptr, 0, vga_params.h_visible_area,
    [](uint8_t byte) { return palette[byte]; });
#endif

  // Right margin.
  for (int i = vga_params.h_margin / /* two pixels */ 2; i != 0; --i) {
    *dma_buf_word_ptr++ = palette[0];
  }
}

void __not_in_flash_func(dma_handler_vga)() {
  // VGA monitor line: 0..video_mode.whole_frame. Visible lines start at 0, vsync lines follow.
  static uint16_t y = 0;

  dma_hw->ints0 = 1u << dma_ch1;

  ++y;
  if (y == video_mode.whole_frame) {
    y = 0;
  }

  if (y >= video_mode.v_visible_area
      && y < (video_mode.v_visible_area + video_mode.v_front_porch)) {
    // vertical sync front porch
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufBlank], false);
    return;
  }
  if (y >= (video_mode.v_visible_area + video_mode.v_front_porch)
      && y < (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)) {
    // vertical sync pulse
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufVsync], false);
    return;
  }
  if (y >= (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)
      && y < video_mode.whole_frame) {
    // vertical sync back porch
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufBlank], false);
    return;
  }

  // Top and bottom black bars when the vertical size of the image is smaller than the vertical
  // resolution of the screen.
  if (y < vga_params.v_margin || y >= (vga_params.v_visible_area + vga_params.v_margin)) {
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufBlank], false);
    return;
  }

  // Image area.
  ASSERT_CMP(video_mode.v_scale, ==, 2);  // TODO: Support other scales.
  int active_buf_idx = -1;
  switch (y % 4) {
    case 0: {
      active_buf_idx = kDmaBufImageA;
      break;
    }
    case 1: {
      dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufImageA], false);
      return;
    };
    case 2: {
      active_buf_idx = kDmaBufImageB;
      break;
    }
    case 3: {
      dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufImageB], false);
      return;
    }
  }

  convert_vram_line_to_vga_dma_buf(vga_params, dma_bufs[active_buf_idx], vram, y);
  dma_channel_set_read_addr(dma_ch1, &dma_bufs[active_buf_idx], /*triogger=*/false);
}

void prepare_agat7_dma_bufs() {
  const int whole_line = video_mode.whole_line / video_mode.h_scale;
  const int h_sync_pulse_front =
      (video_mode.h_visible_area + video_mode.h_front_porch) / video_mode.h_scale;
  const int h_sync_pulse = video_mode.h_sync_pulse / video_mode.h_scale;

  // Prepare all 256 lines (0..255) from the frame buffer.
  for (int y = 0; y < 256; y++) {
    agat7_dma_bufs[y] = (uint32_t*)malloc(whole_line);
    uint8_t* line_bytes = (uint8_t*)agat7_dma_bufs[y];

    // Fill with the sync pattern.
    memset(line_bytes, (kNoSyncGpioByte ^ video_mode.sync_polarity), whole_line);
    memset(line_bytes + h_sync_pulse_front, (kHSyncGpioByte ^ video_mode.sync_polarity), h_sync_pulse);

    // Convert the frame buffer line through the palette.
    const auto vram_line_bytes = vram.LineBytes(y);
    uint16_t* const line_buf = (uint16_t*)agat7_dma_bufs[y];
    for (int x = 0; x < vram_line_bytes.size(); ++x) {
      line_buf[x] = palette[vram_line_bytes[x]];
    }
  }
}

void __not_in_flash_func(dma_handler_agat7)() {
  static uint16_t y = 0;

  dma_hw->ints0 = 1u << dma_ch1;
  ++y;
  if (y == video_mode.whole_frame) {
    y = 0;
  }
  if (y >= video_mode.v_visible_area
      && y < (video_mode.v_visible_area + video_mode.v_front_porch)) {
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufBlank], false);  // Start a V-front-porch.
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch)
      && y < (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)) {
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufVsync], false);  // Start a V-sync-pulse.
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)
    && y < video_mode.whole_frame) {
    dma_channel_set_read_addr(dma_ch1, &dma_bufs[kDmaBufBlank], false);  // Start a V-back-porch.
  }
  else if (y < video_mode.v_visible_area) {
    dma_channel_set_read_addr(dma_ch1, &agat7_dma_bufs[y], false);  // Start a pixel line.
  }
}

VgaParams calc_vga_params(const VideoMode& video_mode, const Vram& vram) {
  ASSERT_CMP(vram.height(), % 2 ==, 0);
  ASSERT_CMP(vram.width_px(), % 4 ==, 0);
  ASSERT_CMP(video_mode.v_visible_area, % 2 ==, 0);
  ASSERT_CMP(video_mode.v_visible_area, % video_mode.v_scale ==, 0);
  ASSERT_CMP(video_mode.h_visible_area, % 2 ==, 0);
  ASSERT_CMP(video_mode.h_visible_area, % video_mode.h_scale ==, 0);
  ASSERT_CMP(vram.width_px() * video_mode.h_scale, <=, video_mode.h_visible_area);

  VgaParams r;

  r.h_visible_area = vram.width_px();
  r.h_margin = (video_mode.h_visible_area / video_mode.h_scale - vram.width_px()) / 2;

  r.v_visible_area = vram.height() * video_mode.v_scale;
  if (r.v_visible_area > video_mode.v_visible_area) {  // Truncate the bottom part of the image.
    r.v_visible_area = video_mode.v_visible_area;
  }
  r.v_margin = (video_mode.v_visible_area / video_mode.v_scale - vram.height()) / 2;
  if (r.v_margin < 0) {
    r.v_margin = 0;
  }

  printf("VgaParams{.h_visible_area = %d, .h_margin = %d, .v_visible_area = %d, .v_margin = %d}\n",
      r.h_visible_area, r.h_margin, r.v_visible_area, r.v_margin);

  return r;
}

void start_vga() {
  constexpr uint8_t kRgbhvGpioStart =
    (Config::kBoard == Config::Board::rgb2vga) ? 8 :
    (Config::kBoard == Config::Board::murmulator) ? 6 :
    printf/*compile-time error*/("Unexpected BOARD\n");
  const PIO kRgbGenPio = pio0_hw;
  constexpr uint kStateMachine = 0;

  // DMA buffer structure:
  // _____HHHHH_____XXXXX
  //      ^h_sync_pulse_front
  //      |<->|h_sync_pulse

  // Width of a DMA buffer, in bytes - that is, in RGB output pixels.
  const int whole_line = video_mode.whole_line / video_mode.h_scale;

  ASSERT(video_mode.h_visible_area % video_mode.h_scale == 0);
  ASSERT(video_mode.h_front_porch % video_mode.h_scale == 0);
  ASSERT(video_mode.h_sync_pulse % video_mode.h_scale == 0);
  const int h_sync_pulse_front =
      (video_mode.h_visible_area + video_mode.h_front_porch) / video_mode.h_scale;
  const int h_sync_pulse = video_mode.h_sync_pulse / video_mode.h_scale;

  vga_params = calc_vga_params(video_mode, vram);

  set_sys_clock_khz(video_mode.sys_freq, /*required=*/true);
  sleep_ms(10);

  // Set the VGA pins.
  for (int i = kRgbhvGpioStart; i < kRgbhvGpioStart + 8; ++i) {
    pio_gpio_init(kRgbGenPio, i);
    gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(i, GPIO_SLEW_RATE_SLOW);
  }

  dma_bufs[kDmaBufBlank] = (uint32_t*)malloc(whole_line);
  memset(dma_bufs[kDmaBufBlank], (kNoSyncGpioByte ^ video_mode.sync_polarity), whole_line);
  memset((uint8_t*)dma_bufs[kDmaBufBlank] + h_sync_pulse_front,
      (kHSyncGpioByte ^ video_mode.sync_polarity), h_sync_pulse);

  dma_bufs[kDmaBufVsync] = (uint32_t*)malloc(whole_line);
  memset(dma_bufs[kDmaBufVsync], (kVSyncGpioByte ^ video_mode.sync_polarity), whole_line);
  memset((uint8_t*)dma_bufs[kDmaBufVsync] + h_sync_pulse_front,
      (kVHSyncGpioByte ^ video_mode.sync_polarity), h_sync_pulse);

  dma_bufs[kDmaBufImageA] = (uint32_t*) malloc(whole_line);
  memcpy(dma_bufs[kDmaBufImageA], dma_bufs[0], whole_line);

  dma_bufs[kDmaBufImageB] = (uint32_t*) malloc(whole_line);
  memcpy(dma_bufs[kDmaBufImageB], dma_bufs[0], whole_line);

  // PIO initialization.
  pio_sm_config state_machine_config = pio_get_default_sm_config();

  // PIO program load.
  const int pio_program_offset = pio_add_program(kRgbGenPio, &pio_vga_program);
  sm_config_set_wrap(&state_machine_config,
      pio_program_offset, pio_program_offset + (pio_vga_program.length - 1));

  sm_config_set_out_pins(&state_machine_config, kRgbhvGpioStart, 8);
  pio_sm_set_consecutive_pindirs(kRgbGenPio, kStateMachine, kRgbhvGpioStart, 8, true);

  sm_config_set_out_shift(&state_machine_config, true, true, 32);
  sm_config_set_fifo_join(&state_machine_config, PIO_FIFO_JOIN_TX);

  // TODO: Investigate the formula.
  sm_config_set_clkdiv(&state_machine_config,
      ((float)clock_get_hz(clk_sys) * video_mode.h_scale) / video_mode.pixel_freq);

  pio_sm_init(kRgbGenPio, kStateMachine, pio_program_offset, &state_machine_config);
  pio_sm_set_enabled(kRgbGenPio, kStateMachine, /*enabled=*/true);

  // DMA initialization.
  dma_ch0 = dma_claim_unused_channel(true);
  dma_ch1 = dma_claim_unused_channel(true);

  // DMA channel 0 - data.
  dma_channel_config ch0_config = dma_channel_get_default_config(dma_ch0);
  channel_config_set_transfer_data_size(&ch0_config, DMA_SIZE_32);
  channel_config_set_read_increment(&ch0_config, true);
  channel_config_set_write_increment(&ch0_config, false);
  channel_config_set_dreq(&ch0_config, DREQ_PIO0_TX0 + kStateMachine);
  // Set the DMA channel 1 to start when the DMA channel 0 completes.
  channel_config_set_chain_to(&ch0_config, dma_ch1);
  dma_channel_configure(
      dma_ch0,
      &ch0_config,
      /*write_addr=*/&kRgbGenPio->txf[kStateMachine],
      /*read_addr=*/dma_bufs[kDmaBufBlank],  // DMA will read data from the DMA buf.
      /*encoded_transfer_count=*/whole_line / 4,
      /*trigger=*/false  // Don't start yet.
  );

  // DMA channel 1 - control.
  dma_channel_config ch1_config = dma_channel_get_default_config(dma_ch1);
  channel_config_set_transfer_data_size(&ch1_config, DMA_SIZE_32);
  channel_config_set_read_increment(&ch1_config, false);
  channel_config_set_write_increment(&ch1_config, false);
  // Set the DMA channel 0 to start when the DMA channel 1 completes.
  channel_config_set_chain_to(&ch1_config, dma_ch0);
  dma_channel_configure(
      dma_ch1,
      &ch1_config,
      /*write_addr=*/&dma_hw->ch[dma_ch0].read_addr,  // DMA will set the read addr of channel 0.
      /*read_addr=*/&dma_bufs[kDmaBufBlank],  // DMA will read the address of the DMA buf.
      /*encoded_transfer_count=*/1,
      /*trigger=*/false  // Don't start yet.
  );

  // Set IRQ0 to trigger when the DMA channel 1 completes.
  dma_channel_set_irq0_enabled(dma_ch1, /*enabled=*/true);

  // Assign an IRQ0 handler - a callback that will be called when the DMA channel 1 completes.
  switch (Config::kMode) {
    case Config::Mode::vga: {
      irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_vga);
    } break;
    case Config::Mode::agat7: {
      irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_agat7);
    } break;
  }
  irq_set_enabled(DMA_IRQ_0, /*enabled=*/true);

  dma_start_channel_mask((1u << dma_ch0));  // Start DMA channel 1.
}

//-------------------------------------------------------------------------------------------------

int main() {
  vreg_set_voltage(VREG_VOLTAGE_1_25);
  sleep_ms(100);

  stdio_init_all();
  sleep_ms(1000);  // Allow the USB UART to initialize for printf().
  printf("Started.\n");

  debug::SetBuiltInLed(false);  // Clear the assertion LED from the state before reset.
  Agat7Picture agat7_picture(agat7_renderer);
  agat7_picture.DrawPicture(kVideoModeAgat7);

  switch (Config::kMode) {
    case Config::Mode::vga: {
      video_mode = kVideoModeVga640x480x60;
    } break;
    case Config::Mode::agat7: {
      video_mode = kVideoModeAgat7;
    } break;
  };

  palette.Init(video_mode);
  prepare_agat7_dma_bufs();
  start_vga();

  for (;;);
}

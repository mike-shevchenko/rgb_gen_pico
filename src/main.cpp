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

// Sync pulse patterns (positive polarity).
constexpr uint8_t kNoSync = 0b00000000;
constexpr uint8_t kVSync = 0b10000000;
constexpr uint8_t kHSync = 0b01000000;
constexpr uint8_t kVHSync = 0b11000000;
constexpr uint8_t kSyncPolatiryMaskPositive = kNoSync;  // Invert no bits.
constexpr uint8_t kSyncPolatiryMaskNegative = kVHSync;  // Invert both H and V sync bits.

constexpr VideoMode kVideoModeVga640x480x60{
    .sys_freq = 252000,
    .pixel_freq = 25175000.0,
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
    .div = 2,
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
    .sys_freq = 252000,
    .pixel_freq = 5250000.0,
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
    .div = 1,  // Scan-doubling is not used.
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

// PIO and SM for VGA.
#define PIO_VGA pio0
#define DREQ_PIO_VGA DREQ_PIO0_TX0
#define SM_VGA 0

static Vram vram(/*width_px=*/256, /*height=*/256);
static Agat7Renderer agat7_renderer(vram);

//-------------------------------------------------------------------------------------------------
// VGA

static int dma_ch0;
static int dma_ch1;
static uint offset;

static VideoMode video_mode;

// Position of Vram on the VGA video output, in VGA pixels.
static int16_t h_visible_area;
static int16_t h_margin;
static int16_t v_visible_area;
static int16_t v_margin;

// 4 buffers, each represents a full video-out line (with porches), including invisible lines.
constexpr int kDmaBufBlank = 0;
constexpr int kDmaBufVsync = 1;
constexpr int kDmaBufImageA = 2;
constexpr int kDmaBufImageB = 3;
static uint32_t* dma_buf[4];

static uint16_t palette[256];
uint32_t* prepared_line_buffers[256];  // One buffer per line.

void __not_in_flash_func(dma_handler_vga)()
{
  static uint16_t y = 0; // VGA monitor line: 0..video_mode.whole_frame.

  static uint8_t* scr_buffer = nullptr;

  dma_hw->ints0 = 1u << dma_ch1;

  y++;

  if (y == video_mode.whole_frame)
  {
    y = 0;
    scr_buffer = vram.LineBytes(0).data();
  }

  if (y >= video_mode.v_visible_area && y < (video_mode.v_visible_area + video_mode.v_front_porch))
  {
    // vertical sync front porch
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);
    return;
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch)
      && y < (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse))
  {
    // vertical sync pulse
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufVsync], false);
    return;
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)
      && y < video_mode.whole_frame)
  {
    // vertical sync back porch
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);
    return;
  }

  if (!(scr_buffer))
  {
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufImageA], false);
    return;
  }

  // Top and bottom black bars when the vertical size of the image is smaller than the vertical
  // resolution of the screen.
  if (y < v_margin || y >= (v_visible_area + v_margin))
  {
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);
    return;
  }

  // Image area.
  uint8_t line = y % (2 * video_mode.div);

  switch (video_mode.div)
  {
  case 2:
    if (line > 1)
      line++;
    break;

  case 3:
    if (line == 2 || line == 5)
      line--;
    break;

  case 4:
    if (line > 2)
      line--;

    if (line == 6)
      line--;

    if ((line == 2) || (line == 5))
      line--;

    break;

  default:
    break;
  }

  int active_buf_idx;

  switch (line)
  {
  case 0:
    active_buf_idx = kDmaBufImageA;
    break;

  case 1:
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufImageA], false);
    return;

  case 2:
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);
    return;

  case 3:
    active_buf_idx = kDmaBufImageB;
    break;

  case 4:
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufImageB], false);
    return;

  case 5:
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);
    return;

  default:
    return;
  }

  // Represents the line in the original captured image.
  ASSERT_CMP(y, >=, v_margin);
  const uint16_t scaled_y = (y - v_margin) / video_mode.div;

  const uint8_t* vram_byte_ptr = vram.LineBytes(scaled_y).data();
  uint16_t* dma_buf_word_ptr = (uint16_t*)dma_buf[active_buf_idx];

  // left margin
  for (int x = h_margin; x--;) {
    *dma_buf_word_ptr++ = palette[0];
  }

  int x = 0;

  while ((x + 4) <= h_visible_area)
  {
    *dma_buf_word_ptr++ = palette[*vram_byte_ptr++];
    *dma_buf_word_ptr++ = palette[*vram_byte_ptr++];
    *dma_buf_word_ptr++ = palette[*vram_byte_ptr++];
    *dma_buf_word_ptr++ = palette[*vram_byte_ptr++];

    x += 4;
  }

  while (x < h_visible_area)
  {
    *dma_buf_word_ptr++ = palette[*vram_byte_ptr++];
    x++;
  }

  // right margin
  for (int x = h_margin; x--;) {
    *dma_buf_word_ptr++ = palette[0];
  }

  dma_channel_set_read_addr(dma_ch1, &dma_buf[active_buf_idx], false);
}

void prepare_frame_buffer_lines() {
  int whole_line = video_mode.whole_line / video_mode.div;
  int h_sync_pulse_front = (video_mode.h_visible_area + video_mode.h_front_porch) / video_mode.div;
  int h_sync_pulse = video_mode.h_sync_pulse / video_mode.div;

  // Prepare all 256 lines (0..255) from the frame buffer.
  for (int y = 0; y < 256; y++) {
    prepared_line_buffers[y] = (uint32_t*)calloc(whole_line / 4, sizeof(uint32_t));
    uint8_t* line_bytes = (uint8_t*)prepared_line_buffers[y];

    // Fill with the sync pattern.
    memset(line_bytes, (kNoSync ^ video_mode.sync_polarity), whole_line);
    memset(line_bytes + h_sync_pulse_front, (kHSync ^ video_mode.sync_polarity), h_sync_pulse);

    // Convert the frame buffer line through the palette.
    const std::span<uint8_t> vram_line = vram.LineBytes(y);
    uint16_t* const line_buf = (uint16_t*)prepared_line_buffers[y];
    for (size_t x = 0; x < vram_line.size(); ++x) {
      line_buf[x] = palette[vram_line[x]];
    }
  }
}

void __not_in_flash_func(dma_handler_agat7)() {
  static uint16_t y = 0;

  dma_hw->ints0 = 1u << dma_ch1;
  y++;
  if (y == video_mode.whole_frame) {
    y = 0;
  }
  if (y >= video_mode.v_visible_area
      && y < (video_mode.v_visible_area + video_mode.v_front_porch)) {
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);  // Start a V-front-porch.
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch)
      && y < (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)) {
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufVsync], false);  // Sstart a V-sync-pulse.
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)
    && y < video_mode.whole_frame) {
    dma_channel_set_read_addr(dma_ch1, &dma_buf[kDmaBufBlank], false);  // Strat a V-back-porch.
  }
  else if (y < video_mode.v_visible_area) {
    dma_channel_set_read_addr(dma_ch1, &prepared_line_buffers[y], false);  // Start a pixel line.
  }
}

void start_vga() {
  constexpr uint8_t kVgaGpioStart =
    (Config::kBoard == Config::Board::rgb2vga) ? 8 :
    (Config::kBoard == Config::Board::murmulator) ? 6 :
    printf/*compile-time error*/("Unexpected BOARD\n");

  const int whole_line = video_mode.whole_line / video_mode.div;
  const int h_sync_pulse_front = (video_mode.h_visible_area + video_mode.h_front_porch) / video_mode.div;
  const int h_sync_pulse = video_mode.h_sync_pulse / video_mode.div;

  const uint16_t h_width = (uint16_t)(video_mode.h_visible_area / (video_mode.div * 4)) * 2;
  constexpr int zx_spectrum_pixel_clock_mhz = 7'000'000;
  constexpr int pal_visible_line_duration_us = 52;
  h_margin = (h_width - (uint8_t)zx_spectrum_pixel_clock_mhz * (pal_visible_line_duration_us / 2)) / 2;
  if (h_margin < 0) {
    h_margin = 0;
  }
  h_visible_area = h_width - h_margin * 2;

  v_visible_area = vram.height() * video_mode.div;
  v_margin = ((int16_t)((video_mode.v_visible_area - v_visible_area) / (video_mode.div * 2) + 0.5))
      * video_mode.div;
  if (v_margin < 0)
    v_margin = 0;

  set_sys_clock_khz(video_mode.sys_freq, /*required=*/true);
  sleep_ms(10);

  // Palette initialization.
  // TODO: Investigate the proper way to output "bright black".
  for (int i = 0; i < 16; ++i) {
    uint8_t Yi = (i >> 3) & 1;
    uint8_t Ri = ((i >> 2) & 1) ? (Yi ? 0b00000011 : 0b00000010) : 0;
    uint8_t Gi = ((i >> 1) & 1) ? (Yi ? 0b00001100 : 0b00001000) : 0;
    uint8_t Bi = ((i >> 0) & 1) ? (Yi ? 0b00110000 : 0b00100000) : 0;

    for (int j = 0; j < 16; ++j) {
      uint8_t Yj = (j >> 3) & 1;
      uint8_t Rj = ((j >> 2) & 1) ? (Yj ? 0b00000011 : 0b00000010) : 0;
      uint8_t Gj = ((j >> 1) & 1) ? (Yj ? 0b00001100 : 0b00001000) : 0;
      uint8_t Bj = ((j >> 0) & 1) ? (Yj ? 0b00110000 : 0b00100000) : 0;

      palette[(i * 16) + j] =
          ((uint16_t)(Ri | Gi | Bi | (kNoSync ^ video_mode.sync_polarity)) << 8)
          | (Rj | Gj | Bj | (kNoSync ^ video_mode.sync_polarity));
    }
  }

  // Set the VGA pins.
  for (int i = kVgaGpioStart; i < kVgaGpioStart + 8; ++i) {
    pio_gpio_init(PIO_VGA, i);
    gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(i, GPIO_SLEW_RATE_SLOW);
  }

  dma_buf[kDmaBufBlank] = (uint32_t*)malloc(whole_line);
  memset(dma_buf[kDmaBufBlank], (kNoSync ^ video_mode.sync_polarity), whole_line);
  memset((uint8_t*)dma_buf[kDmaBufBlank] + h_sync_pulse_front, (kHSync ^ video_mode.sync_polarity),
      h_sync_pulse);

  dma_buf[kDmaBufVsync] = (uint32_t*)malloc(whole_line);
  memset(dma_buf[kDmaBufVsync], (kVSync ^ video_mode.sync_polarity), whole_line);
  memset((uint8_t*)dma_buf[kDmaBufVsync] + h_sync_pulse_front, (kVHSync ^ video_mode.sync_polarity),
      h_sync_pulse);

  dma_buf[kDmaBufImageA] = (uint32_t*) malloc(whole_line);
  memcpy(dma_buf[kDmaBufImageA], dma_buf[0], whole_line);

  dma_buf[kDmaBufImageB] = (uint32_t*) malloc(whole_line);
  memcpy(dma_buf[kDmaBufImageB], dma_buf[0], whole_line);

  // PIO initialization.
  pio_sm_config c = pio_get_default_sm_config();

  // PIO program load.
  offset = pio_add_program(PIO_VGA, &pio_vga_program);
  sm_config_set_wrap(&c, offset, offset + (pio_vga_program.length - 1));

  sm_config_set_out_pins(&c, kVgaGpioStart, 8);
  pio_sm_set_consecutive_pindirs(PIO_VGA, SM_VGA, kVgaGpioStart, 8, true);

  sm_config_set_out_shift(&c, true, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  sm_config_set_clkdiv(&c,
      ((float)clock_get_hz(clk_sys) * video_mode.div) / video_mode.pixel_freq);

  pio_sm_init(PIO_VGA, SM_VGA, offset, &c);
  pio_sm_set_enabled(PIO_VGA, SM_VGA, true);

  // DMA initialization.
  dma_ch0 = dma_claim_unused_channel(true);
  dma_ch1 = dma_claim_unused_channel(true);

  // The main (data) DMA channel.
  dma_channel_config c0 = dma_channel_get_default_config(dma_ch0);

  channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
  channel_config_set_read_increment(&c0, true);
  channel_config_set_write_increment(&c0, false);
  channel_config_set_dreq(&c0, DREQ_PIO_VGA + SM_VGA);
  channel_config_set_chain_to(&c0, dma_ch1);  // Chain to the control channel.

  dma_channel_configure(
      dma_ch0,
      &c0,
      &PIO_VGA->txf[SM_VGA],  // Write address.
      dma_buf[kDmaBufBlank],  // Read address.
      whole_line / 4,
      false  // Don't start yet.
  );

  // Control DMA channel.
  dma_channel_config c1 = dma_channel_get_default_config(dma_ch1);

  channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
  channel_config_set_read_increment(&c1, false);
  channel_config_set_write_increment(&c1, false);
  channel_config_set_chain_to(&c1, dma_ch0); // Chain to the other channel.

  dma_channel_configure(
      dma_ch1,
      &c1,
      &dma_hw->ch[dma_ch0].read_addr,  // Write address.
      &dma_buf[kDmaBufBlank],  // Read address.
      1,
      false  // Don't start yet.
  );

  dma_channel_set_irq0_enabled(dma_ch1, true);

  // Configure the processor to run the DMA handler when DMA IRQ 0 is asserted.
  switch (Config::kMode) {
    case Config::Mode::vga: {
      irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_vga);
    } break;
    case Config::Mode::agat7: {
      irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_agat7);
    } break;
  }
  irq_set_enabled(DMA_IRQ_0, /*enabled=*/true);

  dma_start_channel_mask((1u << dma_ch0));
}

//-------------------------------------------------------------------------------------------------

int main() {
  vreg_set_voltage(VREG_VOLTAGE_1_25);
  sleep_ms(100);

  stdio_init_all();
  sleep_ms(1000);  // Allow the USB UART to initialize for printf().
  printf("Started.\n");

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

  start_vga();
  prepare_frame_buffer_lines();

  for (;;);
}

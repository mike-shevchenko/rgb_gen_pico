#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/systick.h"

#include "../build/programs.pio.h"

#include "agat7_font.c"

//-------------------------------------------------------------------------------------------------
// Config

// Choose the hardware version.
#if !defined(BOARD)  // Can be defined externally with -DBOARD=...
  #define BOARD RGB2VGA
  //#define BOARD MURMULATOR
#endif // defined(BOARD)

// The first VGA GPIO pin.
#if (BOARD == RGB2VGA)
  #define VGA_PIN_D0 8
#elif (BOARD == MURMULATOR)
  #define VGA_PIN_D0 6
#else
  #error "Unsupported BOARD."
#endif

typedef enum color_t {
  COLOR_BLACK = 0,
  COLOR_RED = 0x04,
  COLOR_GREEN = 0x02,
  COLOR_BLUE = 0x01,
  COLOR_MAGENTA = COLOR_RED | COLOR_BLUE,
  COLOR_YELLOW = COLOR_RED | COLOR_GREEN,
  COLOR_CYAN = COLOR_BLUE | COLOR_GREEN,
} color_t;

typedef enum video_out_mode_t {
  MODE_640x480_60Hz,
  MODE_720x576_50Hz,
} video_out_mode_t;

typedef struct video_mode_t {
  uint32_t sys_freq;
  float pixel_freq;
  uint16_t h_visible_area;
  uint16_t v_visible_area;
  uint16_t whole_line;
  uint16_t whole_frame;
  uint8_t h_front_porch;
  uint8_t h_sync_pulse;
  uint8_t h_back_porch;
  uint8_t v_front_porch;
  uint8_t v_sync_pulse;
  uint8_t v_back_porch;
  uint8_t sync_polarity;
  uint8_t div;
} video_mode_t;

video_mode_t mode_640x480_60Hz = {
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
    .sync_polarity = 0b11000000, // negative
    .div = 2,
};

video_mode_t mode_agat7 = {
    .sys_freq = 126000,  // 126MHz system clock (keep same as PAL version).
    .pixel_freq = 5250000.0,  // 5.25MHz pixel clock (Agat-7 specification).
    .h_visible_area = 256,  // 256 visible pixels (Agat-7 specification).
    .v_visible_area = 256,  // 256 visible lines (Agat-7 specification).
    .whole_line = 336,  // Total pixels per line (256 + porches + sync).
    .whole_frame = 312,  // Total lines per frame (PAL-compatible 50Hz).
    .h_front_porch = 16,  // Horizontal front porch.
    .h_sync_pulse = 32,  // Horizontal sync pulse.
    .h_back_porch = 32,  // Horizontal back porch.
    .v_front_porch = 16,  // Vertical front porch. 
    .v_sync_pulse = 8,  // Vertical sync pulse.
    .v_back_porch = 32,  // Vertical back porch.
    .sync_polarity = 0b11000000, // Negative sync polarity.
    .div = 1,  // Keep the divider from working version.
};

// PIO and SM for VGA.
#define PIO_VGA pio0
#define DREQ_PIO_VGA DREQ_PIO0_TX0
#define SM_VGA 0

#define FREQUENCY 7000000

// Video timings.
// 64 us - duration of a single scanline, 12 us - combined duration of the front porch, horizontal
// sync pulse, and back porch.
#define ACTIVE_VIDEO_TIME (64 - 12)

// Video buffer size.
#define V_BUF_W 256  // For the VGA version: (ACTIVE_VIDEO_TIME * (FREQUENCY / 1000000))
#define V_BUF_H 256  // For the VGA version: 304
#define V_BUF_SZ (V_BUF_H * V_BUF_W / 2)  // 2 pixels per byte

uint8_t g_v_buf[V_BUF_SZ];

//-------------------------------------------------------------------------------------------------
// VGA

// Sync pulse patterns (positive polarity).
#define NO_SYNC 0b00000000
#define V_SYNC 0b10000000
#define H_SYNC 0b01000000
#define VH_SYNC 0b11000000

static int dma_ch0;
static int dma_ch1;
static uint offset;

static video_mode_t video_mode;
static int16_t h_visible_area;
static int16_t h_margin;
static int16_t v_visible_area;
static int16_t v_margin;

static uint32_t *v_out_dma_buf[4];
static uint16_t palette[256];

void __not_in_flash_func(memset32)(uint32_t *dst, const uint32_t data, uint32_t size);

#if 0 // Original VGA code.

void __not_in_flash_func(dma_handler_vga)(void)
{
  static uint16_t y = 0;

  static uint8_t *scr_buffer = NULL;

  dma_hw->ints0 = 1u << dma_ch1;

  y++;

  if (y == video_mode.whole_frame)
  {
    y = 0;
    scr_buffer = g_v_buf;
  }

  if (y >= video_mode.v_visible_area && y < (video_mode.v_visible_area + video_mode.v_front_porch))
  {
    // vertical sync front porch
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
    return;
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch) && y < (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse))
  {
    // vertical sync pulse
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[1], false);
    return;
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse) && y < video_mode.whole_frame)
  {
    // vertical sync back porch
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
    return;
  }

  if (!(scr_buffer))
  {
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[2], false);
    return;
  }

  // top and bottom black bars when the vertical size of the image is smaller than the vertical resolution of the screen
  if (y < v_margin || y >= (v_visible_area + v_margin))
  {
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
    return;
  }

  // image area
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
    active_buf_idx = 2;
    break;

  case 1:
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[2], false);
    return;

  case 2:
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
    return;

  case 3:
    active_buf_idx = 3;
    break;

  case 4:
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[3], false);
    return;

  case 5:
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
    return;

  default:
    return;
  }

  uint16_t scaled_y = (y - v_margin) / video_mode.div; // represents the line in the original captured image
  uint8_t *scr_line = &scr_buffer[scaled_y * (V_BUF_W / 2)];
  uint16_t *line_buf = (uint16_t *)v_out_dma_buf[active_buf_idx];

  // left margin
  for (int x = h_margin; x--;)
    *line_buf++ = palette[0];

    int x = 0;

    while ((x + 4) <= h_visible_area)
    {
      *line_buf++ = palette[*scr_line++];
      *line_buf++ = palette[*scr_line++];
      *line_buf++ = palette[*scr_line++];
      *line_buf++ = palette[*scr_line++];

      x += 4;
    }

    while (x < h_visible_area)
    {
      *line_buf++ = palette[*scr_line++];
      x++;
    }

  // right margin
  for (int x = h_margin; x--;)
    *line_buf++ = palette[0];

  dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[active_buf_idx], false);
}

#else // Agat-7 code.

uint32_t *prepared_line_buffers[256];  // One buffer per line.

void prepare_frame_buffer_lines(void) {
  int whole_line = video_mode.whole_line / video_mode.div;
  int h_sync_pulse_front = (video_mode.h_visible_area + video_mode.h_front_porch) / video_mode.div;
  int h_sync_pulse = video_mode.h_sync_pulse / video_mode.div;
    
  // Prepare all 256 lines (0..255) from the frame buffer.
  for (int y = 0; y < 256; y++) {
    prepared_line_buffers[y] = (uint32_t*)calloc(whole_line / 4, sizeof(uint32_t));
    uint8_t* line_bytes = (uint8_t*)prepared_line_buffers[y];
        
    // Fill with the sync pattern.
    memset(line_bytes, (NO_SYNC ^ video_mode.sync_polarity), whole_line);
    memset(line_bytes + h_sync_pulse_front, (H_SYNC ^ video_mode.sync_polarity), h_sync_pulse);
        
    // Convert the frame buffer line through the palette.
    const uint8_t* const scr_line = &g_v_buf[y * (V_BUF_W / 2)];
    uint16_t* const line_buf = (uint16_t*)prepared_line_buffers[y];
        
    for (int x = 0; x < V_BUF_W / 2; x++) {
      line_buf[x] = palette[scr_line[x]];
    }
  }
}

void __not_in_flash_func(dma_handler_vga)()
{
  static uint16_t y = 0;

  dma_hw->ints0 = 1u << dma_ch1;
  y++;
  if (y == video_mode.whole_frame) y = 0;

  if (y >= video_mode.v_visible_area
      && y < (video_mode.v_visible_area + video_mode.v_front_porch)) {
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch)
      && y < (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)) {
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[1], false);
  }
  else if (y >= (video_mode.v_visible_area + video_mode.v_front_porch + video_mode.v_sync_pulse)
    && y < video_mode.whole_frame) {
    dma_channel_set_read_addr(dma_ch1, &v_out_dma_buf[0], false);
  }
  else if (y < video_mode.v_visible_area) {
    dma_channel_set_read_addr(dma_ch1, &prepared_line_buffers[y], false);
  }
}

#endif // 0

void start_vga(void)
{
  int whole_line = video_mode.whole_line / video_mode.div;
  int h_sync_pulse_front = (video_mode.h_visible_area + video_mode.h_front_porch) / video_mode.div;
  int h_sync_pulse = video_mode.h_sync_pulse / video_mode.div;

  h_visible_area = (uint16_t)(video_mode.h_visible_area / (video_mode.div * 4)) * 2;
  h_margin = (h_visible_area - (uint8_t)(FREQUENCY / 1000000) * (ACTIVE_VIDEO_TIME / 2)) / 2;

  if (h_margin < 0) {
    h_margin = 0;
  }

  h_visible_area -= h_margin * 2;

  v_visible_area = V_BUF_H * video_mode.div;
  v_margin = ((int16_t)((video_mode.v_visible_area - v_visible_area) / (video_mode.div * 2) + 0.5))
      * video_mode.div;

  if (v_margin < 0)
    v_margin = 0;

  set_sys_clock_khz(video_mode.sys_freq, true);
  sleep_ms(10);

  // Palette initialization.
  for (int i = 0; i < 16; ++i) {
    uint8_t Yi = (i >> 3) & 1;
    uint8_t Ri = ((i >> 2) & 1) ? (Yi ? 0b00000011 : 0b00000010) : 0;
    uint8_t gi = ((i >> 1) & 1) ? (Yi ? 0b00001100 : 0b00001000) : 0;
    uint8_t bi = ((i >> 0) & 1) ? (Yi ? 0b00110000 : 0b00100000) : 0;

    for (int j = 0; j < 16; ++j) {
      uint8_t Yj = (j >> 3) & 1;
      uint8_t Rj = ((j >> 2) & 1) ? (Yj ? 0b00000011 : 0b00000010) : 0;
      uint8_t gj = ((j >> 1) & 1) ? (Yj ? 0b00001100 : 0b00001000) : 0;
      uint8_t bj = ((j >> 0) & 1) ? (Yj ? 0b00110000 : 0b00100000) : 0;

      palette[(i * 16) + j] = ((uint16_t)(Ri | gi | bi | (NO_SYNC ^ video_mode.sync_polarity)) << 8)
          | (Rj | gj | bj | (NO_SYNC ^ video_mode.sync_polarity));
    }
  }

  // Set the VGA pins.
  for (int i = VGA_PIN_D0; i < VGA_PIN_D0 + 8; ++i) {
    pio_gpio_init(PIO_VGA, i);
    gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(i, GPIO_SLEW_RATE_SLOW);
  }

  // Allocate memory for the line template definitions - individual allocations.

  // Empty line.
  v_out_dma_buf[0] = calloc(whole_line / 4, sizeof(uint32_t));
  memset((uint8_t *)v_out_dma_buf[0], (NO_SYNC ^ video_mode.sync_polarity), whole_line);
  memset((uint8_t *)v_out_dma_buf[0] + h_sync_pulse_front, (H_SYNC ^ video_mode.sync_polarity),
      h_sync_pulse);
  
  // Vertical sync pulse.
  v_out_dma_buf[1] = calloc(whole_line / 4, sizeof(uint32_t));
  memset((uint8_t *)v_out_dma_buf[1], (V_SYNC ^ video_mode.sync_polarity), whole_line);
  memset((uint8_t *)v_out_dma_buf[1] + h_sync_pulse_front, (VH_SYNC ^ video_mode.sync_polarity),
      h_sync_pulse);

  // Image line.
  v_out_dma_buf[2] = calloc(whole_line / 4, sizeof(uint32_t));
  memcpy((uint8_t *)v_out_dma_buf[2], (uint8_t *)v_out_dma_buf[0], whole_line);

  // Image line.
  v_out_dma_buf[3] = calloc(whole_line / 4, sizeof(uint32_t));
  memcpy((uint8_t *)v_out_dma_buf[3], (uint8_t *)v_out_dma_buf[0], whole_line);

  // PIO initialization.
  pio_sm_config c = pio_get_default_sm_config();

  // PIO program load.
  offset = pio_add_program(PIO_VGA, &pio_vga_program);
  sm_config_set_wrap(&c, offset, offset + (pio_vga_program.length - 1));

  sm_config_set_out_pins(&c, VGA_PIN_D0, 8);
  pio_sm_set_consecutive_pindirs(PIO_VGA, SM_VGA, VGA_PIN_D0, 8, true);

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
      v_out_dma_buf[0],  // Read address.
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
      &v_out_dma_buf[0],  // Read address.
      1,
      false  // Don't start yet.
  );

  dma_channel_set_irq0_enabled(dma_ch1, true);

  // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted.
  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_vga);
  irq_set_enabled(DMA_IRQ_0, true);

  dma_start_channel_mask((1u << dma_ch0));
}

//-------------------------------------------------------------------------------------------------
// Agat-7 text mode.

// Agat-7 display parameters.
#define PHYSICAL_WIDTH 256  // Physical video signal width.
#define PHYSICAL_HEIgHT 256  // Physical video signal height.
#define TEXT_COLS 32  // 32 characters per line.
#define TEXT_ROWS 32  // 32 lines (full height used).
#define CHAR_WIDTH 7  // 7 pixels per character (no spacing in text area).
#define CHAR_HEIgHT 8   // 8 pixels per character.

// Text area dimensions.
#define TEXT_AREA_WIDTH (TEXT_COLS * CHAR_WIDTH)  // 224 pixels.
#define TEXT_AREA_HEIgHT (TEXT_ROWS * CHAR_HEIgHT)  // 256 pixels.

// Centering margins.
#define H_MARgIN ((PHYSICAL_WIDTH - TEXT_AREA_WIDTH) / 2)  // 16 pixels on each side.
#define V_MARgIN ((PHYSICAL_HEIgHT - TEXT_AREA_HEIgHT) / 2)  // 0 pixels (full height).

// Text buffer for 32x32 characters.
char text_buffer[TEXT_ROWS][TEXT_COLS + 1];  // +1 for null terminator per row.

void init_text_buffer() {
  for (int row = 0; row < TEXT_ROWS; row++) {
    for (int col = 0; col < TEXT_COLS; col++) {
      text_buffer[row][col] = ' ';  // Fill with spaces.
    }
    text_buffer[row][TEXT_COLS] = '\0';  // Null-terminate each row.
  }
}

// Print the string at text coordinates (0..31, 0..31).
void print_at(int text_x, int text_y, const char* str) {
  if (text_y < 0 || text_y >= TEXT_ROWS) {
    return;
  }
  
  int col = text_x;
  const char* ptr = str;
  
  while (*ptr && col < TEXT_COLS) {
    if (*ptr == '\n') {
      // Move to the next line.
      ++text_y;
      col = text_x;
      if (text_y >= TEXT_ROWS) {
        break;
      }
    } else {
      // Place the character to the buffer.
      if (col >= 0) {
        text_buffer[text_y][col] = *ptr;
      }
      ++col;
    }
    ++ptr;
  }
}

// NOTE: Accounts for the margins.
void render_char_at_text_pos(int text_x, int text_y, char c) {
  if (c < 32 || c > 127) {
    // Support only printable ASCII and 127.
    return;
  }
  
  const int char_index = c - 32;
  
  // Calculate pixel position with margin
  const int base_pixel_x = H_MARgIN + (text_x * CHAR_WIDTH);
  const int base_pixel_y = V_MARgIN + (text_y * CHAR_HEIgHT);
  
  for (int row = 0; row < 8; row++) {
    const uint8_t char_line = agat7_font[char_index][row];
      
    for (int col = 0; col < 7; col++) {  // 7 pixels wide.
      if (char_line & (1 << (7 - col))) { // Test the bit (MSb first).
        const int screen_x = base_pixel_x + col;
        const int screen_y = base_pixel_y + row;
        
        // Make sure we're within the physical screen bounds.
        if (screen_x >= 0 && screen_x < V_BUF_W && 
            screen_y >= 0 && screen_y < V_BUF_H) {
            
          int buf_index = screen_y * (V_BUF_W / 2) + screen_x / 2;
          uint8_t color = 0x0F;  // White pixel (IRgb = 1111).
          
          if (screen_x & 1) {
            g_v_buf[buf_index] = (g_v_buf[buf_index] & 0x0F) | (color << 4);
          } else {
            g_v_buf[buf_index] = (g_v_buf[buf_index] & 0xF0) | (color & 0x0F);
          }
        }
      }
    }
  }
}

// Render the entire text buffer to the video buffer (with the proper margins).
void render_text_buffer() {
  for (int text_row = 0; text_row < TEXT_ROWS; text_row++) {
    for (int text_col = 0; text_col < TEXT_COLS; text_col++) {
      const char c = text_buffer[text_row][text_col];
      render_char_at_text_pos(text_col, text_row, c);
    }
  }
}

void draw_horz(int x_half, int y, int len_half, uint8_t color) {
  if (x_half < 0 || len_half <= 0 || x_half + len_half > V_BUF_W / 2) {
    printf("%s(): Invalid coordinates: x_half %d, y %d, len_half %d.\n",
        __func__, x_half, y, len_half);
    return;
  }
  for (int b = x_half; b < x_half + len_half; ++b) {
    const int buf_index = y * (V_BUF_W / 2) + b;
    g_v_buf[buf_index] = (color & 0xF) | (color << 4);  // Fill two consecutive pixels.
  }
}

void draw_vert(int x_half, int y, int len, uint8_t color) {
  if (x_half < 0 || len <= 0 || y + len > V_BUF_H) {
    printf("%s(): Invalid coordinates: x_half %d, y %d, len %d.\n",
        __func__, x_half, y, len);
    return;
  }
  for (int line = y; line < y + len; ++line) {
    const int buf_index = line * (V_BUF_W / 2) + x_half;
    g_v_buf[buf_index] = (color & 0xF) | (color << 4);  // Fill two consecutive pixels.
  }
}

//-------------------------------------------------------------------------------------------------

void draw_horz_rgb_line(int y) {
  static const int s = 16;
  draw_horz(s * 0, y, s, COLOR_RED);
  draw_horz(s * 1, y, s, COLOR_GREEN);
  draw_horz(s * 2, y, s, COLOR_BLUE);
  draw_horz(s * 3, y, s, COLOR_RED);
  draw_horz(s * 4, y, s, COLOR_GREEN);
  draw_horz(s * 5, y, s, COLOR_BLUE);
  draw_horz(s * 6, y, s, COLOR_RED);
  draw_horz(s * 7, y, s, COLOR_GREEN);
}

void draw_grid(void) {
  // Draw a 2-pixel-wide frame around the screen.
  draw_horz_rgb_line(0);
  draw_horz_rgb_line(1);
  draw_horz_rgb_line(254);
  draw_horz_rgb_line(255);
  draw_vert(0, 0, 256, COLOR_RED);
  draw_vert(127, 0, 256, COLOR_GREEN);
}

void draw_color_bars(void ) {
  for (int color = 0; color < 16; ++color) {
    for (int x_half = 0; x_half < 7; ++x_half) {  // Each bar will be 14 pixels wide.
      draw_vert(8 + color * 7 + x_half, 128, 32, color);
    }
  }
}

void print_some_text(void) {
  memset(g_v_buf, 0, V_BUF_SZ);
  
  init_text_buffer();
  
  print_at(11, 1, "** agat **");
  print_at(0, 3, "TEXT: 32X32, GRAPHICS: 256x256");
  print_at(0, 4, "MARGINS: 16PX LEFT+RIGHT");

  print_at(0, 6, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  print_at(0, 7, "0123456789 !@#$%^&*()_+-=");

  print_at(0, 9, "00000000001111111111222222222233");
  print_at(0, 10, "01234567890123456789012345678901");

  print_at(0, 13, "....NORMAL..... ....BRIGHT.....");
  print_at(0, 15, "0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7");

  print_at(0, 29, "WRITTEN BY MIKE SHEVCHENKO");
  print_at(0, 30, "  USING CLAUDE 4 SONNET");
  
  render_text_buffer();
}

int main(void) {
  vreg_set_voltage(VREG_VOLTAGE_1_25);
  sleep_ms(100);

  stdio_init_all();
  sleep_ms(1000);  // Allow the USB UART to initialize for printf().
  printf("Started.\n");

  print_some_text();
  draw_grid();
  draw_color_bars();

  video_mode = mode_agat7;
  start_vga();

  prepare_frame_buffer_lines();

  for(;;);
}

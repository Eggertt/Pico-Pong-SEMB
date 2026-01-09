#include <pico/scanvideo.h>
#include <pico/scanvideo/composable_scanline.h>
#include <pico/scanvideo/scanvideo_base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vga.h"

extern const struct scanvideo_pio_program video_24mhz_composable;

static inline uint16_t *
prepare_scanline_buffer(struct scanvideo_scanline_buffer *dest, uint width);

static inline void
finalize_scanline_buffer(struct scanvideo_scanline_buffer *dest);

// === Double Buffer Logic ===
// Two buffers of 320x240 pixels, 8-bit per pixel (Total 150KB)
#define BUFFER_SIZE (320 * 240)
static uint8_t canvas_buffers[2][BUFFER_SIZE];

static volatile int front_buffer_idx = 0;
static volatile int back_buffer_idx = 1;
static volatile bool swap_requested = false;

// Flag to indicate when it's safe to draw (during vblank)
static volatile bool in_vblank = false;

// Optimization: Lookup Table for fast conversion
static uint16_t rgb332_lut[256];

// Helper to pre-calculate LUT logic moved to vga_init

void vga_init() {
  // Initialize Lookup Table using official macro for correct format
  for (int i = 0; i < 256; i++) {
    // Extract RGB332 components
    uint8_t r3 = (i >> 5) & 0x7;
    uint8_t g3 = (i >> 2) & 0x7;
    uint8_t b2 = (i >> 0) & 0x3;

    // Scale to full 8-bit range (0-255)
    // 3 bits (0-7): val * 255 / 7
    // 2 bits (0-3): val * 255 / 3
    uint8_t r8 = (r3 * 255) / 7;
    uint8_t g8 = (g3 * 255) / 7;
    uint8_t b8 = (b2 * 255) / 3;

    rgb332_lut[i] = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(r8, g8, b8);
  }

  scanvideo_setup(&VGA_MODE);
  scanvideo_timing_enable(true);
  memset(canvas_buffers, 0, sizeof(canvas_buffers));
}

uint8_t *vga_get_back_buffer() { return canvas_buffers[back_buffer_idx]; }

void vga_set_vblank(bool vblank) {
  in_vblank = vblank;
  // If entering vblank and swap was requested, DO IT NOW
  if (vblank && swap_requested) {
    int temp = front_buffer_idx;
    front_buffer_idx = back_buffer_idx;
    back_buffer_idx = temp;
    swap_requested = false;
  }
}

bool vga_is_vblank() { return in_vblank; }

void vga_swap_buffers() {
  swap_requested = true;
  // logic handled in set_vblank (or could be checked in render loop start)
}

void vga_clear_canvas(uint8_t *canvas) {
  memset(canvas, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint8_t));
}

void vga_render_scanline(struct scanvideo_scanline_buffer *dest,
                         uint8_t *canvas_slice_ptr_unused) {
  (void)canvas_slice_ptr_unused; // Explicitly ignore

  uint16_t scanline_num = scanvideo_scanline_number(dest->scanline_id);

  // Safety check
  if (scanline_num >= CANVAS_HEIGHT) {
    scanline_num = 0;
  }

  uint8_t *row_pixels =
      &canvas_buffers[front_buffer_idx][scanline_num * CANVAS_WIDTH];
  uint16_t *color_buffer = prepare_scanline_buffer(dest, CANVAS_WIDTH);

  // Convert 8-bit pixels to 16-bit using LUT (super fast)
  for (size_t px = 0; px < CANVAS_WIDTH; px++) {
    color_buffer[px] = rgb332_lut[row_pixels[px]];
  }

  finalize_scanline_buffer(dest);
}

void vga_draw_rectangle_filled(uint8_t *canvas, const pong_rect *rect) {
  // Erase old
  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y_old + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;
    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x_old + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;

      if (canvas_x < rect->x || canvas_x >= rect->x + rect->w ||
          canvas_y < rect->y || canvas_y >= rect->y + rect->h) {
        canvas[canvas_y * CANVAS_WIDTH + canvas_x] = 0;
      }
    }
  }

  // Draw new
  // Note: rect->color coming from game logic is arguably 16-bit RGB565.
  // We need to convert it to 8-bit RGB332 if we want colors to match?
  // OR, we assume game logic now sends 8-bit color codes.
  // Let's assume game logic logic sends 0..255 colors here.
  uint8_t c = (uint8_t)rect->color; // Truncate if it was 16-bit

  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;
    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;
      canvas[canvas_y * CANVAS_WIDTH + canvas_x] = c;
    }
  }
}

void vga_draw_rectangle_border(uint8_t *canvas, size_t x, size_t y,
                               size_t width, size_t height, uint8_t color) {
  // Same logic, adjusted for uint8
  for (size_t col = 0; col < width; col++) {
    size_t top = y * CANVAS_WIDTH + (x + col);
    size_t bot = (y + height - 1) * CANVAS_WIDTH + (x + col);
    if (x + col < CANVAS_WIDTH) {
      if (y < CANVAS_HEIGHT)
        canvas[top] = color;
      if (y + height - 1 < CANVAS_HEIGHT)
        canvas[bot] = color;
    }
  }
  for (size_t row = 0; row < height; row++) {
    size_t left = (y + row) * CANVAS_WIDTH + x;
    size_t right = (y + row) * CANVAS_WIDTH + (x + width - 1);
    if (y + row < CANVAS_HEIGHT) {
      if (x < CANVAS_WIDTH)
        canvas[left] = color;
      if (x + width - 1 < CANVAS_WIDTH)
        canvas[right] = color;
    }
  }
}

void vga_draw_circle_filled(uint8_t *canvas, const pong_rect *rect) {
  long radius_sq = (long)rect->w * rect->w;
  uint8_t c = (uint8_t)rect->color;

  // Erase old
  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y_old + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;
    long dy = (long)(2 * row + 1) - rect->h;
    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x_old + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;
      long dx = (long)(2 * col + 1) - rect->w;
      if (dx * dx + dy * dy <= radius_sq) {
        canvas[canvas_y * CANVAS_WIDTH + canvas_x] = 0;
      }
    }
  }

  // Draw new
  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;
    long dy = (long)(2 * row + 1) - rect->h;
    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;
      long dx = (long)(2 * col + 1) - rect->w;
      if (dx * dx + dy * dy <= radius_sq) {
        canvas[canvas_y * CANVAS_WIDTH + canvas_x] = c;
      }
    }
  }
}

void vga_draw_text(uint8_t *canvas, int x, int y, const char *text,
                   uint8_t color) {
  // Not implemented
}

void vga_draw_number(uint8_t *canvas, int x, int y, int number, uint8_t color) {
  const int w = 10, h = 15, t = 2;
  bool sA = 0, sB = 0, sC = 0, sD = 0, sE = 0, sF = 0, sG = 0;

  switch (number) {
  case 0:
    sA = 1;
    sB = 1;
    sC = 1;
    sD = 1;
    sE = 1;
    sF = 1;
    break;
  case 1:
    sB = 1;
    sC = 1;
    break;
  case 2:
    sA = 1;
    sB = 1;
    sD = 1;
    sE = 1;
    sG = 1;
    break;
  case 3:
    sA = 1;
    sB = 1;
    sC = 1;
    sD = 1;
    sG = 1;
    break;
  case 4:
    sB = 1;
    sC = 1;
    sF = 1;
    sG = 1;
    break;
  case 5:
    sA = 1;
    sC = 1;
    sD = 1;
    sF = 1;
    sG = 1;
    break;
  case 6:
    sA = 1;
    sC = 1;
    sD = 1;
    sE = 1;
    sF = 1;
    sG = 1;
    break;
  case 7:
    sA = 1;
    sB = 1;
    sC = 1;
    break;
  case 8:
    sA = 1;
    sB = 1;
    sC = 1;
    sD = 1;
    sE = 1;
    sF = 1;
    sG = 1;
    break;
  case 9:
    sA = 1;
    sB = 1;
    sC = 1;
    sD = 1;
    sF = 1;
    sG = 1;
    break;
  }

#define DR(rx, ry, rw, rh)                                                     \
  for (int r = 0; r < (rh); r++)                                               \
    for (int c = 0; c < (rw); c++)                                             \
      if ((ry) + r < CANVAS_HEIGHT && (rx) + c < CANVAS_WIDTH)                 \
        canvas[((ry) + r) * CANVAS_WIDTH + ((rx) + c)] = color;

  if (sA)
    DR(x, y, w, t);
  if (sB)
    DR(x + w - t, y, t, h / 2);
  if (sC)
    DR(x + w - t, y + h / 2, t, h / 2);
  if (sD)
    DR(x, y + h - t, w, t);
  if (sE)
    DR(x, y + h / 2, t, h / 2);
  if (sF)
    DR(x, y, t, h / 2);
  if (sG)
    DR(x, y + h / 2 - t / 2, w, t);
#undef DR
}

// Helper Functions
static inline uint16_t *
prepare_scanline_buffer(struct scanvideo_scanline_buffer *dest, uint width) {
  assert(width >= 3 && width % 2 == 0);
  dest->data[0] = COMPOSABLE_RAW_RUN | ((width + 1 - 3) << 16);
  dest->data[width / 2 + 2] = 0x0000u | (COMPOSABLE_EOL_ALIGN << 16);
  dest->data_used = width / 2 + 2;
  return (uint16_t *)&dest->data[1];
}

static inline void
finalize_scanline_buffer(struct scanvideo_scanline_buffer *dest) {
  uint32_t first = dest->data[0];
  uint32_t second = dest->data[1];
  dest->data[0] = (first & 0x0000ffffu) | ((second & 0x0000ffffu) << 16);
  dest->data[1] = (second & 0xffff0000u) | ((first & 0xffff0000u) >> 16);
  dest->status = SCANLINE_OK;
}

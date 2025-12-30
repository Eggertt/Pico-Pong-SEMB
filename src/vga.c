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

// Single canvas buffer
static uint16_t canvas[320 * 240] = {0};

// Flag to indicate when it's safe to draw (during vblank)
static volatile bool in_vblank = false;

void vga_init() {
  scanvideo_setup(&VGA_MODE);
  scanvideo_timing_enable(true);
}

uint16_t *vga_get_canvas() { return canvas; }

// Returns the same canvas (no double buffering due to RAM constraints)
uint16_t *vga_get_display_canvas() { return canvas; }

// Set vblank flag
void vga_set_vblank(bool vblank) { in_vblank = vblank; }

// Check if in vblank
bool vga_is_vblank() { return in_vblank; }

// Empty swap function (for compatibility)
void vga_swap_buffers() {
  // No-op with single buffer
}

uint16_t *vga_get_next_canvas_slice(uint16_t *canvas) {
  static size_t current_row_index = 0;

  // Update logic: Cycle through rows
  current_row_index++;
  if (current_row_index >= CANVAS_HEIGHT) {
    current_row_index = 0;
  }

  return &canvas[current_row_index * CANVAS_WIDTH];
}

void vga_clear_canvas(uint16_t *canvas) {
  memset(canvas, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t));
}

void vga_render_scanline(struct scanvideo_scanline_buffer *dest,
                         uint16_t *canvas_slice) {
  uint16_t *color_buffer = prepare_scanline_buffer(dest, CANVAS_WIDTH);

  // Copy the row from the canvas to the color buffer
  for (size_t px = 0; px < CANVAS_WIDTH; px++) {
    color_buffer[px] = canvas_slice[px];
  }

  finalize_scanline_buffer(dest);
}

void vga_draw_rectangle_filled(uint16_t *canvas, const pong_rect *rect) {
  // Erase old rectangle by setting its pixels to 0
  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y_old + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;

    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x_old + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;

      // Clear pixel if it is outside the new rectangle
      if (canvas_x < rect->x || canvas_x >= rect->x + rect->w ||
          canvas_y < rect->y || canvas_y >= rect->y + rect->h) {
        size_t index = canvas_y * CANVAS_WIDTH + canvas_x;
        canvas[index] = 0; // Clear pixel
      }
    }
  }

  // Draw new rectangle
  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;

    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;

      size_t index = canvas_y * CANVAS_WIDTH + canvas_x;
      canvas[index] = rect->color; // Set pixel to the rectangle's color
    }
  }
}
// Draw a rectangle with borders only
void vga_draw_rectangle_border(uint16_t *canvas, size_t x, size_t y,
                               size_t width, size_t height, uint16_t color) {
  // Top and bottom borders
  for (size_t col = 0; col < width; col++) {
    size_t top_index = y * CANVAS_WIDTH + (x + col);
    size_t bottom_index = (y + height - 1) * CANVAS_WIDTH + (x + col);

    if (x + col < CANVAS_WIDTH) {
      if (y < CANVAS_HEIGHT)
        canvas[top_index] = color; // Top border
      if (y + height - 1 < CANVAS_HEIGHT)
        canvas[bottom_index] = color; // Bottom border
    }
  }

  // Left and right borders
  for (size_t row = 0; row < height; row++) {
    size_t left_index = (y + row) * CANVAS_WIDTH + x;
    size_t right_index = (y + row) * CANVAS_WIDTH + (x + width - 1);

    if (y + row < CANVAS_HEIGHT) {
      if (x < CANVAS_WIDTH)
        canvas[left_index] = color; // Left border
      if (x + width - 1 < CANVAS_WIDTH)
        canvas[right_index] = color; // Right border
    }
  }
}

void vga_draw_circle_filled(uint16_t *canvas, const pong_rect *rect) {
  // Use 2x coordinates to handle even width/height symmetry properly (center at
  // 0.5) Radius in 2x scale is simply rect->w (assuming w==h) Center in 2x
  // scale relative to rect->x is rect->w

  long radius_sq = (long)rect->w * rect->w;

  // Erase old circle (Unconditional erase to fix trails)
  for (size_t row = 0; row < rect->h; row++) {
    size_t canvas_y = rect->y_old + row;
    if (canvas_y >= CANVAS_HEIGHT)
      break;

    long dy =
        (long)(2 * row + 1) - rect->h; // Distance from center in 2x coords

    for (size_t col = 0; col < rect->w; col++) {
      size_t canvas_x = rect->x_old + col;
      if (canvas_x >= CANVAS_WIDTH)
        break;

      long dx = (long)(2 * col + 1) - rect->w;

      if (dx * dx + dy * dy <= radius_sq) {
        size_t index = canvas_y * CANVAS_WIDTH + canvas_x;
        canvas[index] = 0; // Clear pixel (Black)
      }
    }
  }

  // Draw new circle
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
        size_t index = canvas_y * CANVAS_WIDTH + canvas_x;
        canvas[index] = rect->color;
      }
    }
  }
}

void vga_draw_text(uint16_t *canvas, int x, int y, const char *text,
                   uint16_t color) {
  // Not implemented
  (void)canvas;
  (void)x;
  (void)y;
  (void)text;
  (void)color;
}

void vga_draw_number(uint16_t *canvas, int x, int y, int number,
                     uint16_t color) {
  // 7-segment style rendering
  // Size of segments
  const int w = 10; // Width of the digit
  const int h = 15; // Height of the digit
  const int t = 2;  // Thickness of segments

  // Coordinates for segments
  // A: Top horiz
  // B: Top-right vert
  // C: Bottom-right vert
  // D: Bottom horiz
  // E: Bottom-left vert
  // F: Top-left vert
  // G: Middle horiz

  bool segA = false, segB = false, segC = false, segD = false, segE = false,
       segF = false, segG = false;

  switch (number) {
  case 0:
    segA = 1;
    segB = 1;
    segC = 1;
    segD = 1;
    segE = 1;
    segF = 1;
    break;
  case 1:
    segB = 1;
    segC = 1;
    break;
  case 2:
    segA = 1;
    segB = 1;
    segD = 1;
    segE = 1;
    segG = 1;
    break;
  case 3:
    segA = 1;
    segB = 1;
    segC = 1;
    segD = 1;
    segG = 1;
    break;
  case 4:
    segB = 1;
    segC = 1;
    segF = 1;
    segG = 1;
    break;
  case 5:
    segA = 1;
    segC = 1;
    segD = 1;
    segF = 1;
    segG = 1;
    break;
  case 6:
    segA = 1;
    segC = 1;
    segD = 1;
    segE = 1;
    segF = 1;
    segG = 1;
    break;
  case 7:
    segA = 1;
    segB = 1;
    segC = 1;
    break;
  case 8:
    segA = 1;
    segB = 1;
    segC = 1;
    segD = 1;
    segE = 1;
    segF = 1;
    segG = 1;
    break;
  case 9:
    segA = 1;
    segB = 1;
    segC = 1;
    segD = 1;
    segF = 1;
    segG = 1;
    break;
  }

// Helper to draw segment as rect without erasing (since we clear manually or
// dont move numbers often) Actually, vga_draw_rectangle_filled erases based on
// x_old. Since numbers are static score counters, we can just assume we want to
// DRAW them. But wait, the score updates. We should probably use a dedicated
// function that doesn't rely on 'object state' with old pos. Or just manually
// draw rects here.

// Simple rect draw helper for this function
#define DRAW_RECT(rx, ry, rw, rh)                                              \
  for (int r = 0; r < (rh); r++) {                                             \
    for (int c = 0; c < (rw); c++) {                                           \
      if ((ry) + r < CANVAS_HEIGHT && (rx) + c < CANVAS_WIDTH)                 \
        canvas[((ry) + r) * CANVAS_WIDTH + ((rx) + c)] = color;                \
    }                                                                          \
  }

  // A: Top
  if (segA)
    DRAW_RECT(x, y, w, t);
  // B: Top-Right
  if (segB)
    DRAW_RECT(x + w - t, y, t, h / 2);
  // C: Bottom-Right
  if (segC)
    DRAW_RECT(x + w - t, y + h / 2, t, h / 2);
  // D: Bottom
  if (segD)
    DRAW_RECT(x, y + h - t, w, t);
  // E: Bottom-Left
  if (segE)
    DRAW_RECT(x, y + h / 2, t, h / 2);
  // F: Top-Left
  if (segF)
    DRAW_RECT(x, y, t, h / 2);
  // G: Middle
  if (segG)
    DRAW_RECT(x, y + h / 2 - t / 2, w, t);

#undef DRAW_RECT
}

// Helper Functions
static inline uint16_t *
prepare_scanline_buffer(struct scanvideo_scanline_buffer *dest, uint width) {
  assert(width >= 3 && width % 2 == 0);

  // Prepare composable scanline header
  dest->data[0] = COMPOSABLE_RAW_RUN | ((width + 1 - 3) << 16);
  dest->data[width / 2 + 2] = 0x0000u | (COMPOSABLE_EOL_ALIGN << 16);
  dest->data_used = width / 2 + 2;

  assert(dest->data_used <= dest->data_max);
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

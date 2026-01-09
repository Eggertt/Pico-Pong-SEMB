#ifndef _VGA_H_
#define _VGA_H_

#include <pico/scanvideo.h>
#include <pico/scanvideo/composable_scanline.h>
#include <pico/scanvideo/scanvideo_base.h>

#include "game.h"

#define VGA_MODE vga_mode_320x240_60
#define CANVAS_WIDTH VGA_MODE.width
#define CANVAS_HEIGHT VGA_MODE.height
#define CANVAS_SIZE (CANVAS_WIDTH * CANVAS_HEIGHT)

extern uint8_t *vga_get_back_buffer(void);
// uint16_t *vga_get_display_canvas(void); // Not used directly anymore
void vga_swap_buffers(void);

void vga_set_vblank(bool vblank);
bool vga_is_vblank(void);

void vga_init(void);

// Canvas is now uint8_t (8-bit color)
void vga_clear_canvas(uint8_t *canvas);

void vga_render_scanline(struct scanvideo_scanline_buffer *dest,
                         uint8_t *canvas_slice);

void vga_draw_rectangle_filled(uint8_t *canvas, const pong_rect *rect);

void vga_draw_rectangle_border(uint8_t *canvas, size_t x, size_t y,
                               size_t width, size_t height, uint8_t color);

void vga_draw_circle_filled(uint8_t *canvas, const pong_rect *rect);
void vga_draw_text(uint8_t *canvas, int x, int y, const char *text,
                   uint8_t color);
void vga_draw_number(uint8_t *canvas, int x, int y, int number, uint8_t color);
#endif // _VGA_H_

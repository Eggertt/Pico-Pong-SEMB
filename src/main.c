// C includes
#include <stdio.h>

// Pico SDK
#include <pico/multicore.h>
#include <pico/stdlib.h>

// FreeRTOS
#include <FreeRTOS.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>
#include <timers.h>

// Project specific
#include "game.h"
#include "vga.h"

#define mainGAME_LOGIC_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define mainGAME_DRAW_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

// Button definitions (Reusing VGA pins)
// GP0: Originally Red LSB. Now Button UP.
// GP6: Originally Green Bit 1. Now Button DOWN.
// GP11: Originally Blue Bit 1. Now Button RESET (Optional).
#define PIN_BUTTON_UP 0
#define PIN_BUTTON_DOWN 6
#define PIN_BUTTON_COLOR_TOGGLE 11

static void prvSetupHardware(void);
static void prvLaunchRTOS();

static struct mutex render_sync_mutex;
static struct mutex game_state_mutex;

void render_loop() {
  while (true) {
    // Begin scanline generation
    struct scanvideo_scanline_buffer *scanline_buffer =
        scanvideo_begin_scanline_generation(true);

    // Get the current scanline index from VGA hardware
    uint16_t scanline_id =
        scanvideo_scanline_number(scanline_buffer->scanline_id);

    // Signal vblank at end of frame (scanline 0 means new frame started)
    if (scanline_id == 0) {
      vga_set_vblank(true);
    } else if (scanline_id == 10) {
      vga_set_vblank(false);
    }

    // Read directly from canvas
    uint16_t *canvas = vga_get_canvas();
    uint16_t *current_slice = &canvas[scanline_id * CANVAS_WIDTH];
    vga_render_scanline(scanline_buffer, current_slice);

    // End scanline generation
    scanvideo_end_scanline_generation(scanline_buffer);
  }
}

void update_canvas(struct game_state *gs) {
  uint16_t white_color =
      (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0xff, 0xff, 0xff);
  uint16_t black_color = 0;

  // Determine colors based on mode
  uint16_t ball_color;
  uint16_t player_color;
  uint16_t ai_color;

  switch (gs->color_mode) {
  case 1: // Color Mode 1 (Blue/Red/Green)
    // Ball: Lighter Blue
    ball_color = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x66, 0xcc, 0xff);
    // Player: Red
    player_color = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0xff, 0x00, 0x00);
    // AI: Green
    ai_color = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x00, 0xff, 0x00);
    break;

  case 2: // Color Mode 2 (Red/Yellow/Blue)
    // Ball: White for high contrast
    ball_color = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0xff, 0x66, 0xb2);
    // Player: Yellow/Gold
    player_color = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0xff, 0xd7, 0x00);
    // AI: Blue
    ai_color = (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x66, 0xcc, 0xff);
    break;

  default: // Default (0): White
    ball_color = white_color;
    player_color = white_color;
    ai_color = white_color;
    break;
  }

  // Update object colors
  gs->ball.color = ball_color;
  gs->player.color = player_color;
  gs->ai.color = ai_color;

  // Wait for VBlank to avoid tearing
  while (!vga_is_vblank()) {
    // Busy wait for vblank
  }

  uint16_t *canvas = vga_get_canvas();

  // Draw Dashed Line
  {
    struct pong_rect dash = {.x = CANVAS_WIDTH / 2 - 1,
                             .y = 0,
                             .w = 2,
                             .h = 10,
                             .color = white_color,
                             .x_old = CANVAS_WIDTH / 2 - 1,
                             .y_old = 0};

    // Draw dashed line
    for (int i = 0; i < CANVAS_HEIGHT; i += 20) {
      dash.y = i;
      for (size_t r = 0; r < dash.h; r++) {
        if (dash.y + r >= CANVAS_HEIGHT)
          break;
        for (size_t c = 0; c < dash.w; c++) {
          canvas[(dash.y + r) * CANVAS_WIDTH + (dash.x + c)] = dash.color;
        }
      }
    }
  }

  // Draw Player and AI paddles
  mutex_enter_blocking(&game_state_mutex);
  vga_draw_rectangle_filled(canvas, &gs->player);
  vga_draw_rectangle_filled(canvas, &gs->ai);
  mutex_exit(&game_state_mutex);

  // Draw Ball (Round)
  mutex_enter_blocking(&game_state_mutex);
  vga_draw_circle_filled(canvas, &gs->ball);
  mutex_exit(&game_state_mutex);

  // Draw Scores
  mutex_enter_blocking(&game_state_mutex);

  int score_y = 20;
  int p1_x = CANVAS_WIDTH / 2 - 40;
  int ai_x = CANVAS_WIDTH / 2 + 30;

  // Clear previous score areas (Simple black box)
  struct pong_rect clear_box = {
      .x = p1_x, .y = score_y, .w = 14, .h = 19, .color = black_color};
  for (int r = 0; r < clear_box.h; r++) {
    for (int c = 0; c < clear_box.w; c++) {
      if ((clear_box.y + r) < CANVAS_HEIGHT) {
        canvas[(clear_box.y + r) * CANVAS_WIDTH + (clear_box.x + c)] = 0;
        canvas[(clear_box.y + r) * CANVAS_WIDTH + (ai_x + c)] = 0;
      }
    }
  }

  vga_draw_number(canvas, p1_x, score_y, gs->player_score, white_color);
  vga_draw_number(canvas, ai_x, score_y, gs->ai_score, white_color);

  // Handle Reset Score visual
  if (gs->reset_score) {
    vga_clear_canvas(canvas);
    gs->reset_score = false;
  }

  mutex_exit(&game_state_mutex);
}

static void prvGameLogicTask(void *pvParameters) {
  struct game_state *gs = pvParameters;

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(33);

  for (;;) {
    int move_direction = 0; // Default: no movement

    // Poll buttons
    if (gpio_get(PIN_BUTTON_UP)) {
      move_direction = -1; // Up
      // printf("UP Pressed\n");
    } else if (gpio_get(PIN_BUTTON_DOWN)) {
      move_direction = 1; // Down
      // printf("DOWN Pressed\n");
    }

    // Poll color toggle button (Switch C)
    static bool last_toggle_state = false;
    bool current_toggle_state = gpio_get(PIN_BUTTON_COLOR_TOGGLE);
    if (current_toggle_state && !last_toggle_state) {
      // Button pressed (rising edge)
      mutex_enter_blocking(&game_state_mutex);
      // cycle 0 (White) -> 1 (Color) -> 2 (Color Blind) -> 0
      gs->color_mode = (gs->color_mode + 1) % 3;
      mutex_exit(&game_state_mutex);
    }
    last_toggle_state = current_toggle_state;

    mutex_enter_blocking(&game_state_mutex);
    {
      gs_update_player(gs, move_direction);
      gs_update_ai(gs);
      gs_update_ball(gs);

      if (gs->player_score == 6 || gs->ai_score == 6) {
        gs->reset_score = true;
        gs->player_score = 0;
        gs->ai_score = 0;
      }
    }
    mutex_exit(&game_state_mutex);

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

static void prvGameDrawCanvasTask(void *pvParameters) {
  struct game_state *gs = pvParameters; // Not used by task

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(25);

  for (;;) {
    update_canvas(gs);
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

int main(void) {
  // Initialize VGA first (sets up all video pins)
  vga_init();

  // Initialize hardware and override specific VGA pins for buttons
  prvSetupHardware();

  mutex_init(&game_state_mutex);
  mutex_init(&render_sync_mutex);

  multicore_launch_core1(render_loop);

  uint16_t ball_color =
      (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x42, 0xba, 0xff);

  uint16_t player_color =
      (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0xAC, 0x11, 0x22);

  uint16_t AI_color =
      (uint16_t)PICO_SCANVIDEO_PIXEL_FROM_RGB5(0xDC, 0x01, 0x29);

  uint16_t bg_color_1 = 0;

  struct pong_rect ball = {
      .x = 20,
      .y = 20,
      .w = 10,
      .h = 10,
      .color = ball_color,
      .v_x = 2,
      .v_y = 2,
  };

  struct pong_rect player = {
      .x = 20,
      .x_old = 20,
      .y = 100,
      .y_old = 100,
      .w = 5,
      .h = 50,
      .color = player_color,
      .v_x = 20,
      .v_y = 5,
  };

  struct pong_rect AI = {
      .x = CANVAS_WIDTH - 25,
      .x_old = CANVAS_WIDTH - 25,
      .y = 100,
      .y_old = 100,
      .w = 5,
      .h = 50,
      .color = AI_color,
      .v_x = 2,
      .v_y = 2,
  };

  struct game_state gs = {
      .bg_color = bg_color_1,
      .padding_x = 4,
      .padding_y = 10,
      .ball = ball,
      .player = player,
      .ai = AI,
      .player_score = 0,
      .ai_score = 0,
      .canvas_w = CANVAS_WIDTH,
      .canvas_h = CANVAS_HEIGHT,
      .color_mode = 0, // Default to White
  };

  xTaskCreate(prvGameLogicTask, "GameLogic", configMINIMAL_STACK_SIZE, &gs,
              mainGAME_LOGIC_TASK_PRIORITY, NULL);

  xTaskCreate(prvGameDrawCanvasTask, "GameDraw", configMINIMAL_STACK_SIZE, &gs,
              mainGAME_DRAW_TASK_PRIORITY, NULL);

  prvLaunchRTOS();
}

static void prvSetupHardware(void) {
  /* Want to be able to printf */
  stdio_usb_init();

  // Initialize buttons (reclaiming VGA pins)
  // By initializing these AFTER vga_init() (called in main), we switch them
  // from PIO function back to SIO (GPIO) function, effectively disconnecting
  // them from the VGA output.

  gpio_init(PIN_BUTTON_UP);
  gpio_set_dir(PIN_BUTTON_UP, GPIO_IN);
  gpio_pull_up(PIN_BUTTON_UP);

  gpio_init(PIN_BUTTON_DOWN);
  gpio_set_dir(PIN_BUTTON_DOWN, GPIO_IN);
  gpio_pull_up(PIN_BUTTON_DOWN);

  // Initialize Switch C for color toggle
  gpio_init(PIN_BUTTON_COLOR_TOGGLE);
  gpio_set_dir(PIN_BUTTON_COLOR_TOGGLE, GPIO_IN);
  gpio_pull_up(PIN_BUTTON_COLOR_TOGGLE);
}

static void prvLaunchRTOS() {
  vTaskStartScheduler();
  /* should never reach here */
  panic_unsupported();
}

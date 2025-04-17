#include <pebble.h>
#include <stdlib.h>
#include <time.h>

// Clock modules
#include "clock_beat.h"
#include "clock_closest_noon.h"
#include "clock_tid.h"
#include "clock_decimal.h"

// --- Window and Layer Globals ---
static Window *s_main_window;
static TextLayer *s_beat_layer;
static TextLayer *s_tid_layer;
static TextLayer *s_closest_noon_layer;
static TextLayer *s_decimal_layer;

// --- Pebble Window Management ---

// Handles updates from the TickTimerService
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds);

  // Update both time displays
  clock_beat_update(s_beat_layer, seconds);
  clock_tid_update(s_tid_layer, seconds, milliseconds);
  clock_closest_noon_update(s_closest_noon_layer, seconds);
  clock_decimal_update(s_decimal_layer, seconds);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int16_t total_h = bounds.size.h;
  const int16_t layer_h = total_h / 4;

  // Create layers
  s_beat_layer = clock_beat_init(GRect(0, 0, bounds.size.w, layer_h), window_layer);
  s_decimal_layer = clock_decimal_init(GRect(0, layer_h, bounds.size.w, layer_h), window_layer);
  s_closest_noon_layer = clock_closest_noon_init(GRect(0, 2 * layer_h, bounds.size.w, layer_h), window_layer);
  s_tid_layer = clock_tid_init(GRect(0, 3 * layer_h, bounds.size.w, layer_h), window_layer);
}

static void main_window_unload(Window *window) {
  // Destroy TextLayers
  clock_beat_deinit(s_beat_layer);
  clock_closest_noon_deinit(s_closest_noon_layer);
  clock_tid_deinit(s_tid_layer);
  clock_decimal_deinit(s_decimal_layer);
}

static void init() {
  srand(time(NULL));

  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(
      s_main_window,
      (WindowHandlers){.load = main_window_load, .unload = main_window_unload});

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  // Get initial time and update display immediately
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds); // Call only once
  // Initial updates can be skipped if layers show placeholders,
  // tick_handler will update them shortly after load.

  // Register with TickTimerService to update every second
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void deinit() {
    window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

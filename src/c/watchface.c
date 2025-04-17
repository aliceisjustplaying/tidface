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
static TextLayer *s_closest_noon_city_layer;
static TextLayer *s_closest_noon_time_layer;
static TextLayer *s_tid_layer;
static TextLayer *s_beat_layer;

// --- Pebble Window Management ---

// Handles updates from the TickTimerService
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds);
  // Hero: Closest Noon (update city and time layers)
  clock_closest_noon_update(s_closest_noon_city_layer, s_closest_noon_time_layer, seconds);
  // Footer: TID (larger) and Beat (smaller)
  clock_tid_update(s_tid_layer, seconds, milliseconds);
  clock_beat_update(s_beat_layer, seconds);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  // Hero area for Closest Noon: split into city + large time
  int footer_h = 48;
  int hero_h = bounds.size.h - footer_h;
  const int city_h = 24;
  const int usable_h = hero_h - city_h;
  const int time_font_h = 42;
  // Create city name line
  s_closest_noon_city_layer = clock_closest_noon_city_init(
      GRect(0, 0, bounds.size.w, city_h), window_layer);
  text_layer_set_text_alignment(s_closest_noon_city_layer, GTextAlignmentCenter);
  // Create hero time line, vertically centered
  int time_y = city_h + (usable_h - time_font_h) / 2;
  s_closest_noon_time_layer = clock_closest_noon_time_init(
      GRect(0, time_y, bounds.size.w, time_font_h), window_layer);

  // Setup footer area
  int w = bounds.size.w;
  int h = bounds.size.h;
  int footer_y = h - footer_h;
  // Two line heights to fit fonts without clipping
  int tid_h = 26;   // tall enough for 18px font + padding
  int beat_h = footer_h - tid_h;

  // Footer line 1: TID (bigger, center aligned)
  s_tid_layer = clock_tid_init(GRect(0, footer_y, w, tid_h), window_layer);
  text_layer_set_font(s_tid_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_tid_layer, GTextAlignmentCenter);

  // Footer line 2: Beat (smaller, center aligned)
  s_beat_layer = clock_beat_init(GRect(0, footer_y + tid_h, w, beat_h), window_layer);
  text_layer_set_font(s_beat_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_beat_layer, GTextAlignmentCenter);
}

static void main_window_unload(Window *window) {
  // Destroy Closest Noon layers
  clock_closest_noon_deinit(s_closest_noon_city_layer);
  clock_closest_noon_deinit(s_closest_noon_time_layer);
  clock_tid_deinit(s_tid_layer);
  clock_beat_deinit(s_beat_layer);
}

static void init() {
  srand(time(NULL));

  // Create main Window element and assign to pointer
  s_main_window = window_create();
  // Ensure text layers with clear background show up on white
  window_set_background_color(s_main_window, GColorWhite);

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

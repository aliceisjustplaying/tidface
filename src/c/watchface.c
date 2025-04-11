#include <pebble.h>
#include <stdlib.h>
#include <time.h> // Required for time_t, rand(), srand()

// --- Clock Module Includes ---
#include "clock_beat.h"
#include "clock_noonzone.h"
#include "clock_closest_noon.h"
#include "clock_tid.h"

// --- Window and Layer Globals ---
static Window *s_main_window;
static TextLayer *s_beat_layer; // Layer for Swatch Beat Time
static TextLayer *s_tid_layer;  // Layer for TID Time
static TextLayer *s_noonzone_layer; // Layer for Noon Zone Time
static TextLayer *s_closest_noon_layer; // Layer for Closest-to-Noon TZ

// --- Pebble Window Management ---

// Handles updates from the TickTimerService
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Get time once for both updates
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds);

  // Update both time displays
  clock_beat_update(s_beat_layer, seconds);
  clock_tid_update(s_tid_layer, seconds, milliseconds);
  clock_noonzone_update(s_noonzone_layer, seconds);
  clock_closest_noon_update(s_closest_noon_layer, seconds);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Define layout constants (adjust heights based on font sizes)
  const int16_t v_padding = 1; // Tighten padding for 4 layers
  // Allocate heights roughly - adjust based on visual results
  const int16_t beat_h = 28;
  const int16_t noonzone_h = 26;
  const int16_t closest_h = 26;
  const int16_t tid_h = 26; 
  // Calculate total height needed (excluding top/bottom margins provided by layer positioning)
  const int16_t total_inner_h = beat_h + noonzone_h + closest_h + tid_h + 3 * v_padding;
  // Distribute remaining vertical space as top/bottom margin
  const int16_t top_margin = (bounds.size.h - total_inner_h) / 2;

  // Font sizes (adjust as needed)
  #define BEAT_FONT FONT_KEY_GOTHIC_24_BOLD
  #define NOONZONE_FONT FONT_KEY_GOTHIC_18_BOLD // Smaller for longer name
  #define CLOSEST_FONT FONT_KEY_GOTHIC_18_BOLD // Smaller for city name
  #define TID_FONT FONT_KEY_GOTHIC_18_BOLD    // Smaller TID

  // Calculate Y positions
  int16_t current_y = top_margin;
  int16_t beat_y = current_y;
  current_y += beat_h + v_padding;
  int16_t noonzone_y = current_y;
  current_y += noonzone_h + v_padding;
  int16_t closest_y = current_y;
  current_y += closest_h + v_padding;
  int16_t tid_y = current_y;

  // Create Beat Time TextLayer (Top)
  s_beat_layer = clock_beat_init(GRect(0, beat_y, bounds.size.w, beat_h), window_layer);

  // Create Noon Zone Time TextLayer (Middle)
  s_noonzone_layer = clock_noonzone_init(GRect(0, noonzone_y, bounds.size.w, noonzone_h), window_layer);

  // Create Closest Noon Time TextLayer (Middle-Bottom)
  s_closest_noon_layer = clock_closest_noon_init(GRect(0, closest_y, bounds.size.w, closest_h), window_layer);

  // Create TID TextLayer (Bottom)
  s_tid_layer = clock_tid_init(GRect(0, tid_y, bounds.size.w, tid_h), window_layer);
}

static void main_window_unload(Window *window) {
  // Destroy TextLayers
  clock_beat_deinit(s_beat_layer);
  clock_noonzone_deinit(s_noonzone_layer);
  clock_closest_noon_deinit(s_closest_noon_layer);
  clock_tid_deinit(s_tid_layer);
}

static void init() {
  // Seed random number generator (used by closest noon clock)
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
    // Destroy Window
    window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

#include <pebble.h>
#include <stdlib.h>
#include <time.h>

// Enable logging - Removed incorrect define, APP_LOG is in pebble.h

// --- Clock Module Includes ---
#include "clock_beat.h"
// #include "clock_noonzone.h" // Removed
#include "clock_closest_noon.h"
#include "clock_tid.h"
#include "clock_decimal.h"

// --- Window and Layer Globals ---
static Window *s_main_window;
static TextLayer *s_beat_layer; // Layer for Swatch Beat Time
static TextLayer *s_tid_layer;  // Layer for TID Time
// static TextLayer *s_noonzone_layer; // Layer for Noon Zone Time // Removed
static TextLayer *s_closest_noon_layer; // Layer for Closest-to-Noon TZ
static TextLayer *s_decimal_layer; // Layer for Decimal Time

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
  // clock_noonzone_update(s_noonzone_layer, seconds); // Removed
  clock_closest_noon_update(s_closest_noon_layer, seconds);
  clock_decimal_update(s_decimal_layer, seconds);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Bounds: H=%d, W=%d", bounds.size.h, bounds.size.w);

  // Define layout constants for 4 layers
  const int num_layers = 4;
  const int16_t v_padding = 2; // Slightly more padding maybe
  // Calculate available height for layers after removing padding
  const int16_t total_available_h = bounds.size.h - (num_layers - 1) * v_padding;
  // Distribute height equally among layers
  const int16_t layer_h = total_available_h / num_layers; 
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Total Available H: %d, Layer H: %d", total_available_h, layer_h);
  // Verify calculation result
  if (layer_h <= 0) { 
      APP_LOG(APP_LOG_LEVEL_ERROR, "Error: Calculated Layer Height <= 0");
      // Handle error or default to a minimal height
      // layer_h = 10; // Example fallback
  }
  
  // Font sizes (adjust as needed - removed noonzone)
  #define BEAT_FONT FONT_KEY_GOTHIC_24_BOLD
  // #define NOONZONE_FONT FONT_KEY_GOTHIC_18_BOLD // Removed
  #define CLOSEST_FONT FONT_KEY_GOTHIC_18_BOLD
  #define TID_FONT FONT_KEY_GOTHIC_18_BOLD
  #define DECIMAL_FONT FONT_KEY_GOTHIC_18_BOLD

  // Calculate Y positions based on new layout
  // Beat: Top
  int16_t beat_y = 0;

  // TID: Bottom
  int16_t tid_y = bounds.size.h - layer_h;

  // Closest Noon: Above TID
  int16_t closest_y = tid_y - layer_h - v_padding;

  // Decimal: Halfway between bottom of Beat and top of Closest Noon
  // Bottom of Beat = beat_y + layer_h = layer_h
  // Top of Closest Noon = closest_y
  // Midpoint = (bottom_of_beat + top_of_closest_noon) / 2
  // Adjust for decimal layer height: Midpoint - layer_h / 2
  int16_t decimal_midpoint = (layer_h + closest_y) / 2;
  int16_t decimal_y = decimal_midpoint - (layer_h / 2);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Y Positions: Beat=%d, Decimal=%d, Closest=%d, TID=%d", beat_y, decimal_y, closest_y, tid_y);

  // Create Layers in new order
  // Beat Time TextLayer (Top)
  s_beat_layer = clock_beat_init(GRect(0, beat_y, bounds.size.w, layer_h), window_layer);

  // Decimal Time TextLayer (Middle)
  s_decimal_layer = clock_decimal_init(GRect(0, decimal_y, bounds.size.w, layer_h), window_layer);

  // Closest Noon Time TextLayer (Middle-Bottom)
  s_closest_noon_layer = clock_closest_noon_init(GRect(0, closest_y, bounds.size.w, layer_h), window_layer);

  // TID TextLayer (Bottom)
  s_tid_layer = clock_tid_init(GRect(0, tid_y, bounds.size.w, layer_h), window_layer);

  // // Create Noon Zone Time TextLayer // Removed
  // s_noonzone_layer = clock_noonzone_init(GRect(0, noonzone_y, bounds.size.w, layer_h), window_layer);

}

static void main_window_unload(Window *window) {
  // Destroy TextLayers
  clock_beat_deinit(s_beat_layer);
  // clock_noonzone_deinit(s_noonzone_layer); // Removed
  clock_closest_noon_deinit(s_closest_noon_layer);
  clock_tid_deinit(s_tid_layer);
  clock_decimal_deinit(s_decimal_layer); 
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

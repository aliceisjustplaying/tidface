#include <pebble.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Logging: enable by defining ENABLE_APP_LOGS to 1
#ifndef ENABLE_APP_LOGS
#define ENABLE_APP_LOGS 0
#endif
#if !ENABLE_APP_LOGS
#undef APP_LOG
#define APP_LOG(level, fmt, ...)
#endif

// Clock modules
#include "clock_beat.h"
#include "clock_closest_airport_noon.h"
#include "clock_tid.h"

// --- Clock Modules & Settings ---
#define SETTINGS_KEY 1

typedef enum {
  MODE_NOON = 0,
  MODE_5PM = 1
} TargetTimeMode;

typedef enum {
  COLOR_LIGHT = 0,
  COLOR_DARK  = 1
} ColorScheme;

typedef struct AppSettings {
  TargetTimeMode target_time_mode;
  ColorScheme    color_scheme;
} AppSettings;

// Forward declare helper to apply colors across UI
static void apply_color_scheme();

static AppSettings settings;

// --- Window and Layer Globals ---
static Window *s_main_window;
static TextLayer *s_airport_noon_code_layer;
static TextLayer *s_airport_noon_name_layer;
static TextLayer *s_airport_noon_time_layer;
static TextLayer *s_tid_layer;
static TextLayer *s_beat_layer;

// --- Layout Constants ---
// These can be tweaked for different visual arrangements.
static const int LAYER_AIRPORT_CODE_HEIGHT = 28;
static const int LAYER_AIRPORT_NAME_HEIGHT = 28;
static const int LAYER_AIRPORT_TIME_HEIGHT = 42; // Approximate height for FONT_KEY_LECO_42_NUMBERS
static const int FOOTER_AREA_HEIGHT = 48;
static const int FOOTER_TID_HEIGHT = 28;

// Padding and Adjustments
static const int AIRPORT_NAME_X_PADDING = 3;
static const int AIRPORT_NAME_WIDTH_ADJUST = 5; // Total reduction (e.g., 2*padding + 1)
static const int AIRPORT_TIME_Y_ADJUST = -7; // Fine-tuning vertical position
static const int FOOTER_TID_Y_ADJUST = -1;
static const int FOOTER_BEAT_Y_ADJUST = -3;

// --- Pebble Window Management ---

// --- Settings Load/Save/Receive ---
static void load_settings() {
  // Set default values
  settings.target_time_mode = MODE_NOON;
  settings.color_scheme    = COLOR_LIGHT;
  // Read settings from persistent storage, if they exist
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void save_settings() {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_INFO, "Inbox received!");
  // Read timeAlignmentMode preference
  Tuple *target_time_mode_t = dict_find(iter, MESSAGE_KEY_timeAlignmentMode);
  if (target_time_mode_t) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Found key timeAlignmentMode with value %d", (int)target_time_mode_t->value->int32);
    // Convert received ASCII value ('0' or '1') to enum
    int received_value = (int)target_time_mode_t->value->int32;
    if (received_value == 49) { // ASCII for '1'
      settings.target_time_mode = MODE_5PM;
    } else { // Default to Noon for '0' (ASCII 48) or unexpected values
      settings.target_time_mode = MODE_NOON;
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "Setting mode to: %d", settings.target_time_mode);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Key timeAlignmentMode not found!");
  }

  // Read color scheme preference
  Tuple *color_scheme_t = dict_find(iter, MESSAGE_KEY_colorScheme);
  if (color_scheme_t) {
    int recv_val = (int)color_scheme_t->value->int32;
    settings.color_scheme = (recv_val == 49) ? COLOR_DARK : COLOR_LIGHT;
  }

  // Save the new settings
  save_settings();

  // Apply updated colors immediately
  apply_color_scheme();

  // Potentially force an update if needed
  s_last_re_eval_time = -1; // Force re-evaluation
}

// Handles updates from the TickTimerService
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds);

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Tick! Current mode: %d", settings.target_time_mode);
  // Determine target seconds based on setting
  long target_seconds = (settings.target_time_mode == MODE_5PM) ? (17 * 3600L) : (12 * 3600L);

  // Hero: Closest Noon (update city and time layers)
  clock_closest_airport_noon_update(s_airport_noon_code_layer, s_airport_noon_time_layer, seconds, target_seconds);

  // Update airport name below the code
  text_layer_set_text(s_airport_noon_name_layer, s_selected_name);
  // Footer: TID (larger) and Beat (smaller)
  clock_tid_update(s_tid_layer, seconds, milliseconds);
  clock_beat_update(s_beat_layer, seconds);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Calculated heights based on constants
  int hero_h = bounds.size.h - FOOTER_AREA_HEIGHT;
  const int usable_h = hero_h - LAYER_AIRPORT_CODE_HEIGHT - LAYER_AIRPORT_NAME_HEIGHT;

  // Create city name line (IATA Code)
  s_airport_noon_code_layer = clock_closest_airport_noon_code_init(
      GRect(0, 0, bounds.size.w, LAYER_AIRPORT_CODE_HEIGHT), window_layer);
  text_layer_set_text_alignment(s_airport_noon_code_layer, GTextAlignmentCenter);

  // Create airport name line below the IATA code
  s_airport_noon_name_layer = text_layer_util_create(GRect(
    AIRPORT_NAME_X_PADDING,
    LAYER_AIRPORT_CODE_HEIGHT, // Positioned below the code layer
    bounds.size.w - AIRPORT_NAME_WIDTH_ADJUST,
    LAYER_AIRPORT_NAME_HEIGHT),
    window_layer,
    "", // Initial text, will be updated by tick_handler
    FONT_KEY_GOTHIC_24);
  // text_layer_set_text_alignment is GTextAlignmentCenter by default in text_layer_util_create
  // text_layer_set_background_color is GColorClear by default in text_layer_util_create
  // layer_add_child is handled by text_layer_util_create

  // Create hero time line, vertically centered in its usable area
  int time_y = LAYER_AIRPORT_CODE_HEIGHT + LAYER_AIRPORT_NAME_HEIGHT + (usable_h - LAYER_AIRPORT_TIME_HEIGHT) / 2 + AIRPORT_TIME_Y_ADJUST;
  s_airport_noon_time_layer = clock_closest_airport_noon_time_init(
      GRect(0, time_y, bounds.size.w, LAYER_AIRPORT_TIME_HEIGHT), window_layer);

  // Setup footer area
  int w = bounds.size.w;
  int h = bounds.size.h;
  int footer_y = h - FOOTER_AREA_HEIGHT;
  int beat_h = FOOTER_AREA_HEIGHT - FOOTER_TID_HEIGHT; // Calculated

  // Footer line 1: TID (bigger, center aligned)
  s_tid_layer = clock_tid_init(GRect(0, footer_y + FOOTER_TID_Y_ADJUST, w, FOOTER_TID_HEIGHT), window_layer);
  text_layer_set_font(s_tid_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_tid_layer, GTextAlignmentCenter);

  // Footer line 2: Beat (smaller, center aligned)
  s_beat_layer = clock_beat_init(GRect(0, footer_y + FOOTER_TID_HEIGHT + FOOTER_BEAT_Y_ADJUST, w, beat_h), window_layer);
  text_layer_set_font(s_beat_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_beat_layer, GTextAlignmentCenter);

  // Apply color scheme to layers
  apply_color_scheme();
}

static void main_window_unload(Window *window) {
  (void)window;
  // Destroy Closest Noon layers
  clock_closest_airport_noon_deinit(s_airport_noon_code_layer);
  text_layer_destroy(s_airport_noon_name_layer);
  clock_closest_airport_noon_deinit(s_airport_noon_time_layer);
  clock_tid_deinit(s_tid_layer);
  clock_beat_deinit(s_beat_layer);
}

static void init() {
  {
    time_t seed_sec;
    uint16_t seed_ms;
    time_ms(&seed_sec, &seed_ms);
    srand((unsigned int)(seed_sec * 1000 + seed_ms));
  }
  // Load settings
  load_settings();

  // Create main Window element and assign to pointer
  s_main_window = window_create();
  // Set initial background color based on saved settings
  window_set_background_color(s_main_window, (settings.color_scheme == COLOR_DARK) ? GColorBlack : GColorWhite);

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(
      s_main_window,
      (WindowHandlers){.load = main_window_load, .unload = main_window_unload});

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  // Get initial time and update display immediately
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds); 
  // Perform initial update after loading settings
  tick_handler(NULL, SECOND_UNIT); // Pass NULL tick_time as it's not used by our handler logic

  // Register with TickTimerService to update every second
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);

  // Register AppMessage handlers
  app_message_register_inbox_received(inbox_received_handler);
  // Open AppMessage with default inbox size from Clay docs
  AppMessageResult result = app_message_open(128, 0); // 128 inbox, 0 outbox (adjust if needed)
  if (result == APP_MSG_OK) {
      APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage opened successfully!");
  } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to open AppMessage: %d", result);
  }
}

static void deinit() {
  tick_timer_service_unsubscribe();
  window_destroy(s_main_window);
}

// --- Helper: Apply Color Scheme ---
static void apply_color_scheme() {
  GColor bg = (settings.color_scheme == COLOR_DARK) ? GColorBlack : GColorWhite;
  GColor fg = (settings.color_scheme == COLOR_DARK) ? GColorWhite : GColorBlack;

  if (s_main_window) {
    window_set_background_color(s_main_window, bg);
  }

  if (s_airport_noon_code_layer)  text_layer_set_text_color(s_airport_noon_code_layer, fg);
  if (s_airport_noon_name_layer)  text_layer_set_text_color(s_airport_noon_name_layer, fg);
  if (s_airport_noon_time_layer)  text_layer_set_text_color(s_airport_noon_time_layer, fg);
  if (s_tid_layer)               text_layer_set_text_color(s_tid_layer, fg);
  if (s_beat_layer)              text_layer_set_text_color(s_beat_layer, fg);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

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
// #include "clock_decimal.h"

// --- Clock Modules & Settings ---
#define SETTINGS_KEY 1

// --- AppMessage Keys for Airport Info ---
#define KEY_REQUEST_TYPE  200
#define KEY_AIRPORT_CODE  201
#define KEY_CITY          202
#define KEY_COUNTRY       203

#define REQUEST_AIRPORT_INFO 1

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

// Detail display
static char s_airport_detail_buf[64];
static bool s_showing_details = false;
static AppTimer *s_detail_timer = NULL;

// Interaction handlers
static void detail_timeout_handler(void *data);
static void tap_handler(AccelAxisType axis, int32_t direction);

// AppMessage diagnostics forward decls
static void inbox_dropped_handler(AppMessageResult reason, void *context);
static void out_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context);

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
  bool settings_changed = false;
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
    settings_changed = true;
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Key timeAlignmentMode not found!");
  }

  // Read color scheme preference
  Tuple *color_scheme_t = dict_find(iter, MESSAGE_KEY_colorScheme);
  if (color_scheme_t) {
    int recv_val = (int)color_scheme_t->value->int32;
    settings.color_scheme = (recv_val == 49) ? COLOR_DARK : COLOR_LIGHT;
    settings_changed = true;
  }

  // Handle airport detail response
  Tuple *city_t = dict_find(iter, KEY_CITY);
  Tuple *country_t = dict_find(iter, KEY_COUNTRY);
  if (city_t && country_t) {
    snprintf(s_airport_detail_buf, sizeof(s_airport_detail_buf), "%s, %s", city_t->value->cstring, country_t->value->cstring);
    text_layer_set_text(s_airport_noon_name_layer, s_airport_detail_buf);
    s_showing_details = true;
    s_detail_timer = app_timer_register(5000, detail_timeout_handler, NULL);  // show for 5s
  }

  // Save and apply if any settings changed
  if (settings_changed) {
    save_settings();
    apply_color_scheme();
    s_last_re_eval_time = -1; // Force re-evaluation on next tick
  }
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
  // Re-evaluation (picking a new airport) is skipped if s_showing_details is true.
  clock_closest_airport_noon_update(s_airport_noon_code_layer,
                                      s_airport_noon_time_layer,
                                      seconds,
                                      target_seconds,
                                      !s_showing_details); // allow_reeval

  // Update airport name below the code (skip if showing temporary details)
  if (!s_showing_details) {
    text_layer_set_text(s_airport_noon_name_layer, s_selected_name);
  }
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
  const int city_h = 28;
  const int name_h = 28;  // height for airport name (increased to fit descenders)
  const int usable_h = hero_h - city_h - name_h;
  const int time_font_h = 42;
  // Create city name line
  s_airport_noon_code_layer = clock_closest_airport_noon_code_init(
      GRect(0, 0, bounds.size.w, city_h), window_layer);
  text_layer_set_text_alignment(s_airport_noon_code_layer, GTextAlignmentCenter);
  // Create airport name line below the IATA code
  s_airport_noon_name_layer = text_layer_create(GRect(3, name_h, bounds.size.w - 5, name_h));
  text_layer_set_font(s_airport_noon_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_background_color(s_airport_noon_name_layer, GColorClear); // Make background transparent
  text_layer_set_text_alignment(s_airport_noon_name_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_airport_noon_name_layer));
  // Create hero time line, vertically centered
  int time_y = city_h + name_h + (usable_h - time_font_h) / 2 - 7;
  s_airport_noon_time_layer = clock_closest_airport_noon_time_init(
      GRect(0, time_y, bounds.size.w, time_font_h), window_layer);

  // Setup footer area
  int w = bounds.size.w;
  int h = bounds.size.h;
  int footer_y = h - footer_h;
  // Two line heights to fit fonts without clipping
  int tid_h = 28;   // tall enough for 18px font + padding
  int beat_h = footer_h - tid_h;

  // Footer line 1: TID (bigger, center aligned)
  s_tid_layer = clock_tid_init(GRect(0, footer_y - 1, w, tid_h), window_layer);
  text_layer_set_font(s_tid_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_tid_layer, GTextAlignmentCenter);

  // Footer line 2: Beat (smaller, center aligned)
  s_beat_layer = clock_beat_init(GRect(0, footer_y + tid_h - 3, w, beat_h), window_layer);
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

  // Register with services
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);

  // Register AppMessage handlers
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(out_failed_handler);
  // Open AppMessage with larger inbox/outbox sizes (watch needs outbox >0 to send)
  const uint32_t INBOX_SIZE = 256;
  const uint32_t OUTBOX_SIZE = 256;
  AppMessageResult result = app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  if (result == APP_MSG_OK) {
      APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage opened successfully!");
  } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to open AppMessage: %d", result);
  }
}

static void deinit() {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
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

static void detail_timeout_handler(void *data) {
  (void)data;
  s_showing_details = false;
  text_layer_set_text(s_airport_noon_name_layer, s_selected_name);
}

// --- Tap Handling --------------------------------------------------------
static void tap_handler(AccelAxisType axis, int32_t direction) {
  (void)axis; (void)direction;
  APP_LOG(APP_LOG_LEVEL_INFO, "Tap detected - requesting airport info");
  DictionaryIterator *out_iter;
  AppMessageResult res = app_message_outbox_begin(&out_iter);
  if (res == APP_MSG_OK && out_iter) {
    dict_write_uint8(out_iter, KEY_REQUEST_TYPE, REQUEST_AIRPORT_INFO);
    dict_write_cstring(out_iter, KEY_AIRPORT_CODE, s_selected_code);
    app_message_outbox_send();
    text_layer_set_text(s_airport_noon_name_layer, "Fetching...");
    s_showing_details = true;  // Freeze display until info arrives
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Tap: outbox begin failed: %d", res);
  }
}

// --- AppMessage diagnostic handlers ------------------------------------
static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  (void)context;
#if ENABLE_APP_LOGS
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", reason);
#else
  (void)reason;
#endif
}

static void out_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  (void)iter; (void)context;
#if ENABLE_APP_LOGS
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", reason);
#else
  (void)reason;
#endif
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

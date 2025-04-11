#include <pebble.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h> // Required for time_t

// --- Constants ---
#define HOUR_LENGTH 3600 // Seconds in an hour
#define DAY_LENGTH 86400 // Seconds in a day (24 * 3600)

// --- Global Variables ---
static Window *s_main_window;
static TextLayer *s_beat_layer; // Layer for Swatch Beat Time
static TextLayer *s_tid_layer;  // Layer for TID Time (renamed from s_time_layer)

static const char S32_CHAR[] = "234567abcdefghijklmnopqrstuvwxyz";
#define S32_CHAR_LEN (sizeof(S32_CHAR) - 1) // 32

static uint64_t last_timestamp = 0;

// Encodes 'i' into 'buffer' (size 'buffer_len').
// Returns a pointer to the start of the encoded string within the buffer.
// Fills from the end. Result is NOT null-terminated by this function itself.
static char* s32encode_c(uint64_t i, char *buffer, size_t buffer_len) {
    if (buffer_len == 0) return NULL;

    char *ptr = buffer + buffer_len; // Point *past* the end

    // Handle 0 case directly if timestamp/clockid can be 0
    if (i == 0 && ptr > buffer) {
         *(--ptr) = S32_CHAR[0]; // '2'
         return ptr;
    }

    // Encode the number from right to left
    while (i > 0 && ptr > buffer) {
        // Optimize: Use bitwise operations for division/modulo by 32 (2^5)
        uint8_t remainder = i & 31; // i % 32
        i >>= 5;                    // i / 32
        *(--ptr) = S32_CHAR[remainder]; // Place character and move pointer left
    }

    // If i is still > 0 here, the buffer was too small for the number.
    // This shouldn't happen for timestamp (11 chars) or clockid (2 chars).

    return ptr; // Pointer to the first character written
}


// Creates a TID in tid_buffer (size must be >= 14)
static void createRaw_c(uint64_t timestamp, uint16_t clockid, char *tid_buffer, size_t tid_buffer_len) {
    if (tid_buffer_len < 14) return; // Need space for 13 chars + null terminator

    // --- Timestamp Encoding (11 chars) ---
    char ts_temp_buffer[11];
    char* encoded_ts_start = s32encode_c(timestamp, ts_temp_buffer, sizeof(ts_temp_buffer));
    size_t encoded_ts_len = (ts_temp_buffer + sizeof(ts_temp_buffer)) - encoded_ts_start;

    // Pad beginning with '2'
    memset(tid_buffer, '2', 11);
    // Copy encoded part to the end of the 11-char section
    if (encoded_ts_start && encoded_ts_len > 0) {
        if (encoded_ts_len <= 11) {
            memcpy(tid_buffer + (11 - encoded_ts_len), encoded_ts_start, encoded_ts_len);
        } else {
            // Timestamp too large? Copy last 11 chars.
            memcpy(tid_buffer, encoded_ts_start + (encoded_ts_len - 11), 11);
        }
    }
    // If timestamp was 0, encoded_ts_len is 1 ('2'), correctly placed by the above.


    // --- Hardcoded Clock ID ("22") ---
    // The new logic always appends "22", ignoring the clockid parameter.
    tid_buffer[11] = S32_CHAR[0]; // '2'
    tid_buffer[12] = S32_CHAR[0]; // '2'

    // Null-terminate the final string
    tid_buffer[13] = '\0';
}

// Generates a new TID into tid_buffer (size must be >= 14)
static void now_c(time_t seconds, uint16_t milliseconds, char *tid_buffer, size_t tid_buffer_len) {
    if (tid_buffer_len < 14) return;

    uint64_t current_micros = (uint64_t)seconds * 1000000 + (uint64_t)milliseconds * 1000;

    // Ensure monotonicity (at microsecond level)
    if (current_micros <= last_timestamp) {
        current_micros = last_timestamp + 1;
    }
    last_timestamp = current_micros;

    // Create the final TID string
    // Pass 0 for clockid, although it's ignored by the modified createRaw_c
    createRaw_c(current_micros, 0, tid_buffer, tid_buffer_len);
}

// --- Swatch .beat Time Code ---
static char s_beat_buffer[7];   // Buffer for Beat time string "@XXX.X\0"
static int last_beat_time = -1; // Cache the last displayed beat time (multiplied by 10)

/**
 * Computes the current .beat time * 10 (0-9999)
 * @param current_seconds_utc The current time (UTC seconds since epoch)
 * @return The current beat time multiplied by 10.
 */
static int beat(time_t current_seconds_utc) {
  // Add one hour for Biel Mean Time (BMT)
  time_t now_bmt = current_seconds_utc + HOUR_LENGTH;
  // Calculate seconds into the current BMT day (use unsigned for safety)
  uint32_t day_now_bmt = now_bmt % DAY_LENGTH;

  // Calculate beats * 10.
  // Cast intermediate multiplication to 64-bit to prevent overflow.
  int b = (int)(((uint64_t)day_now_bmt * 10000) / DAY_LENGTH);

  // Clamp result just in case of edge cases (0 - 9999)
  if (b > 9999) b = 9999;
  if (b < 0) b = 0;
  return b;
}

/**
 * Updates the beat time TextLayer if the time has changed.
 * @param current_seconds_utc The current time (UTC seconds since epoch)
 */
static void update_beat_time(time_t current_seconds_utc) {
  int b = beat(current_seconds_utc); // b is 0-9999

  // Only update the layer if the value has changed
  if (b == last_beat_time) {
    return;
  }

  int beats_integer = b / 10;    // 0-999
  int beats_fraction = b % 10;   // 0-9

  // Format as @XXX.X (e.g., @083.3, @999.9)
  snprintf(s_beat_buffer, sizeof(s_beat_buffer), "@%03d.%d", beats_integer, beats_fraction);

  // Update the TextLayer
  text_layer_set_text(s_beat_layer, s_beat_buffer);

  // Cache the newly displayed value
  last_beat_time = b;
}

// --- TID Time Update ---
static char s_tid_buffer[14]; // Buffer for TID string (13 chars + null)

/**
 * Updates the TID time TextLayer.
 * Uses provided time components for efficiency.
 */
static void update_tid_time(time_t seconds, uint16_t milliseconds) {
  // Generate the current TID into the buffer using provided time
  now_c(seconds, milliseconds, s_tid_buffer, sizeof(s_tid_buffer));

  // Display this TID on the TextLayer
  // Note: TID changes every microsecond theoretically,
  // so we update the text layer every second regardless of visible change.
  text_layer_set_text(s_tid_layer, s_tid_buffer);
}


// --- Pebble Window Management ---

// Handles updates from the TickTimerService
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Get time once for both updates
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds);

  // Update both time displays
  update_beat_time(seconds);
  update_tid_time(seconds, milliseconds);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Define layout constants (adjust heights based on font sizes)
  const int16_t top_margin = 5;
  const int16_t bottom_margin = 12;
  const int16_t beat_layer_height = 30; // Approx height for FONT_KEY_GOTHIC_28_BOLD
  const int16_t tid_layer_height = 30;  // Approx height for FONT_KEY_GOTHIC_24_BOLD

  // Calculate Y positions
  int16_t beat_y = top_margin;
  int16_t tid_y = bounds.size.h - tid_layer_height - bottom_margin;

  // Create Beat Time TextLayer (Top)
  s_beat_layer = text_layer_create(
      GRect(0, beat_y, bounds.size.w, beat_layer_height));
  text_layer_set_background_color(s_beat_layer, GColorClear);
  text_layer_set_text_color(s_beat_layer, GColorBlack);
  text_layer_set_text(s_beat_layer, "@--.-"); // Initial placeholder
  text_layer_set_font(s_beat_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD)); // Adjust font as needed
  text_layer_set_text_alignment(s_beat_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_beat_layer));

  // Create TID TextLayer (Bottom)
  s_tid_layer = text_layer_create(
      GRect(0, tid_y, bounds.size.w, tid_layer_height));
  text_layer_set_background_color(s_tid_layer, GColorClear);
  text_layer_set_text_color(s_tid_layer, GColorBlack);
  text_layer_set_text(s_tid_layer, "loading tid..."); // Initial placeholder
  text_layer_set_font(s_tid_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD)); // Adjust font as needed
  text_layer_set_text_alignment(s_tid_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_tid_layer));
}

static void main_window_unload(Window *window) {
  // Destroy TextLayers
  text_layer_destroy(s_beat_layer);
  text_layer_destroy(s_tid_layer); // Renamed from s_time_layer
}

static void init() {
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
  time_ms(&seconds, &milliseconds);
  update_beat_time(seconds); // Initial beat time
  update_tid_time(seconds, milliseconds); // Initial TID time


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

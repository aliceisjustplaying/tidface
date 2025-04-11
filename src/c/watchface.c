#include <pebble.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static Window *s_main_window;
static TextLayer *s_time_layer;

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
static void now_c(char *tid_buffer, size_t tid_buffer_len) {
    if (tid_buffer_len < 14) return;

    time_t seconds;
    uint16_t milliseconds;
    // Get time with millisecond precision
    time_ms(&seconds, &milliseconds);

    // Convert to microseconds (Pebble resolution)
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


// --- Watchface Code ---

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Create the TextLayer with specific bounds
  s_time_layer =
      text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(58, 52), bounds.size.w, 50));

  // Improve the layout to be more like a watchface
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  // text_layer_set_text(s_time_layer, "00:00");
  text_layer_set_font(s_time_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
}

static void update_time() {
  static char s_tid_buffer[14]; // Buffer for TID string (13 chars + null)
  now_c(s_tid_buffer, sizeof(s_tid_buffer)); // Generate TID

  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, s_tid_buffer); // Display TID
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
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

  // Update the time immediately
  update_time();

  // Register with TickTimerService
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void deinit() { window_destroy(s_main_window); }

int main(void) {
  init();
  app_event_loop();
  deinit();
}

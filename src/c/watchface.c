#include <pebble.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h> // Required for time_t
#include <math.h>  // Required for fabsf
#include <stdbool.h> // Required for bool type

// --- Include Generated Timezone Data ---
// This assumes tz_list.c defines TzInfo, tz_list[], and tz_list_count
#include "tz_list.c"

// --- Constants ---
#define HOUR_LENGTH 3600 // Seconds in an hour
#define DAY_LENGTH 86400 // Seconds in a day (24 * 3600)

// --- Global Variables ---
static Window *s_main_window;
static TextLayer *s_beat_layer; // Layer for Swatch Beat Time
static TextLayer *s_tid_layer;  // Layer for TID Time (renamed from s_time_layer)
static TextLayer *s_noonzone_layer; // Layer for Noon Zone Time
static TextLayer *s_closest_noon_layer; // Layer for Closest-to-Noon TZ

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

// --- Noon Zone Time Code ---
static char s_noonzone_buffer[16]; // Buffer for "NAME:MM:SS\0"
static int last_noonzone_update_secs = -1; // Cache full secs for update check
static int last_utc_hour = -1;             // Cache hour for zone name lookup
static const char *last_zone_name_ptr = NULL; // Cache pointer to zone name string

/**
 * Gets the military timezone name for the longitude where it is currently noon,
 * based on the provided UTC hour.
 * Uses caching to avoid repeated lookups for the same hour.
 */
static const char* get_noon_zone_name(int utc_hour) {
    // Check cache first
    if (utc_hour == last_utc_hour && last_zone_name_ptr != NULL) {
        return last_zone_name_ptr;
    }

    const char *name = "???"; // Default for unexpected hours
    switch(utc_hour){
        // Cases map UTC hour to the zone where it's noon
        case 12: name="ZULU"; break;
        case 11: name="ALPHA"; break;
        case 10: name="BRAVO"; break;
        case 9:  name="CHARLIE"; break;
        case 8:  name="DELTA"; break;
        case 7:  name="ECHO"; break;
        case 6:  name="FOXTROT"; break;
        case 5:  name="GOLF"; break;
        case 4:  name="HOTEL"; break;
        // INDIA is skipped
        case 3:  name="JULIET"; break;
        case 2:  name="KILO"; break;
        case 1:  name="LIMA"; break;
        case 0:  name="MIKE"; break; // or YANKEE
        // Other half of the day (wrapping around)
        case 23: name="NOVEMBER"; break;
        case 22: name="OSCAR"; break;
        case 21: name="PAPA"; break;
        case 20: name="QUEBEC"; break;
        case 19: name="ROMEO"; break;
        case 18: name="SIERRA"; break;
        case 17: name="TANGO"; break;
        case 16: name="UNIFORM"; break;
        case 15: name="VICTOR"; break;
        case 14: name="WHISKEY"; break;
        case 13: name="X-RAY"; break;
    }

    // Update cache
    last_utc_hour = utc_hour;
    last_zone_name_ptr = name;
    return name;
}

/**
 * Updates the Noon Zone time TextLayer if the time (seconds) has changed.
 */
static void update_noonzone_time(time_t current_seconds_utc) {
    // Check if the second has changed since the last update
    if (current_seconds_utc == last_noonzone_update_secs) {
        return;
    }

    struct tm *utc_tm = gmtime(&current_seconds_utc);
    if (!utc_tm) { // Check if gmtime failed
        return;
    }

    const char *zone_name = get_noon_zone_name(utc_tm->tm_hour);

    // Format as NAME:MM:SS
    snprintf(s_noonzone_buffer, sizeof(s_noonzone_buffer),
             "%s:%02d:%02d",
             zone_name,
             utc_tm->tm_min,
             utc_tm->tm_sec);

    text_layer_set_text(s_noonzone_layer, s_noonzone_buffer);

    // Cache the time of this update
    last_noonzone_update_secs = current_seconds_utc;
}

// --- Closest to Noon Timezone Code ---
#define NOON_SECONDS (12 * 3600L) // Noon in seconds past midnight
static char s_closest_noon_buffer[32]; // Buffer for "City Name:MM:SS\0"
static int last_closest_noon_update_secs = -1; // Cache based on seconds
static int last_closest_candidate_indices[TZ_LIST_COUNT]; // Cache indices of previous winners
static int last_closest_candidate_count = 0;             // Cache count of previous winners
static const char* last_chosen_closest_name = NULL;      // Cache the specific name pointer displayed

/**
 * Comparison function for qsort (integers).
 */
static int compare_ints(const void *a, const void *b) {
   return (*(int*)a - *(int*)b);
}

/**
 * Checks if two arrays of integers (candidate indices) are identical.
 * Assumes arrays are sorted beforehand.
 */
static bool are_candidate_sets_equal(int* set1, int count1, int* set2, int count2) {
    if (count1 != count2) {
        return false;
    }
    // Assumes counts are equal now
    for (int i = 0; i < count1; ++i) {
        if (set1[i] != set2[i]) {
            return false;
        }
    }
    return true;
}

/**
 * Updates the Closest-to-Noon timezone TextLayer if the second has changed.
 */
static void update_closest_noon_time(time_t current_seconds_utc) {
    // --- Caching --- 
    if (current_seconds_utc == last_closest_noon_update_secs) {
        return;
    }

    // --- Get Current UTC MM:SS --- 
    struct tm *utc_tm = gmtime(&current_seconds_utc);
    if (!utc_tm) return;
    int utc_min = utc_tm->tm_min;
    int utc_sec = utc_tm->tm_sec;

    // --- Calculate UTC seconds past midnight --- 
    // Note: tm_yday is 0-365, time_t is secs since epoch. Simpler to use modulo.
    uint32_t utc_seconds_today = current_seconds_utc % DAY_LENGTH;

    // --- Find Timezone Closest to Noon --- 
    long min_diff_secs = DAY_LENGTH; // Init with impossibly large difference
    int current_candidate_indices[TZ_LIST_COUNT]; // Store indices of current winners
    int current_candidate_count = 0;

    for (int i = 0; i < TZ_LIST_COUNT; ++i) {
        // Calculate offset in seconds (handle float)
        long offset_seconds = (long)(tz_list[i].offset_hours * 3600.0f);

        // Calculate local time in seconds past midnight (handle wrap around DAY_LENGTH)
        long local_seconds_today = (long)utc_seconds_today + offset_seconds;
        // Modulo that handles negative results correctly 
        local_seconds_today = (local_seconds_today % DAY_LENGTH + DAY_LENGTH) % DAY_LENGTH;

        // Calculate the shortest difference to noon (around the 24h clock)
        long diff1 = labs(local_seconds_today - NOON_SECONDS); // labs for long
        long current_min_diff = (diff1 <= DAY_LENGTH / 2) ? diff1 : DAY_LENGTH - diff1;

        // --- Update candidate list --- 
        if (current_min_diff < min_diff_secs) {
            // New minimum found
            min_diff_secs = current_min_diff;
            current_candidate_indices[0] = i;
            current_candidate_count = 1;
        } else if (current_min_diff == min_diff_secs) {
            // Tie found, add to candidates (if space permits - should be fine)
            if (current_candidate_count < TZ_LIST_COUNT) { // Safety check
                current_candidate_indices[current_candidate_count++] = i;
            }
        }
    }

    // --- Select Final Timezone and Name --- 
    const char *final_name = "???";

    // Sort current candidates for comparison
    if (current_candidate_count > 1) {
        qsort(current_candidate_indices, current_candidate_count, sizeof(int), compare_ints);
    }

    // Check if the winning set is the same as last time
    bool reuse_last_name = false;
    if (last_chosen_closest_name != NULL) { // Only reuse if we have a previous name
         // Assume last_closest_candidate_indices is already sorted from previous run
         if (are_candidate_sets_equal(current_candidate_indices, current_candidate_count,
                                        last_closest_candidate_indices, last_closest_candidate_count)) {
             reuse_last_name = true;
             final_name = last_chosen_closest_name; // Tentatively reuse
         }
    }

    // If not reusing, or first time, perform selection
    if (!reuse_last_name) {
        if (current_candidate_count > 0) {
            int chosen_list_index;
            if (current_candidate_count == 1) {
                chosen_list_index = current_candidate_indices[0];
            } else {
                // Randomly select among tied candidates
                chosen_list_index = current_candidate_indices[rand() % current_candidate_count];
            }

            // Select random name if multiple exist for the chosen offset
            const TzInfo *chosen_tz = &tz_list[chosen_list_index];
            if (chosen_tz->name_count > 0) {
                int name_index = (chosen_tz->name_count == 1) ? 0 : (rand() % chosen_tz->name_count);
                if (name_index < chosen_tz->name_count) { // Bounds check
                   final_name = chosen_tz->names[name_index].name;
                }
            }

            // --- Update Cache for next time --- 
            last_chosen_closest_name = final_name; // Cache the chosen name pointer
            // Copy current candidates to last candidates cache (already sorted)
            memcpy(last_closest_candidate_indices, current_candidate_indices, current_candidate_count * sizeof(int));
            last_closest_candidate_count = current_candidate_count;

        } else {
             // Should not happen if tz_list is not empty, but handle defensively
             last_chosen_closest_name = NULL; // Reset cache if no candidates found
             last_closest_candidate_count = 0;
        }
    } // end selection block

    // --- Format Output and Update Layer --- 
    // Use the determined final_name (either reused or newly selected)
    snprintf(s_closest_noon_buffer, sizeof(s_closest_noon_buffer),
             "%s:%02d:%02d", final_name, utc_min, utc_sec);

    text_layer_set_text(s_closest_noon_layer, s_closest_noon_buffer);

    // Cache the update time
    last_closest_noon_update_secs = current_seconds_utc;
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
  update_noonzone_time(seconds);
  update_closest_noon_time(seconds);
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
  s_beat_layer = text_layer_create(
      GRect(0, beat_y, bounds.size.w, beat_h));
  text_layer_set_background_color(s_beat_layer, GColorClear);
  text_layer_set_text_color(s_beat_layer, GColorBlack);
  text_layer_set_text(s_beat_layer, "@--.-"); // Initial placeholder
  text_layer_set_font(s_beat_layer, fonts_get_system_font(BEAT_FONT));
  text_layer_set_text_alignment(s_beat_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_beat_layer));

  // Create Noon Zone Time TextLayer (Middle)
  s_noonzone_layer = text_layer_create(
      GRect(0, noonzone_y, bounds.size.w, noonzone_h));
  text_layer_set_background_color(s_noonzone_layer, GColorClear);
  text_layer_set_text_color(s_noonzone_layer, GColorBlack);
  text_layer_set_text(s_noonzone_layer, "ZONE:--:--"); // Initial placeholder
  text_layer_set_font(s_noonzone_layer, fonts_get_system_font(NOONZONE_FONT));
  text_layer_set_text_alignment(s_noonzone_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_noonzone_layer));

  // Create Closest Noon Time TextLayer (Middle-Bottom)
  s_closest_noon_layer = text_layer_create(
      GRect(0, closest_y, bounds.size.w, closest_h));
  text_layer_set_background_color(s_closest_noon_layer, GColorClear);
  text_layer_set_text_color(s_closest_noon_layer, GColorBlack);
  text_layer_set_text(s_closest_noon_layer, "City:--:--"); // Initial placeholder
  text_layer_set_font(s_closest_noon_layer, fonts_get_system_font(CLOSEST_FONT));
  text_layer_set_text_alignment(s_closest_noon_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_closest_noon_layer));

  // Create TID TextLayer (Bottom)
  s_tid_layer = text_layer_create(
      GRect(0, tid_y, bounds.size.w, tid_h));
  text_layer_set_background_color(s_tid_layer, GColorClear);
  text_layer_set_text_color(s_tid_layer, GColorBlack);
  text_layer_set_text(s_tid_layer, "loading tid..."); // Initial placeholder
  text_layer_set_font(s_tid_layer, fonts_get_system_font(TID_FONT));
  text_layer_set_text_alignment(s_tid_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_tid_layer));
}

static void main_window_unload(Window *window) {
  // Destroy TextLayers
  text_layer_destroy(s_beat_layer);
  text_layer_destroy(s_tid_layer); // Renamed from s_time_layer
  text_layer_destroy(s_noonzone_layer);
  text_layer_destroy(s_closest_noon_layer);
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
  update_noonzone_time(seconds); // Initial noon zone time
  update_closest_noon_time(seconds); // Initial closest noon time


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

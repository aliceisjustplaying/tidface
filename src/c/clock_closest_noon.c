#include "clock_closest_noon.h"
#include <pebble.h> // Always include pebble for device builds
#include <stdlib.h> // For rand()
#include <stdint.h> // For uint32_t, int64_t
#include <time.h>   // For time_t, struct tm, gmtime, strcmp, strncmp
#include <stdbool.h>// For bool
#include <limits.h> // For LONG_MAX // Not strictly needed anymore, but harmless
#include <string.h> // For strcmp, strncmp
#include <stdio.h>  // For snprintf
#include "text_layer_util.h"

// Include the generated timezone data structure definitions and list
// This must contain TzInfo, tz_list[], and TZ_LIST_COUNT
#include "tz_list.c"

#define DAY_SECONDS (24 * 3600L)
#define NOON_SECONDS (12 * 3600L)

// --- Static variables specific to Closest Noon Clock ---
static char s_closest_noon_buffer[40]; // Increased size for potentially longer names + MM:SS
static time_t s_last_update_time = -1;
static time_t s_last_re_evaluation_time = -1;

// Stores the results of the last re-evaluation
static const char *s_selected_city_name = "Wait...";
static float s_selected_offset_hours = 0.0f; // Store the active offset

// --- Static helper functions ---

/**
 * @brief Re-evaluates and selects the timezone whose local time is >= 12:00:00
 *        and closest to 12:00:00, based on current UTC.
 *        Updates the static s_selected_city_name and s_selected_offset_hours.
 *        Should only be called at :00, :15, :30, :45 minutes past the hour (UTC).
 *
 * @param current_utc_t The current UTC time as time_t.
 */
static void update_selected_timezone_and_city(time_t current_utc_t) {
    // Seed for consistent randomness when multiple zones tie
    srand(current_utc_t);
    uint32_t utc_secs = current_utc_t % DAY_SECONDS;
    long best_delta = LONG_MAX;
    int best_count = 0;
    int best_candidates[TZ_LIST_COUNT];
    // Find zones that have local time >= noon and minimal seconds past noon
    for (int i = 0; i < (int)TZ_LIST_COUNT; ++i) {
        const TzInfo *tz = &tz_list[i];
        // Determine DST active
        bool is_dst = false;
        if (tz->dst_start_utc && tz->dst_end_utc) {
            int64_t now = (int64_t)current_utc_t;
            if ((tz->dst_start_utc <= tz->dst_end_utc && now >= tz->dst_start_utc && now < tz->dst_end_utc) ||
                (tz->dst_start_utc > tz->dst_end_utc && (now >= tz->dst_start_utc || now < tz->dst_end_utc))) {
                is_dst = true;
            }
        }
        float off_h = is_dst ? tz->dst_offset_hours : tz->std_offset_hours;
        long local_secs = (long)utc_secs + (long)(off_h * 3600.0f);
        local_secs %= DAY_SECONDS;
        if (local_secs < 0) local_secs += DAY_SECONDS;
        if (local_secs < NOON_SECONDS) continue;
        long delta = local_secs - NOON_SECONDS;
        if (delta < best_delta) {
            best_delta = delta;
            best_count = 0;
            best_candidates[best_count++] = i;
        } else if (delta == best_delta && best_count < (int)TZ_LIST_COUNT) {
            best_candidates[best_count++] = i;
        }
    }
    if (best_count == 0) {
        s_selected_city_name = "Wait...";
        s_selected_offset_hours = 0.0f;
    } else {
        int pick = (best_count == 1) ? 0 : (rand() % best_count);
        int idx = best_candidates[pick];
        const TzInfo *tz = &tz_list[idx];
        bool is_dst = (current_utc_t >= tz->dst_start_utc && current_utc_t < tz->dst_end_utc);
        s_selected_offset_hours = is_dst ? tz->dst_offset_hours : tz->std_offset_hours;
        int cnt = tz->name_count;
        int ni = (cnt == 1) ? 0 : (rand() % cnt);
        s_selected_city_name = tz_name_pool[tz->name_offset + ni];
    }
    s_last_re_evaluation_time = current_utc_t;
}


// --- Interface Functions (Pebble only) ---

TextLayer* clock_closest_noon_init(GRect bounds, Layer *window_layer) {
    TextLayer* layer = text_layer_util_create(bounds, window_layer, "Wait...", FONT_KEY_GOTHIC_18_BOLD);

    // Initialize static vars
    s_selected_city_name = "Wait...";
    s_selected_offset_hours = 0.0f;
    s_last_update_time = -1;
    s_last_re_evaluation_time = -1; // Force re-evaluation on first update

    return layer;
}

void clock_closest_noon_deinit(TextLayer *layer) {
    if (layer) {
        text_layer_destroy(layer);
    }
}

void clock_closest_noon_update(TextLayer *layer, time_t current_utc_t) {
    if (!layer) return;

    // Avoid redundant updates for the same second
    if (current_utc_t == s_last_update_time) {
        return;
    }
    s_last_update_time = current_utc_t;

    // --- Check if it's time for re-evaluation ---
    struct tm *utc_tm_struct = gmtime(&current_utc_t); // Use a different name to avoid shadowing
    bool needs_re_evaluation = false;

    // Check if gmtime succeeded and if it's a re-evaluation time point (:00, :15, :30)
    if (utc_tm_struct && (utc_tm_struct->tm_min % 15 == 0) && (utc_tm_struct->tm_min != 45) && (utc_tm_struct->tm_sec == 0)) {
         // Avoid re-evaluating multiple times within the same second if update is called rapidly
         if (current_utc_t != s_last_re_evaluation_time) {
            needs_re_evaluation = true;
         }
    } else if (s_last_re_evaluation_time == -1) {
        // Ensure initial evaluation happens if starting between intervals
        needs_re_evaluation = true;
    }

    if (needs_re_evaluation) {
        update_selected_timezone_and_city(current_utc_t);
    }


    // --- Calculate current local time based on selected zone ---
    // Use the stored offset from the last re-evaluation
    long current_offset_seconds = (long)(s_selected_offset_hours * 3600.0f);
    time_t local_time_epoch = current_utc_t + current_offset_seconds; // Use a distinct name

    // --- Format and display the time ---
    struct tm *current_local_tm_struct = gmtime(&local_time_epoch); // Use gmtime to format based on the calculated local epoch time

    // Check if a valid city was selected in the last evaluation or if it's fallback
    if (s_selected_city_name && strcmp(s_selected_city_name, "Wait...") != 0 &&
        strncmp(s_selected_city_name, "ERR:", 4) != 0)
    {
        if (current_local_tm_struct) {
            // Display format: CityName:MM:SS using actual local time
            snprintf(s_closest_noon_buffer, sizeof(s_closest_noon_buffer),
                     "%s:%02d:%02d",
                     s_selected_city_name, // Already checked for null/error states
                     current_local_tm_struct->tm_min,
                     current_local_tm_struct->tm_sec);
        } else {
            // Handle gmtime failure for a selected city
            snprintf(s_closest_noon_buffer, sizeof(s_closest_noon_buffer), "%s:ERR:TIME",
                     s_selected_city_name);
        }
    } else {
         // Display the fallback/error name directly (e.g., "Wait...", "ERR:NOZONE")
         snprintf(s_closest_noon_buffer, sizeof(s_closest_noon_buffer), "%s",
                  s_selected_city_name ? s_selected_city_name : "ERR:NULL");
    }


    text_layer_set_text(layer, s_closest_noon_buffer);
}

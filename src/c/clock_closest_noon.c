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
    // Seed random number generator (important for random selection)
    srand(current_utc_t);

    // Find the minimum local time that is >= noon
    long min_valid_local_seconds = DAY_SECONDS; // Initialize higher than any possible time

    // Structure to hold candidate timezone info
    typedef struct {
        int index;             // Index in tz_list
        long local_secs_today; // Local time in seconds past midnight
        float active_offset_h; // The offset used for calculation
    } ZoneCandidate;

    ZoneCandidate candidates[TZ_LIST_COUNT];
    int candidate_count = 0; // Count of zones with time >= noon

    uint32_t utc_seconds_today = current_utc_t % DAY_SECONDS;

    // --- Pass 1: Calculate local time for all zones, filter >= noon, find minimum valid time ---
    for (int i = 0; i < TZ_LIST_COUNT; ++i) {
        const TzInfo *tz = &tz_list[i];
        bool is_dst_active = false;

        // --- Determine if DST is active ---
        if (tz->dst_start_utc != 0LL && tz->dst_end_utc != 0LL) {
            int64_t start_time = tz->dst_start_utc;
            int64_t end_time = tz->dst_end_utc;
            int64_t current_time_64 = (int64_t)current_utc_t;
            // Standard check for non-wrapping interval
            if (start_time <= end_time) {
                if (current_time_64 >= start_time && current_time_64 < end_time) {
                    is_dst_active = true;
                }
            }
            // Check for wrapping interval (e.g., Southern Hemisphere DST)
            else {
                if (current_time_64 >= start_time || current_time_64 < end_time) {
                    is_dst_active = true;
                }
            }
        }

        float active_offset_hours = is_dst_active ? tz->dst_offset_hours : tz->std_offset_hours;
        long offset_seconds = (long)(active_offset_hours * 3600.0f);

        // Calculate local time in seconds past local midnight
        long local_seconds_today = ((long)utc_seconds_today + offset_seconds);
        // Ensure positive modulo result within the day
        local_seconds_today %= DAY_SECONDS;
        if (local_seconds_today < 0) {
            local_seconds_today += DAY_SECONDS;
        }

        // --- Check if local time is at or after noon ---
        if (local_seconds_today >= NOON_SECONDS) {
            // Store as a potential candidate
            if (candidate_count < TZ_LIST_COUNT) {
                 candidates[candidate_count].index = i;
                 candidates[candidate_count].local_secs_today = local_seconds_today;
                 candidates[candidate_count].active_offset_h = active_offset_hours;
                 candidate_count++; // Increment count of valid candidates
            }
            // Update the minimum valid local time found so far
            if (local_seconds_today < min_valid_local_seconds) {
                min_valid_local_seconds = local_seconds_today;
            }
        }
    }

    // --- Pass 2: Collect all candidates matching the minimum valid local time ---
    int best_candidates_indices[TZ_LIST_COUNT]; // Stores indices into 'candidates' array
    int best_candidate_count = 0;
    if (candidate_count > 0) { // Only proceed if we found any zones >= noon
        for (int k = 0; k < candidate_count; ++k) {
            // Check if this candidate's time is the minimum valid time we found
            if (candidates[k].local_secs_today == min_valid_local_seconds) {
                if (best_candidate_count < TZ_LIST_COUNT) {
                    best_candidates_indices[best_candidate_count++] = k; // Store index into 'candidates' array
                }
            }
        }
    }


    // --- Select the winning timezone and city ---
    if (best_candidate_count > 0) {
        // Randomly select one of the best candidates
        int list_pos = (best_candidate_count == 1) ? 0 : (rand() % best_candidate_count);
        int winning_candidate_idx_in_candidates = best_candidates_indices[list_pos]; // Index in 'candidates' array

        int chosen_zone_list_index = candidates[winning_candidate_idx_in_candidates].index; // Index in tz_list
        const TzInfo *chosen_tz = &tz_list[chosen_zone_list_index];

        // Randomly select a city name from the winning timezone
        if (chosen_tz->name_count > 0) {
            int name_index = (chosen_tz->name_count == 1) ? 0 : (rand() % chosen_tz->name_count);
            // Basic bounds check (should always be true if rand() works correctly)
            if (name_index >= 0 && name_index < chosen_tz->name_count) {
                s_selected_city_name = chosen_tz->names[name_index].name;
            } else {
                s_selected_city_name = "ERR:NAME"; // Safety fallback
            }
        } else {
            s_selected_city_name = "ERR:NO_NM"; // Safety fallback
        }
        // Store the active offset that was used for this winning timezone
        s_selected_offset_hours = candidates[winning_candidate_idx_in_candidates].active_offset_h;

    } else {
        // This case handles when NO timezone has local time >= 12:00:00 PM
        s_selected_city_name = "Wait..."; // Indicate searching or fallback state
        s_selected_offset_hours = 0.0f;     // Reset offset
    }
    s_last_re_evaluation_time = current_utc_t; // Record when we last did this
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

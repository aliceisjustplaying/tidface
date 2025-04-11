#include "clock_closest_noon.h"
#include <pebble.h> // Always include pebble for device builds
#include <stdlib.h> // For rand()
#include <stdint.h> // For uint32_t, int64_t
#include <time.h>   // For time_t, struct tm, gmtime
#include <stdbool.h>// For bool

// Include the generated timezone data structure definitions and list
// This must contain TzInfo, tz_list[], and TZ_LIST_COUNT
#include "tz_list.c"

#define DAY_LENGTH 86400
#define NOON_SECONDS (12 * 3600L)

// --- Static variables specific to Closest Noon Clock ---
static char s_closest_noon_buffer[32];
static int last_closest_noon_update_secs = -1;

// Cache for stabilization
static int last_chosen_zone_index = -1;     // Index in tz_list of the last chosen zone
static const char* last_displayed_name = NULL; // Specific name displayed last time

// --- Static helper functions ---
/**
 * Helper function to find the current noon-hour zone, handle stabilization,
 * and calculate display values. Switches proactively to the zone(s)
 * closest to 12:00:00 local time.
 *
 * Returns: true if a zone was found and display values calculated, false otherwise.
 * Outputs: Pointers to store the chosen display name, minutes past noon, and seconds past noon.
 */
static bool calculate_noon_zone_display(time_t current_seconds_utc,
                                        const char** out_display_name,
                                        long* out_display_minutes,
                                        long* out_display_seconds)
{
    time_t current_time_t = (time_t)current_seconds_utc;
    uint32_t utc_seconds_today = current_seconds_utc % DAY_LENGTH;

    // --- Find candidate zones currently in the noon hour (12:00:00 - 12:59:59) ---
    typedef struct {
        int index;
        long local_secs;
    } NoonCandidate;
    NoonCandidate current_candidates[TZ_LIST_COUNT];
    int current_candidate_count = 0;
    long min_local_secs_in_noon_hour = DAY_LENGTH; // Find the earliest time past 12:00

    for (int i = 0; i < TZ_LIST_COUNT; ++i) {
        const TzInfo *tz = &tz_list[i];
        bool is_dst_active = false;
        // --- Determine if DST is active --- 
        if (tz->dst_start_utc != 0LL && tz->dst_end_utc != 0LL) {
            int64_t start_time = tz->dst_start_utc;
            int64_t end_time = tz->dst_end_utc;
            int64_t current_time_64 = (int64_t)current_time_t;
            if (start_time <= end_time) {
                if (current_time_64 >= start_time && current_time_64 < end_time) is_dst_active = true;
            } else {
                if (current_time_64 >= start_time || current_time_64 < end_time) is_dst_active = true;
            }
        }
        float chosen_offset_hours = is_dst_active ? tz->dst_offset_hours : tz->std_offset_hours;
        long offset_seconds = (long)(chosen_offset_hours * 3600.0f);
        long local_seconds_today = ((long)utc_seconds_today + offset_seconds) % DAY_LENGTH;
        if (local_seconds_today < 0) local_seconds_today += DAY_LENGTH;

        // --- Check if this zone is in the noon hour --- 
        if (local_seconds_today >= NOON_SECONDS && local_seconds_today < (NOON_SECONDS + 3600)) {
            if (current_candidate_count < TZ_LIST_COUNT) {
                current_candidates[current_candidate_count].index = i;
                current_candidates[current_candidate_count].local_secs = local_seconds_today;
                current_candidate_count++;
                // Track the minimum local time among candidates
                if (local_seconds_today < min_local_secs_in_noon_hour) {
                    min_local_secs_in_noon_hour = local_seconds_today;
                }
            }
        }
    }

    // --- Identify the "Best" Candidates (those with the minimum local time) ---
    NoonCandidate best_candidates[TZ_LIST_COUNT];
    int best_candidate_count = 0;
    if (current_candidate_count > 0) {
        for (int k = 0; k < current_candidate_count; ++k) {
            if (current_candidates[k].local_secs == min_local_secs_in_noon_hour) {
                 if (best_candidate_count < TZ_LIST_COUNT) {
                    best_candidates[best_candidate_count++] = current_candidates[k];
                 }
            }
        }
    }

    // --- Select the zone and name to display ---
    int chosen_zone_index = -1;
    long chosen_local_seconds = -1;
    const char *current_display_name = "Wait...";

    bool use_last_chosen = false;
    if (last_chosen_zone_index != -1 && best_candidate_count > 0) {
        // Check if the last chosen zone is among the *best* current candidates
        for (int k = 0; k < best_candidate_count; ++k) {
            if (best_candidates[k].index == last_chosen_zone_index) {
                chosen_zone_index = last_chosen_zone_index;
                chosen_local_seconds = best_candidates[k].local_secs;
                current_display_name = last_displayed_name; // Reuse the specific name
                use_last_chosen = true;
                break;
            }
        }
    }

    if (!use_last_chosen) { // If last wasn't valid *among the best*, or first run, or no candidates
        if (best_candidate_count > 0) {
            // Select an index randomly *from the best candidates*
            int list_pos = (best_candidate_count == 1) ? 0 : (rand() % best_candidate_count);
            chosen_zone_index = best_candidates[list_pos].index;
            chosen_local_seconds = best_candidates[list_pos].local_secs;

            // Select a specific city name from that chosen zone's list
            const TzInfo *chosen_tz = &tz_list[chosen_zone_index];
            if (chosen_tz->name_count > 0) {
                int name_index = (chosen_tz->name_count == 1) ? 0 : (rand() % chosen_tz->name_count);
                if (name_index < chosen_tz->name_count) {
                    current_display_name = chosen_tz->names[name_index].name;
                } else { current_display_name = "ERR:NAME"; } // Safety fallback
            } else { current_display_name = "ERR:NO_NM"; } // Safety fallback

            // Update static cache for next time
            last_chosen_zone_index = chosen_zone_index;
            last_displayed_name = current_display_name;
        } else {
            // No zones currently in noon hour - reset static cache
            last_chosen_zone_index = -1;
            last_displayed_name = NULL;
            current_display_name = "Wait..."; // Explicitly set placeholder
            chosen_zone_index = -1; // Ensure calculation is skipped
        }
    }

    // --- Calculate display values and return --- 
    if (chosen_zone_index != -1) {
        long seconds_since_noon_in_zone = chosen_local_seconds - NOON_SECONDS;
        *out_display_minutes = seconds_since_noon_in_zone / 60;
        *out_display_seconds = seconds_since_noon_in_zone % 60;
        *out_display_name = current_display_name;
        return true; // Success
    } else {
        *out_display_name = current_display_name; // Return "Wait..." or error
        return false; // Indicate failure
    }
}

// --- Interface Functions (Pebble only) ---

TextLayer* clock_closest_noon_init(GRect bounds, Layer *window_layer) {
    TextLayer* layer = text_layer_create(bounds);
    text_layer_set_background_color(layer, GColorClear);
    text_layer_set_text_color(layer, GColorBlack);
    text_layer_set_text(layer, "City:--:--"); // Initial placeholder
    #define CLOSEST_FONT FONT_KEY_GOTHIC_18_BOLD // Keep font definitions near usage
    text_layer_set_font(layer, fonts_get_system_font(CLOSEST_FONT));
    text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(layer));
    return layer;
}

void clock_closest_noon_deinit(TextLayer *layer) {
    if (layer) {
        text_layer_destroy(layer);
    }
}

void clock_closest_noon_update(TextLayer *layer, time_t current_seconds_utc) {
    if (!layer) return;

    // Avoid update if time hasn't changed
    if (current_seconds_utc == last_closest_noon_update_secs) {
        return;
    }

    const char *display_name;
    long display_minutes;
    long display_seconds;

    bool found_zone = calculate_noon_zone_display(current_seconds_utc,
                                                  &display_name,
                                                  &display_minutes,
                                                  &display_seconds);

    if (found_zone) {
        snprintf(s_closest_noon_buffer, sizeof(s_closest_noon_buffer),
                 "%s:%02ld:%02ld", display_name, display_minutes, display_seconds);
    } else {
        // Use the placeholder/error name returned by the helper
        snprintf(s_closest_noon_buffer, sizeof(s_closest_noon_buffer), "%s", display_name);
    }

    text_layer_set_text(layer, s_closest_noon_buffer);
    last_closest_noon_update_secs = current_seconds_utc;
}

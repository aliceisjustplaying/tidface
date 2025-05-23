#ifndef CLOCK_CLOSEST_AIRPORT_NOON_H
#define CLOCK_CLOSEST_AIRPORT_NOON_H

// A header-only implementation of a Pebble clock module that displays the
// IATA code of a randomly-chosen airport whose local time is the *closest past
// but not before* 12:00:00 (noon) relative to the current UTC.  The
// underlying data come from the generated `airport_tz_list.c`, which is built
// by `generateAirportTzList.ts`.
//
// The public interface mirrors `clock_closest_noon.h`, so you can swap calls
// easily in `watchface.c`.
//
//  • clock_closest_airport_noon_code_init   – returns a TextLayer* for the IATA
//    display (3- or 4-char string).
//  • clock_closest_airport_noon_time_init   – returns a TextLayer* for the
//    hero minutes : seconds display.
//  • clock_closest_airport_noon_update      – to be called once per second with
//    the current UTC epoch (as provided by Pebble's `time_ms`).
//  • clock_closest_airport_noon_deinit      – cleanup helper.
//
// Implementation note: The whole logic is declared `static inline` so that the
// header can be included in just one translation unit (e.g. `watchface.c`) and
// does not require a separate .c file.  All heavy data symbols are kept
// `static` in the generated C file, so multiple inclusion wouldn't clash.

#include <pebble.h>
#include <stdint.h>
#include <stdlib.h>   // rand, srand
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include "text_layer_util.h"

// Bring in the generated data table; make sure the build has already executed
// generate_airport_tz_list.py.
#include "airport_tz_list.c"
#define NAME_POOL           airport_name_pool
#define TZ_LIST             airport_tz_list
#define TZ_LIST_COUNT       AIRPORT_TZ_LIST_COUNT
// Bit-packed IATA code pool (15-bits per entry)
extern const uint16_t airport_code_pool_bits[];

#ifdef __cplusplus
extern "C" {
#endif

// Helper to fetch the Nth null-terminated name from the flat pool
static inline const char* _airport_flat_name(const char* pool, int nameIndex) {
    const char* p = pool;
    while (nameIndex-- > 0) {
        while (*p) p++; // skip current string
        p++;            // move past '\0'
    }
    return p;
}

// Public API ---------------------------------------------------------------

static inline TextLayer* clock_closest_airport_noon_code_init(GRect bounds,
                                                              Layer *window_layer);
static inline TextLayer* clock_closest_airport_noon_time_init(GRect bounds,
                                                              Layer *window_layer);
static inline void       clock_closest_airport_noon_deinit(TextLayer *layer);
static inline void       clock_closest_airport_noon_update(TextLayer *code_layer,
                                                           TextLayer *time_layer,
                                                           time_t     current_utc_t,
                                                           long       target_seconds_of_day);
// -------------------------------------------------------------------------

// Internal constants / storage --------------------------------------------
#define DAY_SECONDS   (24 * 3600L)

static time_t s_last_update_time        = -1;
static time_t s_last_re_eval_time       = -1;
static char s_selected_code[4]          = "---";  // IATA placeholder
static const char *s_selected_name      = "---";  // Airport name placeholder
static float s_selected_offset_hours    = 0.0f;

// Helper: determine if DST is active for a TzInfo at current UTC
static inline bool _airport_is_dst(const TzInfo *tz, int64_t now_utc) {
  if (tz->dst_start_utc == 0 && tz->dst_end_utc == 0) return false;
  if (tz->dst_start_utc <= tz->dst_end_utc) {
      return (now_utc >= tz->dst_start_utc && now_utc < tz->dst_end_utc);
  }
  // southern hemisphere wrap-around
  return (now_utc >= tz->dst_start_utc || now_utc < tz->dst_end_utc);
}

static inline void _airport_pick_new(time_t current_utc_t, long target_seconds_of_day) {
    srand((unsigned int)current_utc_t);  // stable randomness per eval moment

    uint32_t utc_secs = current_utc_t % DAY_SECONDS;
    long best_delta = LONG_MAX;
    int  best_count = 0;
    int  best_candidates[TZ_LIST_COUNT];

    // 1. Scan every timezone bucket to find the one(s) whose local time is >=12:00
    //    and *closest* to exactly noon.
    for (int i = 0; i < (int)TZ_LIST_COUNT; ++i) {
        const TzInfo *tz = &TZ_LIST[i];
        bool is_dst = _airport_is_dst(tz, (int64_t)current_utc_t);
        // reconstruct hours from quarter-hour units
        float offset_h = (is_dst ? tz->dst_quarters : tz->std_quarters) * 0.25f;
        long local_secs = (long)utc_secs + (long)(offset_h * 3600.0f);
        local_secs %= DAY_SECONDS;
        if (local_secs < 0) local_secs += DAY_SECONDS;
        if (local_secs < target_seconds_of_day) continue;   // hasn't reached target time yet
        long delta = local_secs - target_seconds_of_day;
        if (delta < best_delta) {
            best_delta = delta;
            best_count = 0;
            best_candidates[best_count++] = i;
        } else if (delta == best_delta && best_count < (int)TZ_LIST_COUNT) {
            best_candidates[best_count++] = i;
        }
    }

    // 2. Pick a random candidate, then a random airport code from that bucket
    if (best_count == 0) {
        memcpy((void *)s_selected_code, "---", 3);
        s_selected_code[3] = '\0'; // Ensure null termination
        s_selected_name = "---";
        s_selected_offset_hours = 0.0f;
    } else {
        int idx = best_candidates[(best_count == 1) ? 0 : (rand() % best_count)];
        const TzInfo *tz = &TZ_LIST[idx];
        bool is_dst = _airport_is_dst(tz, (int64_t)current_utc_t);
        // reconstruct float offset from quarter-hour units
        s_selected_offset_hours = (is_dst ? tz->dst_quarters : tz->std_quarters) * 0.25f;
        int cnt = tz->name_count;
        int ni  = (cnt == 1) ? 0 : (rand() % cnt);
        // unpack 3-letter code from bit-packed 15-bit entries
        uint16_t bits = airport_code_pool_bits[tz->name_offset + ni];
        s_selected_code[0] = 'A' + ((bits >> 10) & 0x1F);
        s_selected_code[1] = 'A' + ((bits >> 5) & 0x1F);
        s_selected_code[2] = 'A' + ( bits        & 0x1F);
        s_selected_code[3] = '\0';
        s_selected_name = _airport_flat_name(NAME_POOL, tz->name_offset + ni);
    }
    s_last_re_eval_time = current_utc_t;
}

// Public function implementations -----------------------------------------
static inline TextLayer* clock_closest_airport_noon_code_init(GRect bounds,
                                                              Layer *window_layer) {
    TextLayer* layer = text_layer_util_create(bounds, window_layer, "---", FONT_KEY_GOTHIC_28_BOLD);
    memcpy((void *)s_selected_code, "---", 3);
    s_selected_code[3] = '\0'; // Ensure null termination
    s_selected_name = "---";
    s_last_update_time = -1;
    s_last_re_eval_time = -1;
    return layer;
}

static inline TextLayer* clock_closest_airport_noon_time_init(GRect bounds,
                                                              Layer *window_layer) {
    TextLayer* layer = text_layer_util_create(bounds, window_layer, "--:--", FONT_KEY_LECO_42_NUMBERS);
    s_selected_offset_hours = 0.0f;
    return layer;
}

static inline void clock_closest_airport_noon_deinit(TextLayer *layer) {
    if (layer) {
        text_layer_destroy(layer);
    }
}

static inline void clock_closest_airport_noon_update(TextLayer *code_layer,
                                                     TextLayer *time_layer,
                                                     time_t     current_utc_t,
                                                     long       target_seconds_of_day) {
    if (!code_layer || !time_layer) return;

    // Skip redundant updates in the same second
    if (current_utc_t == s_last_update_time) return;
    s_last_update_time = current_utc_t;

    // Re-evaluate every 15-minutes on UTC :00, :15, :30 (excluding :45)
    struct tm *utc_tm = gmtime(&current_utc_t);
    bool needs_eval = false;
    if (utc_tm && (utc_tm->tm_min % 15 == 0) && (utc_tm->tm_min != 45) && utc_tm->tm_sec == 0) {
        if (current_utc_t != s_last_re_eval_time) {
            needs_eval = true;
        }
    } else if (s_last_re_eval_time == -1) {
        // First time after boot
        needs_eval = true;
    }

    if (needs_eval) {
        _airport_pick_new(current_utc_t, target_seconds_of_day);
    }

    // Update text layers ---------------------------------------------------
    text_layer_set_text(code_layer, s_selected_code);
    long offset_seconds = (long)(s_selected_offset_hours * 3600.0f);
    // Compute local time using utc_tm and offset to avoid second gmtime()
    long total_local_secs = (utc_tm->tm_hour * 3600L + utc_tm->tm_min * 60L + utc_tm->tm_sec + offset_seconds) % DAY_SECONDS;
    if (total_local_secs < 0) total_local_secs += DAY_SECONDS;
    int local_min = (int)((total_local_secs / 60) % 60);
    int local_sec = (int)(total_local_secs % 60);
    static char s_timebuf[10];
    snprintf(s_timebuf, sizeof(s_timebuf), "%02d:%02d", local_min, local_sec);
    text_layer_set_text(time_layer, s_timebuf);
}

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_CLOSEST_AIRPORT_NOON_H */

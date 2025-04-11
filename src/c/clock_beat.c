#include "clock_beat.h"
#include <pebble.h> // Always include pebble for device builds
#include <stdint.h> // For uint32_t, uint64_t
#include <time.h>   // For time_t
#include <stdio.h>  // For snprintf

#define HOUR_LENGTH 3600
#define DAY_LENGTH 86400

// --- Static variables specific to Beat Clock ---
static char s_beat_buffer[7];   // Buffer for Beat time string "@XXX.X\0"
static int last_beat_time = -1; // Cache the last displayed beat time (multiplied by 10)

// --- Static helper function ---
/**
 * Computes the current .beat time * 10 (0-9999)
 */
static int beat(time_t current_seconds_utc) {
  time_t now_bmt = current_seconds_utc + HOUR_LENGTH;
  // Ensure calculations use appropriate types to avoid overflow/truncation
  // Use uint64_t for multiplication before division
  uint64_t seconds_in_day = (uint64_t)(now_bmt % DAY_LENGTH);
  // Ensure division by DAY_LENGTH is done carefully
  int b = (int)((seconds_in_day * 10000ULL) / DAY_LENGTH);
  // Clamp the value just in case of edge issues
  if (b < 0) b = 0;
  if (b > 9999) b = 9999;
  return b;
}

/**
 * Calculates the Beat time string and writes it to the buffer.
 */
static void format_beat_time_string(char* buffer, size_t buffer_size, int b) {
    int beats_integer = b / 10;    // 0-999
    int beats_fraction = b % 10;   // 0-9
    snprintf(buffer, buffer_size, "@%03d.%d", beats_integer, beats_fraction);
}

// --- Pebble UI Interface Functions ---

TextLayer* clock_beat_init(GRect bounds, Layer *window_layer) {
    TextLayer* layer = text_layer_create(bounds);
    text_layer_set_background_color(layer, GColorClear);
    text_layer_set_text_color(layer, GColorBlack);
    text_layer_set_text(layer, "@--.-"); // Initial placeholder
    #define BEAT_FONT FONT_KEY_GOTHIC_24_BOLD // Keep font definitions near usage
    text_layer_set_font(layer, fonts_get_system_font(BEAT_FONT));
    text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(layer));
    return layer;
}

void clock_beat_deinit(TextLayer *layer) {
    if (layer) {
        text_layer_destroy(layer);
    }
}

void clock_beat_update(TextLayer *layer, time_t current_seconds_utc) {
    if (!layer) return;

    int b = beat(current_seconds_utc);

    // Check cache *before* formatting to prevent redundant UI updates
    if (b == last_beat_time) {
        return; // No change, don't update UI
    }

    // Use the helper to format the string into the static buffer
    format_beat_time_string(s_beat_buffer, sizeof(s_beat_buffer), b);

    text_layer_set_text(layer, s_beat_buffer);
    last_beat_time = b;
}

// Removed clock_beat_get_time_string wrapper as formatting logic moved

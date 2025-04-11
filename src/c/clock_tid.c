#include "clock_tid.h"
#include <pebble.h>
#include <string.h> // For memcpy, memset
#include <stdint.h> // For uint types
#include <stdio.h>  // For snprintf

// --- Static variables specific to TID Clock ---
static const char S32_CHAR[] = "234567abcdefghijklmnopqrstuvwxyz";
#define S32_CHAR_LEN (sizeof(S32_CHAR) - 1)
static uint64_t last_timestamp = 0;
// Buffer needs to hold 11 (timestamp) + 2 (clock ID) + 1 (null terminator) = 14 chars
static char s_tid_buffer[14]; // Buffer for TID time string "MMMSS\0"
// Remove cache for the old 5-digit display (variable is now unused)
// static int last_tid_time = -1; 

// --- Static helper functions ---

static char* s32encode_c(uint64_t i, char *buffer, size_t buffer_len) {
    if (buffer_len == 0) return NULL;
    char *ptr = buffer + buffer_len;
    if (i == 0) {
        if (ptr > buffer) { *(--ptr) = S32_CHAR[0]; return ptr; }
        else { return NULL; }
    }
    while (i > 0 && ptr > buffer) {
        uint8_t remainder = i & 31;
        i >>= 5;
        *(--ptr) = S32_CHAR[remainder];
    }
    return ptr;
}

static void createRaw_c(uint64_t timestamp, char *tid_buffer, size_t tid_buffer_len) {
    if (tid_buffer_len < 14) return;

    char ts_temp_buffer[11];
    char* encoded_ts_start = s32encode_c(timestamp, ts_temp_buffer, sizeof(ts_temp_buffer));
    size_t encoded_ts_len = encoded_ts_start ? (ts_temp_buffer + sizeof(ts_temp_buffer)) - encoded_ts_start : 0;

    memset(tid_buffer, S32_CHAR[0], 11);
    if (encoded_ts_len > 0 && encoded_ts_len <= 11) {
        memcpy(tid_buffer + (11 - encoded_ts_len), encoded_ts_start, encoded_ts_len);
    } else if (encoded_ts_len > 11) {
         memcpy(tid_buffer, encoded_ts_start + (encoded_ts_len - 11), 11);
    }

    tid_buffer[11] = S32_CHAR[0];
    tid_buffer[12] = S32_CHAR[0];
    tid_buffer[13] = '\0';
}

/**
 * Generates a TID string for the given time into the provided buffer.
 * Ensures monotonicity using a static variable internally.
 */
void clock_tid_get_string(char *tid_buffer, size_t tid_buffer_len, time_t seconds, uint16_t milliseconds) {
    if (tid_buffer_len < 14) return; // Ensure buffer is large enough

    uint64_t current_micros = (uint64_t)seconds * 1000000 + (uint64_t)milliseconds * 1000;

    if (current_micros <= last_timestamp) {
        current_micros = last_timestamp + 1;
    }
    last_timestamp = current_micros;
    createRaw_c(current_micros, tid_buffer, tid_buffer_len);
}

// --- Pebble UI Interface Functions ---

TextLayer* clock_tid_init(GRect bounds, Layer *window_layer) {
    TextLayer* layer = text_layer_create(bounds);
    text_layer_set_background_color(layer, GColorClear);
    text_layer_set_text_color(layer, GColorBlack);
    text_layer_set_text(layer, "-----"); // Initial placeholder
    #define TID_FONT FONT_KEY_GOTHIC_18_BOLD // Keep font definitions near usage
    text_layer_set_font(layer, fonts_get_system_font(TID_FONT));
    text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(layer));
    return layer;
}

void clock_tid_deinit(TextLayer *layer) {
    if (layer) {
        text_layer_destroy(layer);
    }
}

void clock_tid_update(TextLayer *layer, time_t current_seconds_utc, uint16_t current_milliseconds) {
    if (!layer) return;

    // Generate the full TID string into the static buffer
    clock_tid_get_string(s_tid_buffer, sizeof(s_tid_buffer), current_seconds_utc, current_milliseconds);

    // Update the TextLayer
    text_layer_set_text(layer, s_tid_buffer);
}

#include "clock_tid.h"
#include <pebble.h>
#include <stdint.h> // For uint types
#include "text_layer_util.h"
#include <stdlib.h> // For rand()

static const char S32_CHAR[] = "234567abcdefghijklmnopqrstuvwxyz";
static uint64_t last_timestamp;
static char s_tid_buffer[14];

// Helper to encode a value into a fixed-width base-32 string, right-to-left, padded with S32_CHAR[0]
static void encode_to_base32_fixed_width(char *out_buf, size_t width, uint64_t val) {
    for (size_t i = 0; i < width; ++i) {
        out_buf[i] = S32_CHAR[0]; // Pre-fill with padding character
    }
    size_t current_pos = width;
    while (val && current_pos) {
        out_buf[--current_pos] = S32_CHAR[val & 31];
        val >>= 5;
    }
}

// Generate monotonic TID string into tid_buffer
static void clock_tid_get_string(char *tid_buffer, size_t tid_buffer_len, time_t seconds, uint16_t milliseconds) {
    if (tid_buffer_len < sizeof(s_tid_buffer)) return;

    uint64_t current_micros = (uint64_t)seconds * 1000000 + (uint64_t)milliseconds * 1000;
    if (current_micros <= last_timestamp) {
        current_micros = last_timestamp + 1;
    }
    last_timestamp = current_micros;

    // Encode 11-char base-32 timestamp
    encode_to_base32_fixed_width(tid_buffer, 11, current_micros);

    // Encode 2-char random clock ID (0-1023)
    uint16_t cid = (uint16_t)(rand() % 1024);
    encode_to_base32_fixed_width(tid_buffer + 11, 2, cid);

    tid_buffer[13] = '\0';
}

// --- Pebble UI Interface Functions ---

TextLayer* clock_tid_init(GRect bounds, Layer *window_layer) {
    return text_layer_util_create(bounds, window_layer, "-----", FONT_KEY_GOTHIC_18_BOLD);
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

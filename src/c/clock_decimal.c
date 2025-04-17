#include "clock_decimal.h"
#include <pebble.h>
#include <stdio.h>
#include <time.h>
#include "text_layer_util.h"

static char s_buffer[10]; // Buffer for time string HH:MM:SS.ss

// Updates the decimal-time TextLayer by converting local time to French decimal time inline
void clock_decimal_update(TextLayer *text_layer, time_t system_seconds) {
    // Get local hours, minutes, seconds
    struct tm *local_tm = localtime(&system_seconds);
    uint32_t seconds_today = local_tm->tm_hour * 3600 + local_tm->tm_min * 60 + local_tm->tm_sec;

    // Compute decimal seconds: (seconds_today * 100000) / 86400
    uint32_t total_decimal_seconds = (uint32_t)(((uint64_t)seconds_today * 100000ULL) / 86400ULL);
    uint32_t dec_hour = total_decimal_seconds / 10000;
    uint32_t dec_min = (total_decimal_seconds / 100) % 100;
    uint32_t dec_sec = total_decimal_seconds % 100;

    // Format and display
    snprintf(s_buffer, sizeof(s_buffer), "%lu:%02lu:%02lu",
             (unsigned long)dec_hour,
             (unsigned long)dec_min,
             (unsigned long)dec_sec);
    text_layer_set_text(text_layer, s_buffer);
}

// Initializes the decimal time TextLayer
TextLayer* clock_decimal_init(GRect bounds, Layer *window_layer) {
    TextLayer *text_layer = text_layer_util_create(bounds, window_layer, "", FONT_KEY_GOTHIC_18_BOLD);
    
    // Initialize with placeholder or current time
    time_t temp = time(NULL);
    clock_decimal_update(text_layer, temp); 
    
    return text_layer;
}

// Deinitializes the decimal time TextLayer
void clock_decimal_deinit(TextLayer *text_layer) {
    text_layer_destroy(text_layer);
} 

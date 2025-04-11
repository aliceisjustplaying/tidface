#include "clock_decimal.h"
#include <pebble.h>
#include <stdio.h>
#include <time.h>

#define DECIMAL_FONT FONT_KEY_GOTHIC_18_BOLD

static char s_buffer[10]; // Buffer for time string HH:MM:SS.ss

// Function to calculate French Revolutionary Time using integer math
static void calculate_decimal_time(time_t system_seconds, int *decimal_hour, int *decimal_minute, int *decimal_second) {
    // Standard time has 86400 seconds per day.
    // Decimal time has 100000 decimal seconds per day.
    
    // Get the time structure for the current day
    struct tm *current_tm = localtime(&system_seconds);
    
    // Calculate standard seconds elapsed since midnight
    uint32_t seconds_today = current_tm->tm_hour * 3600 + current_tm->tm_min * 60 + current_tm->tm_sec;
    
    // Convert standard seconds today to decimal seconds today using 64-bit integer math
    // total_decimal_seconds = (seconds_today * 100000) / 86400
    uint64_t total_decimal_seconds = ((uint64_t)seconds_today * 100000ULL) / 86400ULL;
    
    // Calculate decimal hours, minutes, seconds using integer division and modulo
    *decimal_hour = (int)(total_decimal_seconds / 10000ULL);
    uint64_t remainder = total_decimal_seconds % 10000ULL;
    *decimal_minute = (int)(remainder / 100ULL);
    *decimal_second = (int)(remainder % 100ULL);
}

// Updates the decimal time TextLayer
void clock_decimal_update(TextLayer *text_layer, time_t system_seconds) {
    int decimal_hour, decimal_minute, decimal_second;
    calculate_decimal_time(system_seconds, &decimal_hour, &decimal_minute, &decimal_second);
    
    // Format the time string (e.g., H:MM:SS)
    snprintf(s_buffer, sizeof(s_buffer), "%d:%02d:%02d", decimal_hour, decimal_minute, decimal_second);
    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Decimal Update: %s", s_buffer);
    // Display this time on the TextLayer
    text_layer_set_text(text_layer, s_buffer);
}

// Initializes the decimal time TextLayer
TextLayer* clock_decimal_init(GRect bounds, Layer *window_layer) {
    TextLayer *text_layer = text_layer_create(bounds);
    text_layer_set_background_color(text_layer, GColorClear);
    text_layer_set_text_color(text_layer, GColorBlack);
    text_layer_set_font(text_layer, fonts_get_system_font(DECIMAL_FONT));
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(text_layer));
    
    // Initialize with placeholder or current time
    time_t temp = time(NULL);
    clock_decimal_update(text_layer, temp); 
    
    return text_layer;
}

// Deinitializes the decimal time TextLayer
void clock_decimal_deinit(TextLayer *text_layer) {
    text_layer_destroy(text_layer);
} 

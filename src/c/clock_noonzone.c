#include "clock_noonzone.h"
#include <pebble.h>
#include <time.h>   // For time_t, struct tm, gmtime
#include <string.h> // For NULL definition (consider stddef.h?)
#include <stdio.h>  // For snprintf

// --- Static variables specific to Noon Zone Clock ---
static char s_noonzone_buffer[16]; // Buffer for "NAME:MM:SS\0"
static int last_noonzone_update_secs = -1;
static int last_utc_hour = -1;
static const char *last_zone_name_ptr = NULL;

// --- Static helper function ---
/**
 * Gets the military timezone name for the longitude where it is currently noon.
 * Includes caching based on UTC hour.
 */
static const char* get_noon_zone_name(int utc_hour) {
    if (utc_hour == last_utc_hour && last_zone_name_ptr != NULL) {
        return last_zone_name_ptr;
    }

    const char *name = "???";
    switch(utc_hour){
        case 12: name="ZULU"; break;
        case 11: name="ALPHA"; break;
        case 10: name="BRAVO"; break;
        case 9:  name="CHARLIE"; break;
        case 8:  name="DELTA"; break;
        case 7:  name="ECHO"; break;
        case 6:  name="FOXTROT"; break;
        case 5:  name="GOLF"; break;
        case 4:  name="HOTEL"; break;
        case 3:  name="JULIET"; break;
        case 2:  name="KILO"; break;
        case 1:  name="LIMA"; break;
        case 0:  name="MIKE"; break;
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
        // default: name="???"; // Already initialized
    }
    last_utc_hour = utc_hour;
    last_zone_name_ptr = name;
    return name;
}

// --- Pebble UI Interface Functions ---

TextLayer* clock_noonzone_init(GRect bounds, Layer *window_layer) {
    TextLayer* layer = text_layer_create(bounds);
    text_layer_set_background_color(layer, GColorClear);
    text_layer_set_text_color(layer, GColorBlack);
    text_layer_set_text(layer, "ZONE:--:--"); // Initial placeholder
    #define NOONZONE_FONT FONT_KEY_GOTHIC_18_BOLD // Keep font definitions near usage
    text_layer_set_font(layer, fonts_get_system_font(NOONZONE_FONT));
    text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(layer));
    return layer;
}

void clock_noonzone_deinit(TextLayer *layer) {
    if (layer) {
        text_layer_destroy(layer);
    }
}

void clock_noonzone_update(TextLayer *layer, time_t current_seconds_utc) {
    if (!layer) return;

    // Check cache first to avoid unnecessary calculation/string formatting
    if (current_seconds_utc == last_noonzone_update_secs) {
        return;
    }

    // Perform calculations
    struct tm *utc_tm = gmtime(&current_seconds_utc);
    if (!utc_tm) {
        text_layer_set_text(layer, "ERR:TIME"); // Show error on layer
        last_noonzone_update_secs = current_seconds_utc; // Cache update time even on error
        return;
    }

    const char *zone_name = get_noon_zone_name(utc_tm->tm_hour);

    // Format string into static buffer
    snprintf(s_noonzone_buffer, sizeof(s_noonzone_buffer),
             "%s:%02d:%02d",
             zone_name,
             utc_tm->tm_min,
             utc_tm->tm_sec);

    text_layer_set_text(layer, s_noonzone_buffer);
    last_noonzone_update_secs = current_seconds_utc;
}

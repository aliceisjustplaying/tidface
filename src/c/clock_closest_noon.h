#ifndef CLOCK_CLOSEST_NOON_H
#define CLOCK_CLOSEST_NOON_H

#include <pebble.h> // Pebble time_t definition and UI types
#include <stddef.h> // For size_t

// Initializes the Closest Noon clock layer
TextLayer* clock_closest_noon_init(GRect bounds, Layer *window_layer);

// Deinitializes the Closest Noon clock layer
void clock_closest_noon_deinit(TextLayer *layer);

// Updates the Closest Noon clock layer
void clock_closest_noon_update(TextLayer *layer, time_t current_seconds_utc);

#endif // CLOCK_CLOSEST_NOON_H

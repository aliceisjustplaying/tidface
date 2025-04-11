#ifndef CLOCK_NOONZONE_H
#define CLOCK_NOONZONE_H

#include <pebble.h>
#include <stddef.h> // For size_t

// Initializes the Noon Zone clock layer
TextLayer* clock_noonzone_init(GRect bounds, Layer *window_layer);

// Deinitializes the Noon Zone clock layer
void clock_noonzone_deinit(TextLayer *layer);

// Updates the Noon Zone clock layer
void clock_noonzone_update(TextLayer *layer, time_t current_seconds_utc);

#endif // CLOCK_NOONZONE_H

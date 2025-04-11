#ifndef CLOCK_BEAT_H
#define CLOCK_BEAT_H

#include <pebble.h>
#include <stddef.h> // For size_t

// Initializes the Beat clock layer
TextLayer* clock_beat_init(GRect bounds, Layer *window_layer);

// Deinitializes the Beat clock layer
void clock_beat_deinit(TextLayer *layer);

// Updates the Beat clock layer
void clock_beat_update(TextLayer *layer, time_t current_seconds_utc);

#endif // CLOCK_BEAT_H

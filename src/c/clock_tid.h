#ifndef CLOCK_TID_H
#define CLOCK_TID_H

#include <pebble.h>
#include <stddef.h> // For size_t
#include <stdint.h> // For uint16_t

// Initializes the TID clock layer
TextLayer* clock_tid_init(GRect bounds, Layer *window_layer);

// Deinitializes the TID clock layer
void clock_tid_deinit(TextLayer *layer);

// Updates the TID clock layer
void clock_tid_update(TextLayer *layer, time_t current_seconds_utc, uint16_t current_milliseconds);

#endif // CLOCK_TID_H

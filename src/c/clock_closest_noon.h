#ifndef CLOCK_CLOSEST_NOON_H
#define CLOCK_CLOSEST_NOON_H

#include <pebble.h> // Pebble time_t definition and UI types
#include <stddef.h> // For size_t

// Initializes the city name TextLayer for the Closest Noon clock
TextLayer* clock_closest_noon_city_init(GRect bounds, Layer *window_layer);

// Initializes the hero time TextLayer for the Closest Noon clock
TextLayer* clock_closest_noon_time_init(GRect bounds, Layer *window_layer);

// Deinitializes a Closest Noon TextLayer (city or time)
void clock_closest_noon_deinit(TextLayer *layer);

// Updates the Closest Noon city and time TextLayers
void clock_closest_noon_update(TextLayer *city_layer, TextLayer *time_layer, time_t current_seconds_utc);

#endif // CLOCK_CLOSEST_NOON_H

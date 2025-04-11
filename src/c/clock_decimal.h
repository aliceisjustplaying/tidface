#pragma once

#include <pebble.h>

// Initializes the decimal time TextLayer
TextLayer* clock_decimal_init(GRect bounds, Layer *window_layer);

// Updates the decimal time TextLayer
void clock_decimal_update(TextLayer *text_layer, time_t system_seconds);

// Deinitializes the decimal time TextLayer
void clock_decimal_deinit(TextLayer *text_layer); 

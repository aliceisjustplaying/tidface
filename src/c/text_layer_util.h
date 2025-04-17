#pragma once

#include <pebble.h>

// A helper to create a TextLayer configured with standard styling and add it to a parent layer.
//   bounds: frame of the TextLayer
//   parent: parent Layer to add this TextLayer to
//   init_text: initial text to display
//   font_key: key of the system font, e.g. FONT_KEY_GOTHIC_24_BOLD
static inline TextLayer* text_layer_util_create(GRect bounds, Layer* parent, const char* init_text, const char* font_key) {
    TextLayer* layer = text_layer_create(bounds);
    text_layer_set_background_color(layer, GColorClear);
    text_layer_set_text_color(layer, GColorBlack);
    text_layer_set_text(layer, init_text);
    text_layer_set_font(layer, fonts_get_system_font(font_key));
    text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer_add_child(parent, text_layer_get_layer(layer));
    return layer;
} 

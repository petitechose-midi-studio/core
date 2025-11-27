#pragma once

#include <lvgl.h>
#include <cstdint>

struct FontRegistry {
    // Generic fonts
    lv_font_t* inter_14_light = nullptr;
    lv_font_t* inter_14_regular = nullptr;
    lv_font_t* inter_14_medium = nullptr;
    lv_font_t* inter_14_semibold = nullptr;
    lv_font_t* inter_14_bold = nullptr;

    // Semantic aliases
    lv_font_t* parameter_label = nullptr;
    lv_font_t* parameter_value_label = nullptr;
    lv_font_t* tempo_label = nullptr;
    lv_font_t* list_item_label = nullptr;
    lv_font_t* splash_title = nullptr;
    lv_font_t* splash_version = nullptr;
};

extern FontRegistry fonts;

// Plugin API
void register_font(lv_font_t** font_ptr, const uint8_t* buffer, uint32_t len);
void load_plugin_fonts();
void free_fonts();

// Incremental loading API
void fonts_register_core();
bool fonts_load_essential();
uint8_t fonts_get_pending_count();
bool fonts_load_next(const char** outFontName = nullptr);

#pragma once

#include <lvgl.h>

struct FontRegistry {
    lv_font_t* parameter_label = nullptr;
    lv_font_t* parameter_value_label = nullptr;
    lv_font_t* device_label = nullptr;
    lv_font_t* page_label = nullptr;
    lv_font_t* tempo_label = nullptr;
    lv_font_t* list_item_label = nullptr;
    lv_font_t* splash_title = nullptr;
    lv_font_t* splash_version = nullptr;
};

extern FontRegistry fonts;

void load_fonts();
void free_fonts();

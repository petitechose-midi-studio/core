#pragma once

#include <lvgl.h>

/**
 * @brief Core font registry
 * Contains fonts used by core UI components
 */
struct FontRegistry {
    lv_font_t* parameter_label = nullptr;       // ParameterWidgets
    lv_font_t* parameter_value_label = nullptr; // ParameterListWidget
    lv_font_t* tempo_label = nullptr;           // ListOverlay
    lv_font_t* list_item_label = nullptr;       // ListOverlay
    lv_font_t* splash_title = nullptr;          // SplashScreenView
    lv_font_t* splash_version = nullptr;        // SplashScreenView
};

extern FontRegistry fonts;

void load_fonts();
void free_fonts();

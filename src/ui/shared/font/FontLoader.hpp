#pragma once

#include <cstdint>

#include <lvgl.h>

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
void registerFont(lv_font_t** fontPtr, const uint8_t* buffer, uint32_t len);
void loadPluginFonts();
void freeFonts();

// Incremental loading API
void fontsRegisterCore();
bool fontsLoadEssential();
uint8_t fontsGetPendingCount();
bool fontsLoadNext(const char** outFontName = nullptr);

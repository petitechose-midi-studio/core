#pragma once

#include <lvgl.h>

/**
 * @brief Core font registry
 * Contains generic fonts that plugins can reference by name.
 * Also contains semantic aliases for core UI components.
 */
struct FontRegistry {
    // Generic Inter Display 14px fonts (plugins reference these)
    lv_font_t* inter_14_light = nullptr;
    lv_font_t* inter_14_regular = nullptr;
    lv_font_t* inter_14_medium = nullptr;
    lv_font_t* inter_14_semibold = nullptr;
    lv_font_t* inter_14_bold = nullptr;

    // Semantic aliases for core UI components
    lv_font_t* parameter_label = nullptr;
    lv_font_t* parameter_value_label = nullptr;
    lv_font_t* tempo_label = nullptr;
    lv_font_t* list_item_label = nullptr;
    lv_font_t* splash_title = nullptr;
    lv_font_t* splash_version = nullptr;
};

extern FontRegistry fonts;

/**
 * @brief Register a plugin font to be loaded from buffer
 * Call this in Plugin::loadResources()
 */
void register_font(lv_font_t** font_ptr, const uint8_t* buffer, uint32_t len);

/**
 * @brief Load core fonts (called by ViewManager at startup)
 */
void load_fonts();

/**
 * @brief Load plugin fonts (called by PluginManager after loadResources)
 */
void load_plugin_fonts();

/**
 * @brief Free all loaded fonts
 */
void free_fonts();

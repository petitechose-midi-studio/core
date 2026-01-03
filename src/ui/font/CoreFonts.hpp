#pragma once

/**
 * @file CoreFonts.hpp
 * @brief Core font registry for the application
 *
 * Defines the font storage structure and entry descriptors.
 * Fonts are loaded from flash into RAM on demand.
 */

#include <lvgl.h>
#include <oc/ui/lvgl/FontLoader.hpp>

/**
 * @brief Core font storage
 *
 * Generic fonts are loaded from binary data.
 * Semantic aliases are linked after loading via linkCoreFontAliases().
 */
struct CoreFonts {
    // Generic fonts (loaded from binaries)
    lv_font_t* inter_13_medium = nullptr;
    lv_font_t* inter_13_bold = nullptr;
    lv_font_t* inter_14_light = nullptr;
    lv_font_t* inter_14_regular = nullptr;
    lv_font_t* inter_14_medium = nullptr;
    lv_font_t* inter_14_semibold = nullptr;
    lv_font_t* inter_14_bold = nullptr;

    // Splash fonts (essential)
    lv_font_t* splash_title = nullptr;
    lv_font_t* splash_version = nullptr;

    // Semantic aliases (set by linkCoreFontAliases)
    lv_font_t* parameter_label = nullptr;
    lv_font_t* parameter_value_label = nullptr;
    lv_font_t* tempo_label = nullptr;
    lv_font_t* list_item_label = nullptr;
};

/// Global core fonts instance
extern CoreFonts fonts;

/// Font entry descriptors (stored in flash)
extern const oc::ui::lvgl::font::Entry CORE_FONT_ENTRIES[];

/// Number of core font entries
extern const size_t CORE_FONT_COUNT;

/**
 * @brief Link semantic font aliases
 *
 * Call after fonts are loaded to set up semantic aliases
 * (parameter_label, tempo_label, etc.)
 */
void linkCoreFontAliases();

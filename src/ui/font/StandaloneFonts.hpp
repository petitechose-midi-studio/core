#pragma once

/**
 * @file StandaloneFonts.hpp
 * @brief Standalone mode font configuration
 */

/**
 * @brief Standalone icon font registry
 *
 * Icon fonts specific to standalone mode.
 * Text fonts use CoreFonts (fonts.inter_14_*).
 */

#include <lvgl.h>
#include <oc/ui/lvgl/FontLoader.hpp>

struct StandaloneFonts {
    lv_font_t* icons_12 = nullptr;
    lv_font_t* icons_14 = nullptr;
    lv_font_t* icons_16 = nullptr;
};

extern StandaloneFonts standalone_fonts;

/// Font entry descriptors (stored in flash)
extern const oc::ui::lvgl::font::Entry STANDALONE_FONT_ENTRIES[];

/// Number of standalone font entries
extern const size_t STANDALONE_FONT_COUNT;

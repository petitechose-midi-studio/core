#include "StandaloneFonts.hpp"

#include <Arduino.h>

#include "data/standalone_icons_12.c.inc"
#include "data/standalone_icons_14.c.inc"
#include "data/standalone_icons_16.c.inc"

StandaloneFonts standalone_fonts;

const oc::ui::lvgl::font::Entry STANDALONE_FONT_ENTRIES[] = {
    {&standalone_fonts.icons_12, standalone_icons_12_bin,
     standalone_icons_12_bin_len, "StandaloneIcons12", false},
    {&standalone_fonts.icons_14, standalone_icons_14_bin,
     standalone_icons_14_bin_len, "StandaloneIcons14", false},
    {&standalone_fonts.icons_16, standalone_icons_16_bin,
     standalone_icons_16_bin_len, "StandaloneIcons16", false},
};

const size_t STANDALONE_FONT_COUNT =
    sizeof(STANDALONE_FONT_ENTRIES) / sizeof(STANDALONE_FONT_ENTRIES[0]);

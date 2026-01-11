#include "CoreFonts.hpp"

#include <config/PlatformCompat.hpp>

// Font binary data (stored in flash via PROGMEM)
#include "data/interdisplay_bold_13.c.inc"
#include "data/interdisplay_bold_14.c.inc"
#include "data/interdisplay_bold_20.c.inc"
#include "data/interdisplay_light_14.c.inc"
#include "data/interdisplay_medium_13.c.inc"
#include "data/interdisplay_medium_14.c.inc"
#include "data/interdisplay_regular_14.c.inc"
#include "data/interdisplay_semibold_14.c.inc"
#include "data/jetbrainsmononl_medium_13.c.inc"

// =============================================================================
// Global Instances
// =============================================================================

CoreFonts fonts;

const oc::ui::lvgl::font::Entry CORE_FONT_ENTRIES[] = {
    // Essential (splash) - loaded first during boot
    {&fonts.splash_title, interdisplay_bold_20_bin,
     interdisplay_bold_20_bin_len, "SplashTitle", true},
    {&fonts.splash_version, jetbrainsmononl_medium_13_bin,
     jetbrainsmononl_medium_13_bin_len, "SplashVersion", true},

    // Generic fonts - 13px
    {&fonts.inter_13_medium, interdisplay_medium_13_bin,
     interdisplay_medium_13_bin_len, "Medium13", false},
    {&fonts.inter_13_bold, interdisplay_bold_13_bin,
     interdisplay_bold_13_bin_len, "Bold13", false},

    // Generic fonts - 14px
    {&fonts.inter_14_light, interdisplay_light_14_bin,
     interdisplay_light_14_bin_len, "Light", false},
    {&fonts.inter_14_regular, interdisplay_regular_14_bin,
     interdisplay_regular_14_bin_len, "Regular", false},
    {&fonts.inter_14_medium, interdisplay_medium_14_bin,
     interdisplay_medium_14_bin_len, "Medium", false},
    {&fonts.inter_14_semibold, interdisplay_semibold_14_bin,
     interdisplay_semibold_14_bin_len, "SemiBold", false},
    {&fonts.inter_14_bold, interdisplay_bold_14_bin,
     interdisplay_bold_14_bin_len, "Bold", false},
};

const size_t CORE_FONT_COUNT = sizeof(CORE_FONT_ENTRIES) / sizeof(CORE_FONT_ENTRIES[0]);

void linkCoreFontAliases() {
    fonts.parameter_label = fonts.inter_14_regular;
    fonts.parameter_value_label = fonts.inter_14_medium;
    fonts.tempo_label = fonts.inter_14_semibold;
    fonts.list_item_label = fonts.inter_14_semibold;
}

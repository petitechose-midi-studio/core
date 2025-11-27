#include "FontLoader.hpp"

#include <Arduino.h>
#include <vector>

// Inter Display fonts
#include "data/interdisplay_light_14.c.inc"
#include "data/interdisplay_regular_14.c.inc"
#include "data/interdisplay_medium_13.c.inc"
#include "data/interdisplay_medium_14.c.inc"
#include "data/interdisplay_semibold_14.c.inc"
#include "data/interdisplay_bold_13.c.inc"
#include "data/interdisplay_bold_14.c.inc"
#include "data/interdisplay_bold_20.c.inc"
#include "data/jetbrainsmononl_medium_13.c.inc"

struct FontDescriptor {
    lv_font_t** font_ptr;
    const uint8_t* buffer_data;
    uint32_t buffer_len;
};

static std::vector<FontDescriptor> registered_fonts;

FontRegistry fonts;

void register_font(lv_font_t** font_ptr, const uint8_t* buffer, uint32_t len) {
    registered_fonts.push_back({font_ptr, buffer, len});
}

static void register_core_fonts() {
    // Generic Inter Display 14px fonts (for plugins to reference)
    register_font(&fonts.inter_14_light, interdisplay_light_14_bin, interdisplay_light_14_bin_len);
    register_font(&fonts.inter_14_regular, interdisplay_regular_14_bin, interdisplay_regular_14_bin_len);
    register_font(&fonts.inter_14_medium, interdisplay_medium_14_bin, interdisplay_medium_14_bin_len);
    register_font(&fonts.inter_14_semibold, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len);
    register_font(&fonts.inter_14_bold, interdisplay_bold_14_bin, interdisplay_bold_14_bin_len);

    // Core UI component fonts (semantic aliases)
    register_font(&fonts.parameter_label, interdisplay_regular_14_bin, interdisplay_regular_14_bin_len);
    register_font(&fonts.parameter_value_label, interdisplay_medium_13_bin, interdisplay_medium_13_bin_len);
    register_font(&fonts.tempo_label, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len);
    register_font(&fonts.list_item_label, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len);
    register_font(&fonts.splash_title, interdisplay_bold_20_bin, interdisplay_bold_20_bin_len);
    register_font(&fonts.splash_version, jetbrainsmononl_medium_13_bin, jetbrainsmononl_medium_13_bin_len);
}

void load_fonts() {
    register_core_fonts();
    for (const FontDescriptor& desc : registered_fonts) {
        *(desc.font_ptr) = lv_binfont_create_from_buffer(
            (void*)desc.buffer_data, desc.buffer_len);
    }
}

void load_plugin_fonts() {
    for (const FontDescriptor& desc : registered_fonts) {
        if (*(desc.font_ptr) == nullptr) {
            *(desc.font_ptr) = lv_binfont_create_from_buffer(
                (void*)desc.buffer_data, desc.buffer_len);
        }
    }
}

void free_fonts() {
    for (const FontDescriptor& desc : registered_fonts) {
        if (*(desc.font_ptr)) {
            lv_binfont_destroy(*(desc.font_ptr));
            *(desc.font_ptr) = nullptr;
        }
    }
    registered_fonts.clear();
}

#include "FontLoader.hpp"

#include <Arduino.h>
#include <vector>

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
    const uint8_t* buffer;
    uint32_t len;
    const char* name;
    bool essential;
};

static std::vector<FontDescriptor> registered_fonts;
static size_t next_index = 0;
static bool core_registered = false;

FontRegistry fonts;

void register_font(lv_font_t** font_ptr, const uint8_t* buffer, uint32_t len) {
    registered_fonts.push_back({font_ptr, buffer, len, "plugin", false});
}

static void add_font(lv_font_t** ptr, const uint8_t* buf, uint32_t len, const char* name, bool essential) {
    registered_fonts.push_back({ptr, buf, len, name, essential});
}

void fonts_register_core() {
    if (core_registered) return;
    core_registered = true;

    // Essential (splash)
    add_font(&fonts.splash_title, interdisplay_bold_20_bin, interdisplay_bold_20_bin_len, "Title", true);
    add_font(&fonts.splash_version, jetbrainsmononl_medium_13_bin, jetbrainsmononl_medium_13_bin_len, "Version", true);

    // Generic
    add_font(&fonts.inter_14_light, interdisplay_light_14_bin, interdisplay_light_14_bin_len, "Light", false);
    add_font(&fonts.inter_14_regular, interdisplay_regular_14_bin, interdisplay_regular_14_bin_len, "Regular", false);
    add_font(&fonts.inter_14_medium, interdisplay_medium_14_bin, interdisplay_medium_14_bin_len, "Medium", false);
    add_font(&fonts.inter_14_semibold, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len, "SemiBold", false);
    add_font(&fonts.inter_14_bold, interdisplay_bold_14_bin, interdisplay_bold_14_bin_len, "Bold", false);

    // Semantic aliases
    add_font(&fonts.parameter_label, interdisplay_regular_14_bin, interdisplay_regular_14_bin_len, "Param", false);
    add_font(&fonts.parameter_value_label, interdisplay_medium_13_bin, interdisplay_medium_13_bin_len, "Value", false);
    add_font(&fonts.tempo_label, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len, "Tempo", false);
    add_font(&fonts.list_item_label, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len, "List", false);

    next_index = 0;
}

bool fonts_load_essential() {
    if (!core_registered) fonts_register_core();

    for (auto& f : registered_fonts) {
        if (f.essential && *f.font_ptr == nullptr) {
            *f.font_ptr = lv_binfont_create_from_buffer((void*)f.buffer, f.len);
        }
    }

    // Skip essential fonts
    next_index = 0;
    while (next_index < registered_fonts.size() && registered_fonts[next_index].essential) {
        next_index++;
    }

    return true;
}

uint8_t fonts_get_pending_count() {
    uint8_t count = 0;
    for (size_t i = next_index; i < registered_fonts.size(); i++) {
        if (*registered_fonts[i].font_ptr == nullptr) count++;
    }
    return count;
}

bool fonts_load_next(const char** outFontName) {
    while (next_index < registered_fonts.size()) {
        auto& f = registered_fonts[next_index++];
        if (*f.font_ptr != nullptr) continue;

        *f.font_ptr = lv_binfont_create_from_buffer((void*)f.buffer, f.len);
        if (outFontName) *outFontName = f.name;
        return true;
    }
    return false;
}

void load_plugin_fonts() {
    for (auto& f : registered_fonts) {
        if (*f.font_ptr == nullptr) {
            *f.font_ptr = lv_binfont_create_from_buffer((void*)f.buffer, f.len);
        }
    }
}

void free_fonts() {
    for (auto& f : registered_fonts) {
        if (*f.font_ptr) {
            lv_binfont_destroy(*f.font_ptr);
            *f.font_ptr = nullptr;
        }
    }
    registered_fonts.clear();
    next_index = 0;
    core_registered = false;
}

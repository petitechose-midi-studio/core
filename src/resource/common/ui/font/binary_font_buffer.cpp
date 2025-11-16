#include "binary_font_buffer.hpp"

#include <Arduino.h>

#include "data/interdisplay_bold_13_bold_4bpp.c.inc"
#include "data/interdisplay_bold_14_bold_4bpp.c.inc"
#include "data/interdisplay_light_14_light_4bpp.c.inc"
#include "data/interdisplay_medium_13_4bpp.c.inc"
#include "data/interdisplay_medium_14_4bpp.c.inc"
#include "data/interdisplay_bold_20_bold_4bpp.c.inc"
#include "data/jetbrainsmono_medium_13_4bpp.c.inc"

struct FontDescriptor {
    lv_font_t** font_ptr;
    const uint8_t* buffer_data;
    const uint32_t* buffer_len;
};

#define FONT_ENTRY(font_member, buffer_name) \
    {&fonts.font_member, buffer_name##_bin, &buffer_name##_bin_len}

static const FontDescriptor font_descriptors[] = {
    FONT_ENTRY(parameter_label, interdisplay_bold_13_bold_4bpp),
    FONT_ENTRY(parameter_value_label, interdisplay_medium_13_4bpp),
    FONT_ENTRY(device_label, interdisplay_medium_14_4bpp),
    FONT_ENTRY(page_label, interdisplay_light_14_light_4bpp),
    FONT_ENTRY(tempo_label, interdisplay_bold_14_bold_4bpp),
    FONT_ENTRY(list_item_label, interdisplay_medium_13_4bpp),
    FONT_ENTRY(splash_title, interdisplay_bold_20_bold_4bpp),
    FONT_ENTRY(splash_version, jetbrainsmono_medium_13_4bpp),
};

static constexpr size_t FONT_COUNT = sizeof(font_descriptors) / sizeof(FontDescriptor);

FontRegistry fonts;

void load_fonts() {
    for (size_t i = 0; i < FONT_COUNT; i++) {
        const FontDescriptor& desc = font_descriptors[i];
        *(desc.font_ptr) = lv_binfont_create_from_buffer(
            (void*)desc.buffer_data,
            *(desc.buffer_len)
        );
    }
}

void free_fonts() {
    for (size_t i = 0; i < FONT_COUNT; i++) {
        const FontDescriptor& desc = font_descriptors[i];
        if (*(desc.font_ptr)) {
            lv_binfont_destroy(*(desc.font_ptr));
            *(desc.font_ptr) = nullptr;
        }
    }
}

#undef FONT_ENTRY

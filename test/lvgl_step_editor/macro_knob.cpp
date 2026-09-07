#include <array>
#include <cassert>
#include <cstdio>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/widget/MacroKnobWidget.hpp"

CoreFonts fonts;

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_medium = fonts.inter_13_bold = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    unsigned flushes = 0, checks = 0;
    lv_display_set_user_data(display, &flushes);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) {
        ++*static_cast<unsigned*>(lv_display_get_user_data(d));
        lv_display_flush_ready(d);
    });
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent);
    lv_obj_set_pos(parent, 50, 30);
    uint64_t hash = 14695981039346656037ULL;
    {
        core::ui::MacroKnobWidget knob(parent);
        assert(knob.valid());
        knob.setConfig(127);
        for (const auto size : {lv_point_t{72, 84}, lv_point_t{63, 71}, lv_point_t{88, 96}}) {
            lv_obj_set_size(parent, size.x, size.y);
            for (const bool modulating : {false, true}) {
                knob.setSourceIndicators(true, true, modulating, modulating, false, modulating ? 3 : 0);
                lv_refr_now(display);
                for (const float base : {0.0f, 0.5f, 1.0f}) {
                    for (const float output : {0.0f, 0.25f, 1.0f, 0.75f, 0.5f, 0.0f}) {
                        const auto update = [&] {
                            knob.setResolvedComponents(base, output,
                                                       output == 0.0f, output == 1.0f);
                        };
                        update();
                        lv_refr_now(display);
                        const auto partial = pixels;
                        flushes = 0;
                        update();
                        lv_refr_now(display);
                        assert(flushes == 0);
                        assert(pixels == partial);
                        lv_obj_invalidate(lv_screen_active());
                        lv_refr_now(display);
                        assert(pixels == partial);
                        for (const auto pixel : pixels)
                            hash = ((hash ^ (pixel & 255U)) * 1099511628211ULL ^ (pixel >> 8U)) * 1099511628211ULL;
                        ++checks;
                    }
                }
            }
        }
        knob.setSlotState(false, true);
        lv_refr_now(display);
        knob.setResolvedComponents(0.2f, 0.5f, false, false);
        knob.setSlotState(true, false);
        lv_refr_now(display);
        const auto restored = pixels;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(restored == pixels);
        assert(hash == 0x8000b04773fdc7e5ULL);
        uint64_t configHash = 14695981039346656037ULL;
        for (unsigned cc = 0; cc <= 127; ++cc) {
            knob.setConfig(static_cast<uint8_t>(cc));
            knob.setFocused((cc & 1U) != 0);
            knob.setSelectionState((cc & 2U) != 0, false, false, false);
            lv_refr_now(display);
            const auto partial = pixels;
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            assert(pixels == partial);
            for (const auto pixel : pixels)
                configHash = ((configHash ^ (pixel & 255U)) * 1099511628211ULL ^ (pixel >> 8U)) * 1099511628211ULL;
        }
        std::printf("config_checks=128 rgb565=%016llx\n", static_cast<unsigned long long>(configHash));
        assert(configHash == 0xd6ea7f8b16244926ULL);
        std::printf("checks=%u rgb565=%016llx widget_bytes=%zu\n", checks,
                    static_cast<unsigned long long>(hash), sizeof(knob));
    }
    lv_display_delete(display);
    lv_deinit();
}

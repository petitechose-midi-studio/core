#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/sequencer/StepPropertySelectionOverlay.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main(int argc, char** argv) {
    const bool reference = argc > 1 && std::strcmp(argv[1], "--reference") == 0;
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_medium = fonts.inter_13_bold = font;
    fonts.inter_14_medium = fonts.inter_14_semibold = font;
    standalone_fonts.icons_16 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    unsigned flushes = 0;
    lv_display_set_user_data(display, &flushes);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) {
        ++*static_cast<unsigned*>(lv_display_get_user_data(d));
        lv_display_flush_ready(d);
    });
    {
        core::ui::StepPropertySelectionOverlay overlay(lv_screen_active());
        core::ui::StepPropertySelectionOverlayProps props{
            .visible = true, .customContent = true, .icon = "A", .label = "Length", .value = "8"
        };
        auto* row = lv_obj_get_child(overlay.getElement(), 0);
        auto* icon = lv_obj_get_child(row, 0);
        auto* column = lv_obj_get_child(row, 1);
        auto* label = lv_obj_get_child(column, 0);
        auto* value = lv_obj_get_child(column, 1);
        const auto verify = [&] {
            overlay.render(props);
            lv_refr_now(display);
            assert(std::strcmp(lv_label_get_text(label), props.label ? props.label : "") == 0);
            assert(std::strcmp(lv_label_get_text(value), props.value ? props.value : "") == 0);
            const auto expected = pixels;
            flushes = 0;
            overlay.render(props);
            lv_refr_now(display);
            std::printf("lengths=%zu/%zu repeated_flushes=%u\n",
                std::strlen(lv_label_get_text(label)), std::strlen(lv_label_get_text(value)), flushes);
            if (!reference) assert(flushes == 0);
            assert(pixels == expected);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            assert(pixels == expected);
        };
        for (const auto* text : {"Short", "123456789012345", "123456789012345678901234567890_A",
                 "123456789012345678901234567890_B", "", static_cast<const char*>(nullptr)}) {
            props.label = props.value = text;
            verify();
        }
        props.label = "Length";
        props.value = "8";
        verify();
        // A value-only edit must not invalidate the unchanged sibling labels.
        unsigned siblingChanges = 0;
        for (auto* sibling : {icon, label}) {
            lv_obj_add_event_cb(sibling, [](lv_event_t* event) {
                ++*static_cast<unsigned*>(lv_event_get_user_data(event));
            }, LV_EVENT_GET_SELF_SIZE, &siblingChanges);
        }
        props.value = "9";
        overlay.render(props);
        std::printf("value_only_sibling_size_queries=%u\n", siblingChanges);
        if (!reference) assert(siblingChanges == 0);
        verify();
        char mutableIcon[] = "A";
        props.icon = mutableIcon;
        verify();
        mutableIcon[0] = 'B';
        overlay.render(props);
        std::printf("mutable_icon=%s\n", lv_label_get_text(icon));
        if (!reference) assert(std::strcmp(lv_label_get_text(icon), "B") == 0);
        props.visible = false;
        overlay.render(props);
        assert(lv_obj_has_flag(overlay.getElement(), LV_OBJ_FLAG_HIDDEN));
        props.visible = true;
        verify();
        std::printf("overlay_object_bytes=%zu\n", sizeof(overlay));
    }
    lv_display_delete(display);
    lv_deinit();
}

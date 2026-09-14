#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/time/Time.hpp>
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/theme/StandaloneTheme.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;
static uint32_t now_ms = 1500;

int main() {
    using namespace core::ui;
    lv_init();
    oc::time::setProvider([] { return now_ms; });
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_13_medium = fonts.inter_13_bold = font;
    standalone_fonts.icons_12 = standalone_fonts.icons_14 = standalone_fonts.icons_16 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, 320, 240);
    {
        auto* icon = lv_label_create(parent);
        lv_font_t alternate = *font;
        unsigned styleChanges = 0;
        lv_obj_add_event_cb(icon, [](lv_event_t* event) {
            ++*static_cast<unsigned*>(lv_event_get_user_data(event));
        }, LV_EVENT_STYLE_CHANGED, &styleChanges);
        standalone::icons::set(icon, "X");
        lv_style_value_t localFont{};
        assert(lv_obj_get_local_style_prop(icon, LV_STYLE_TEXT_FONT, &localFont, 0) == LV_STYLE_RES_FOUND);
        assert(localFont.ptr == font);
        lv_refr_now(display);
        styleChanges = 0;
        standalone::icons::set(icon, "X");
        assert(styleChanges == 0); // A repeated icon must not reapply its font.
        lv_obj_set_style_text_font(parent, &alternate, 0);
        assert(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == font);
        standalone_fonts.icons_16 = &alternate;
        standalone::icons::set(icon, "Y", standalone::icons::Size::L);
        assert(lv_obj_get_style_text_font(icon, LV_PART_MAIN) == &alternate);
        assert(std::strcmp(lv_label_get_text(icon), "Y") == 0);
        char mutableText[] = "Z";
        standalone::icons::set(icon, mutableText);
        mutableText[0] = 'Q';
        assert(std::strcmp(lv_label_get_text(icon), "Z") == 0);
        standalone::icons::set(icon, mutableText);
        assert(std::strcmp(lv_label_get_text(icon), "Q") == 0);
        standalone::icons::set(icon, nullptr); // Preserve LVGL's explicit refresh.
        assert(std::strcmp(lv_label_get_text(icon), "Q") == 0);
        standalone_fonts.icons_16 = font;
        lv_obj_set_style_text_font(parent, font, 0);
        lv_obj_delete(icon);
    }
    for (const auto orientation : {ContextActionStripOrientation::HORIZONTAL, ContextActionStripOrientation::VERTICAL}) {
        ContextActionStrip strip(parent, orientation, ContextActionStripVerticalLayout::SPREAD);
        unsigned draws = 0;
        lv_obj_add_event_cb(strip.getElement(), [](lv_event_t* event) {
            ++*static_cast<unsigned*>(lv_event_get_user_data(event));
        }, LV_EVENT_DRAW_MAIN, &draws);
        for (unsigned state = 0; state <= 7; ++state) {
            ContextActionStripProps props{.visible = true};
            props.slots[0] = makeStandaloneIconStripSlot("X", static_cast<ContextActionStripVisualState>(state), ContextActionStripTone::DESTRUCTIVE);
            props.slots[1] = makeStructureSelectionCountStripSlot(3);
            props.slots[2] = makeStandaloneIconStripSlot("+", ContextActionStripVisualState::ACTIVE);
            props.slots[2].holdActive = state == 5;
            props.slots[2].holdStartedAtMs = 1000;
            props.slots[2].holdDurationMs = 1000;
            for (int pass = 0; pass < 2; ++pass) {
                // Cold opening, live update, then closing/reopening with the same content.
                strip.render(props);
                lv_refr_now(display);
                const auto first = pixels;
                lv_obj_invalidate(lv_screen_active());
                lv_refr_now(display);
                assert(first == pixels);
                uint64_t hash = 14695981039346656037ULL;
                for (auto pixel : pixels) hash = ((hash ^ (pixel & 255U)) * 1099511628211ULL ^ (pixel >> 8U)) * 1099511628211ULL;
                std::printf("strip=%u state=%u pass=%d rgb565=%016llx\n", unsigned(orientation), state, pass, static_cast<unsigned long long>(hash));
                // Fixed object budget and no repaint for identical presentation.
                assert(lv_obj_get_child_count(strip.getElement()) == 0);
                draws = 0;
                strip.render(props);
                lv_refr_now(display);
                assert(draws == 0);
                if (pass == 0) {
                    strip.render({.visible = false});
                    lv_refr_now(display);
                    assert(!lv_obj_is_visible(strip.getElement()));
                }
            }
        }
        ContextActionStripProps props{.visible = true};
        char command[] = "C";
        props.slots[0] = {.visualState = ContextActionStripVisualState::ACTIVE,
                          .showLabel = true, .label = command};
        props.hintLeft = "NAV: select";
        strip.render(props);
        lv_refr_now(display);
        const auto owned_text = pixels;
        command[0] = 'X';
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(pixels == owned_text); // The renderer owns transient caller text.
        strip.render(props);
        lv_refr_now(display);
        assert(pixels != owned_text);

        props.slots[0].holdActive = true;
        props.slots[0].holdStartedAtMs = now_ms;
        props.slots[0].holdDurationMs = 1000;
        strip.render(props);
        lv_refr_now(display);
        const auto hold_start = pixels;
        now_ms += 500;
        lv_tick_inc(40);
        lv_timer_handler();
        lv_refr_now(display);
        assert(pixels != hold_start);
        lv_obj_add_flag(parent, LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(display);
        draws = 0;
        now_ms += 200;
        lv_tick_inc(40);
        lv_timer_handler();
        lv_refr_now(display);
        assert(draws == 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_HIDDEN);
        strip.render(props);
        lv_refr_now(display);
        const auto reopened = pixels;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(pixels == reopened);

        // Wrapped clocks must still report a partially completed hold.
        props.slots[0].holdStartedAtMs = UINT32_MAX - 99U;
        now_ms = 150U;
        strip.render(props);
        lv_refr_now(display);
        const auto wrapped = pixels;
        props.slots[0].holdStartedAtMs = now_ms;
        strip.render(props);
        lv_refr_now(display);
        assert(pixels != wrapped);
        props.slots[0].holdActive = false;
        strip.render(props);
        lv_obj_set_width(parent, 260);
        lv_refr_now(display);
        const auto resized = pixels;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(pixels == resized);
        lv_obj_set_width(parent, 320);
    }
    lv_display_delete(display);
    lv_deinit();
}

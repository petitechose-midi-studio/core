#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/common/ContextHeader.hpp"
#include "ui/common/ParameterCard.hpp"
#include "ui/common/ContextSurfaceDraw.hpp"
#include "ui/font/StandaloneFonts.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_13_medium = font;
    standalone_fonts.icons_12 = standalone_fonts.icons_16 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    const auto children = lv_obj_get_child_count(screen);
    using namespace core::ui;
    {
        ContextHeader header(screen);
        ParameterCard card(screen);
        lv_obj_set_pos(header.getElement(), 4, 0);
        lv_obj_set_size(header.getElement(), 312, 22);
        lv_obj_set_pos(card.getElement(), 4, 30);
        lv_obj_set_size(card.getElement(), 100, 36);
        assert(lv_obj_get_child_count(screen) == children + 2);
        assert(lv_obj_get_child_count(header.getElement()) == 0);
        assert(lv_obj_get_child_count(card.getElement()) == 0);
        unsigned draws = 0;
        auto count = [](lv_event_t* e) { ++*static_cast<unsigned*>(lv_event_get_user_data(e)); };
        lv_obj_add_event_cb(header.getElement(), count, LV_EVENT_DRAW_MAIN, &draws);
        lv_obj_add_event_cb(card.getElement(), count, LV_EVENT_DRAW_MAIN, &draws);
        {
            char title[] = "Modulator";
            char value[] = "1/16";
            header.render({.title = title, .status = "LFO · Sync · On", .icon = "+"});
            card.render({.icon = "+", .label = "Rate", .value = value});
            // No field may retain a stack buffer supplied by a presenter.
            std::memset(title, 'X', sizeof(title) - 1);
            std::memset(value, 'X', sizeof(value) - 1);
        }
        lv_refr_now(display);
        const auto initial = pixels;
        draws = 0;
        header.render({.title = "Modulator", .status = "LFO · Sync · On", .icon = "+"});
        card.render({.icon = "+", .label = "Rate", .value = "1/16"});
        lv_refr_now(display);
        assert(draws == 0 && pixels == initial);

        ParameterCardProps focused{.icon = "+", .label = "Rate", .value = "1/8",
            .visual = {.accent = 0x00ff00, .state = interaction::InteractiveSurfaceState::FOCUSED,
                       .activityOpacity = LV_OPA_COVER}};
        card.render(focused);
        lv_refr_now(display);
        assert(draws == 1 && pixels != initial);
        const auto changed = pixels;
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        assert(pixels == changed); // Incremental repaint equals a complete repaint.

        focused.encoderNumber = 8U;
        draws = 0;
        card.render(focused);
        lv_refr_now(display);
        assert(draws == 1 && pixels != changed);
        const auto encoderBadge = pixels;
        draws = 0;
        card.render(focused);
        lv_refr_now(display);
        assert(draws == 0 && pixels == encoderBadge);
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        assert(pixels == encoderBadge);
        assert(lv_obj_get_child_count(card.getElement()) == 0);

        // Long UTF-8, a disappearing status icon and narrow cards cannot escape
        // their bounds; resizing uses current geometry without rebuilding children.
        header.render({.title = "Séquence très longue au-delà du titre", .status = "Preview +100%", .statusIcon = "!"});
        card.render({.icon = "+", .label = "Très longue propriété", .value = "1234567890123456789012345678901234567890"});
        lv_obj_set_width(card.getElement(), 65);
        lv_refr_now(display);
        const auto narrow = pixels;
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        assert(pixels == narrow);
        for (int y = 30; y < 66; ++y) for (int x = 104; x < 320; ++x) {
            assert(pixels[y * 320 + x] == initial[y * 320 + x]);
        }
        lv_obj_add_flag(card.getElement(), LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(display);
        draws = 0;
        card.render(focused);
        lv_refr_now(display);
        assert(draws == 0);
        lv_obj_clear_flag(card.getElement(), LV_OBJ_FLAG_HIDDEN);
        header.render({.title = "Modulator", .status = "Off"});
        lv_refr_now(display);
        const auto reopened = pixels;
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        assert(pixels == reopened);
        std::array<char, 7> bounded{};
        surface::copyText(bounded, "ééééé");
        assert(std::strcmp(bounded.data(), "é…") == 0);
        std::printf("editor surfaces: 2 objects, header=%zu B, card=%zu B; ownership, reuse and damage passed\n",
            sizeof(header), sizeof(card));
    }
    assert(lv_obj_get_child_count(screen) == children);
    lv_display_delete(display);
    lv_deinit();
}

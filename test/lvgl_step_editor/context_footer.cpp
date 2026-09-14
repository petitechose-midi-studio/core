#include <array>
#include <cassert>
#include <cstdio>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/state/NotificationQueue.hpp>
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/transportbar/TransportBar.hpp"
#include "ui/theme/StandaloneTheme.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_13_medium = font;
    standalone_fonts.icons_12 = standalone_fonts.icons_14 = standalone_fonts.icons_16 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    {
        core::state::StatusBarState state;
        core::ui::ContextActionStrip commands(lv_screen_active(), core::ui::ContextActionStripOrientation::HORIZONTAL);
        commands.alignToFooter();
        core::ui::ContextActionStripProps props{.visible = true};
        props.slots[0] = {.visualState = core::ui::ContextActionStripVisualState::ACTIVE,
                          .showLabel = true, .label = "Clear"};
        props.slots[2] = {.visualState = core::ui::ContextActionStripVisualState::ACTIVE,
                          .showLabel = true, .label = "Copy"};
        props.hintLeft = "NAV: focus";
        commands.render(props);
        core::ui::TransportBar transport(lv_screen_active(), state);
        lv_obj_align(transport.getElement(), LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_refr_now(display);
        assert(lv_obj_get_child_count(commands.getElement()) == 0);
        assert(lv_obj_get_child_count(transport.getElement()) == 0);
        assert(lv_obj_get_width(transport.getElement()) == standalone::theme::layout::TRANSPORT_CENTER_WIDTH);
        unsigned command_draws = 0, transport_draws = 0;
        const auto count = [](lv_event_t* e) { ++*static_cast<unsigned*>(lv_event_get_user_data(e)); };
        lv_obj_add_event_cb(commands.getElement(), count, LV_EVENT_DRAW_MAIN, &command_draws);
        lv_obj_add_event_cb(transport.getElement(), count, LV_EVENT_DRAW_MAIN, &transport_draws);
        auto previous = pixels;
        for (const float tempo : {24.0f, 120.0f, 300.0f, 999.9f}) {
            command_draws = transport_draws = 0;
            state.tempoDisplay.set(tempo);
            oc::state::NotificationQueue::instance().flush();
            lv_refr_now(display);
            assert(pixels != previous);
            assert(command_draws == 0); // Beat/tempo must not repaint the view behind the footer.
            assert(transport_draws == 1);
            for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
                if (y < 220 || x < 102 || x >= 218) assert(pixels[y * 320 + x] == previous[y * 320 + x]);
            }
            previous = pixels;
            transport_draws = 0;
            state.tempoDisplay.set(tempo);
            oc::state::NotificationQueue::instance().flush();
            lv_refr_now(display);
            assert(transport_draws == 0);
        }
        state.playing.set(true);
        state.tempoLocked.set(true);
        state.transportLocked.set(true);
        state.ccInActive.set(true);
        state.ccOutActive.set(true);
        oc::state::NotificationQueue::instance().flush();
        lv_refr_now(display);
        const auto active = pixels;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(pixels == active);
        // Long local text cannot modify the central transport pixels.
        props.slots[0].label = "MMMMMMMMMMMMMMM";
        props.slots[2].label = "MMMMMMMMMMMMMMM";
        commands.render(props);
        lv_refr_now(display);
        for (int y = 220; y < 240; ++y) for (int x = 102; x < 218; ++x) {
            assert(pixels[y * 320 + x] == active[y * 320 + x]);
        }
        std::printf("footer: 2 draw objects, strip=%zu bytes, transport=%zu bytes; isolated redraw passed\n",
                    sizeof(commands), sizeof(transport));
    }
    lv_display_delete(display);
    lv_deinit();
}

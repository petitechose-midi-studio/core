#include <array>
#include <cassert>
#include <cstdio>

#include "ui/common/TrackNavigationStrip.hpp"

int main() {
    lv_init();
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, 320, 20);
    {
        core::ui::TrackNavigationStrip strip(parent);
        constexpr auto count = core::ui::TrackNavigationStripProps::TRACK_COUNT;
        auto* row = lv_obj_get_child(strip.getElement(), 0);
        auto* active = lv_obj_get_child(row, count);
        auto* focus = lv_obj_get_child(row, count + 1);
        unsigned layouts = 0;
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            ++*static_cast<unsigned*>(lv_event_get_user_data(e));
        }, LV_EVENT_LAYOUT_CHANGED, &layouts);
        core::ui::TrackNavigationStripProps props{
            .activeTrack = 2, .previewTrack = 4, .enabledMask = 0x001f,
            .soloMask = 4, .selectedMask = 0x0010, .focusingTrack = true,
        };
        strip.render(props);
        // Preparing one widget must not synchronously lay out the whole screen.
        assert(layouts == 0);
        const auto checkFrame = [&] {
            lv_refr_now(display);
            auto* playing = lv_obj_get_child(row, props.activeTrack);
            auto* focused = lv_obj_get_child(row, props.previewTrack);
            assert(lv_obj_is_visible(active) && lv_obj_is_visible(focus));
            assert(lv_obj_get_x(active) == lv_obj_get_x(playing) + 1);
            assert(lv_obj_get_y(active) == lv_obj_get_y(playing) + lv_obj_get_height(playing) - 1);
            assert(lv_obj_get_width(active) == lv_obj_get_width(playing) - 2);
            assert(lv_obj_get_x(focus) == lv_obj_get_x(focused));
            assert(lv_obj_get_y(focus) == lv_obj_get_y(focused));
            assert(lv_obj_get_width(focus) == lv_obj_get_width(focused));
            assert(lv_obj_get_height(focus) == lv_obj_get_height(focused));
            const auto first = pixels;
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            assert(pixels == first);
        };
        checkFrame();
        // Geometry changes without a new model notification, including while hidden.
        lv_obj_set_width(parent, 283);
        checkFrame();
        lv_obj_add_flag(strip.getElement(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(parent, 307);
        lv_obj_update_layout(parent);
        lv_obj_remove_flag(strip.getElement(), LV_OBJ_FLAG_HIDDEN);
        checkFrame();
        props.previewTrack = 3;
        strip.render(props);
        props.previewTrack = 15;
        props.activeTrack = 1;
        strip.render(props);
        checkFrame();
        props.enabledMask = 0;
        props.focusingTrack = false;
        strip.render(props);
        lv_refr_now(display);
        assert(!lv_obj_is_visible(active) && !lv_obj_is_visible(focus));
    }
    lv_display_delete(display);
    lv_deinit();
    std::puts("Track strip: deferred layout, first frame, resize, hidden resize and cursor transitions passed");
}

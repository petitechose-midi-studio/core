#include <array>
#include <cassert>
#include <cstdio>

#include "ui/view/MainViewFrame.hpp"

int main() {
    lv_init();
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent);
    {
        core::ui::MainViewFrame frame(parent);
        assert(frame.valid());
        assert(lv_obj_get_parent(frame.header()) == frame.container());
        auto* header = lv_obj_create(frame.header());
        lv_obj_remove_style_all(header);
        lv_obj_set_style_bg_color(header, lv_color_hex(0xa02050), 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
        frame.createInteractionRow();
        auto* left = lv_obj_create(frame.interactionRow());
        lv_obj_remove_style_all(left);
        lv_obj_set_size(left, 16, LV_PCT(100));
        frame.createCenterColumn();
        auto* center = frame.centerColumn();
        lv_obj_set_style_bg_color(center, lv_color_hex(0x203060), 0);
        lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
        auto* bottom = lv_obj_create(frame.body());
        lv_obj_remove_style_all(bottom);
        lv_obj_set_size(bottom, LV_PCT(100), 18);
        for (const auto dimensions : {lv_point_t{320, 214}, lv_point_t{320, 220}, lv_point_t{283, 190}}) {
            for (const int headerHeight : {0, 20, 32}) {
                lv_obj_set_size(parent, dimensions.x, dimensions.y);
                lv_obj_set_size(header, LV_PCT(100), headerHeight);
                lv_obj_add_flag(frame.container(), LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(frame.container(), LV_OBJ_FLAG_HIDDEN);
                lv_refr_now(display);
                assert(lv_obj_get_height(frame.header()) == headerHeight);
                assert(lv_obj_get_y(frame.body()) == headerHeight);
                assert(lv_obj_get_height(center) == dimensions.y - headerHeight - 18);
                assert(lv_obj_get_width(center) == dimensions.x - 16);
                const auto first = pixels;
                lv_obj_invalidate(lv_screen_active());
                lv_refr_now(display);
                assert(first == pixels);
                uint64_t hash = 14695981039346656037ULL;
                for (auto pixel : pixels) hash = ((hash ^ (pixel & 255U)) * 1099511628211ULL ^ (pixel >> 8U)) * 1099511628211ULL;
                std::printf("frame=%dx%d header=%d rgb565=%016llx\n", int(dimensions.x), int(dimensions.y), headerHeight, static_cast<unsigned long long>(hash));
            }
        }
    }
    lv_display_delete(display);
    lv_deinit();
}

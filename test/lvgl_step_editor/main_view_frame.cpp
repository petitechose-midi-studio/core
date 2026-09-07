#include <array>
#include <cassert>
#include <cstdio>

#include "ui/view/MainViewFrame.hpp"
#include <oc/ui/lvgl/RetainedSurfaceParkingLot.hpp>

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
    {
        // Project pages are flex children, unlike the overlapping top-level views.
        // Parking must preserve pixels, restore geometry, and isolate hidden layout.
        oc::ui::lvgl::RetainedSurfaceParkingLot parking;
        assert(parking.initialize());
        auto* host = parking.createHost();
        assert(host);
        core::ui::MainViewFrame frame(parent);
        frame.createInteractionRow();
        frame.createCenterColumn();
        auto* center = frame.centerColumn();
        std::array<lv_obj_t*, 2> pages{};
        for (size_t i = 0; i < pages.size(); ++i) {
            pages[i] = lv_obj_create(center);
            lv_obj_remove_style_all(pages[i]);
            lv_obj_set_size(pages[i], LV_PCT(100), 0);
            lv_obj_set_flex_grow(pages[i], 1);
            lv_obj_set_style_bg_opa(pages[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(pages[i], lv_color_hex(i ? 0x804020 : 0x204080), 0);
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        for (unsigned pass = 0; pass < 24; ++pass) {
            const auto active = pass % 2;
            const auto inactive = 1 - active;
            lv_obj_set_size(parent, 280 + pass, 190 + pass);
            for (auto* page : pages) {
                parking.attach(page, center);
                lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_remove_flag(pages[active], LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            const auto reference = pixels;
            const auto hiddenWidth = lv_obj_get_width(pages[inactive]);
            parking.park(pages[inactive], host);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            assert(reference == pixels);
            assert(lv_obj_get_child_count(center) == 1);
            assert(lv_obj_get_width(pages[active]) == 280 + pass);
            assert(lv_obj_get_height(pages[active]) == 190 + pass);
            lv_obj_set_width(parent, 310);
            lv_obj_update_layout(center);
            assert(lv_obj_get_width(pages[inactive]) == hiddenWidth);
        }
        // Page owners are destroyed before the parking lot, whichever parent owns them.
        for (auto* page : pages) lv_obj_delete(page);
    }
    lv_display_delete(display);
    lv_deinit();
}

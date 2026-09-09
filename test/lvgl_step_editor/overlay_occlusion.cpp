#include <array>
#include <cassert>
#include <cstdio>
#include <ms/ui/ViewContainer.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

// Real LVGL coverage check for StandaloneUiAssembly's curtain placement.
// A translucent overlay and a separate opaque footer must not redraw the
// obscured view when the dirty rectangle spans both zones.
int main() {
    lv_init();
    auto* display = lv_display_create(320, 240);
    using Pixels = std::array<uint16_t, 320 * 240>;
    Pixels pixels{};
    std::array<Pixels, 6> reference{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    unsigned draws[2]{};
    for (unsigned variant = 0; variant < 2; ++variant) {
        ms::ui::ViewContainer zones(lv_screen_active());
        auto* root = zones.getContainer();
        auto* main = zones.getMainZone();
        auto* bottom = zones.getBottomZone();
        auto box = [](lv_obj_t* parent, int width, int height, uint32_t color, lv_opa_t opa) {
            auto* obj = lv_obj_create(parent);
            lv_obj_remove_style_all(obj);
            oc::ui::lvgl::style::apply(obj).size(width, height).bgColor(color, opa).noScroll();
            return obj;
        };
        auto* view = box(main, LV_PCT(100), LV_PCT(100), 0xa03060, LV_OPA_COVER);
        lv_obj_add_event_cb(view, [](lv_event_t* e) {
            ++*static_cast<unsigned*>(lv_event_get_user_data(e));
        }, LV_EVENT_DRAW_MAIN, &draws[variant]);
        auto* curtain = box(variant ? root : main, LV_PCT(100), LV_PCT(100), 0, LV_OPA_TRANSP);
        lv_obj_add_flag(curtain, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT));
        auto* overlay = box(root, LV_PCT(100), LV_PCT(100), 0, LV_OPA_90);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
        box(overlay, 70, 40, 0x2070a0, LV_OPA_COVER);
        box(bottom, LV_PCT(100), 20, 0, LV_OPA_COVER);
        auto* indicator = box(bottom, 8, 8, 0x40ff80, LV_OPA_COVER);
        lv_obj_add_flag(indicator, LV_OBJ_FLAG_FLOATING);
        lv_obj_move_foreground(bottom);
        for (unsigned state = 0; state < reference.size(); ++state) {
            const bool open = state != 0 && state != 5;
            lv_obj_set_style_bg_opa(curtain, open ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            if (open) lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            if (state == 2) lv_obj_set_size(overlay, 240, 150); // compact overlay
            if (state == 3) lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
            if (state == 4) lv_obj_set_style_bg_color(view, lv_color_hex(0x3070b0), 0);
            lv_obj_update_layout(root);
            assert(lv_obj_get_height(main) == 220);
            assert(lv_obj_get_y(bottom) == 220);
            draws[variant] = 0;
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            if (variant) {
                assert(reference[state] == pixels);
                assert(draws[variant] == (open ? 0U : 1U));
            } else {
                reference[state] = pixels;
                assert(draws[variant] == 1);
            }
            std::printf("curtain=%u state=%u view_draws=%u\n", variant, state, draws[variant]);
        }
        assert(reference[0] != reference[1] && reference[4] != reference[5]);
    }
    lv_display_delete(display);
    lv_deinit();
}

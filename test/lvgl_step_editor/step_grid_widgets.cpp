#include <array>
#include <cassert>
#include <cstdio>

#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/sequencer/StepGridWidgets.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_bold = font;
    standalone_fonts.icons_12 = standalone_fonts.icons_14 = standalone_fonts.icons_16 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, 304, 190);
    lv_obj_t *root{}, *grid{}, *notes{};
    namespace widgets = core::ui::sequencer::grid::widgets;
    widgets::createRoot(parent, root, grid, notes, [](lv_event_t*) {}, nullptr);
    std::array<lv_obj_t*, 8> tiles{}, buttons{};
    for (uint8_t i = 0; i < 8; ++i) {
        lv_obj_t *label{}, *secondary{}, *icon{};
        lv_coord_t width{}, height{};
        widgets::createTile(i, grid, notes, tiles[i], label, secondary, icon, buttons[i],
                            width, height, [](lv_event_t*) {}, nullptr);
        lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x20b060 + i * 0x100000), 0);
        lv_obj_set_style_bg_opa(buttons[i], LV_OPA_70, 0);
        lv_obj_set_style_border_opa(buttons[i], LV_OPA_COVER, 0);
        assert(width > 0 && height > 0);
    }
    for (const auto size : {lv_point_t{304, 170}, lv_point_t{320, 176}, lv_point_t{283, 181}}) {
        lv_obj_set_size(parent, size.x, size.y);
        lv_refr_now(display);
        for (uint8_t i = 0; i < 8; ++i) {
            lv_area_t tile{}, button{};
            lv_obj_get_coords(tiles[i], &tile);
            lv_obj_get_coords(buttons[i], &button);
            assert(tile.x1 == button.x1 && tile.y1 == button.y1 && tile.x2 == button.x2 && tile.y2 == button.y2);
        }
        const auto first = pixels;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(first == pixels);
        uint64_t hash = 14695981039346656037ULL;
        for (const auto pixel : pixels) hash = ((hash ^ (pixel & 255U)) * 1099511628211ULL ^ (pixel >> 8U)) * 1099511628211ULL;
        std::printf("grid=%dx%d rgb565=%016llx\n", int(size.x), int(size.y), static_cast<unsigned long long>(hash));
    }
    for (uint8_t i = 0; i < 8; ++i) {
        assert(lv_obj_get_parent(buttons[i]) == tiles[i]);
        assert(lv_obj_get_style_layout(tiles[i], LV_PART_MAIN) == LV_LAYOUT_NONE);
    }
    lv_display_delete(display);
    lv_deinit();
}

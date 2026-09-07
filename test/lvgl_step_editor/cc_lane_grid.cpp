#include <array>
#include <cassert>
#include <cstdio>
#include <lvgl.h>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/sequencer/SequencerCcLaneGrid.hpp"

CoreFonts fonts;
static unsigned labels{}, curves{};
static void observeLabel(lv_layer_t* layer, const lv_draw_label_dsc_t* dsc, const lv_area_t* area) {
    ++labels;
    lv_draw_label(layer, dsc, area);
}
static void observeLine(lv_layer_t* layer, const lv_draw_line_dsc_t* dsc) {
    ++curves;
    lv_draw_line(layer, dsc);
}
#define lv_draw_label observeLabel
#define lv_draw_line observeLine
#include "../../src/ui/sequencer/SequencerCcLaneGrid.cpp"
#undef lv_draw_label
#undef lv_draw_line

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_medium = fonts.inter_13_bold = font;
    fonts.inter_14_medium = fonts.inter_14_semibold = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    unsigned partialLabels{}, partialCurves{}, checks{};
    for (auto layout : {core::ui::SequencerCcLaneGridLayout::OVERLAY, core::ui::SequencerCcLaneGridLayout::EMBEDDED}) {
        core::ui::SequencerCcLaneGrid grid(lv_screen_active(), layout);
        core::ui::SequencerCcLaneGridProps props{};
        props.visible = true;
        props.title = "CC 1";
        props.hint = "Stable curve";
        for (unsigned i = 0; i < 8; ++i) {
            props.cells[i] = {.visible = true, .authored = i % 2 == 0, .focused = i == 3,
                              .playhead = i == 0, .step = static_cast<uint8_t>(i),
                              .value = static_cast<uint8_t>(i % 2 ? 127 : 0)};
            if (i == 7) continue;
            auto& segment = props.segments[i];
            segment.visible = true;
            segment.pointCount = 16;
            for (unsigned j = 0; j < 16; ++j)
                segment.points[j] = {static_cast<uint8_t>(j * 17), static_cast<uint8_t>((j * 23 + i * 17) % 128)};
        }
        grid.render(props);
        lv_refr_now(display);
        uint64_t hash = 14695981039346656037ULL;
        for (auto p : pixels) hash = (hash ^ p) * 1099511628211ULL;
        std::printf("layout=%u full_hash=%llu\n", unsigned(layout), static_cast<unsigned long long>(hash));
        for (unsigned step = 1; step < 8; ++step) {
            props.cells[step - 1].playhead = false;
            props.cells[step].playhead = true;
            labels = curves = 0;
            grid.render(props);
            lv_refr_now(display);
            partialLabels += labels;
            partialCurves += curves;
            const auto partial = pixels;
            lv_obj_invalidate(grid.getElement());
            lv_refr_now(display);
            assert(partial == pixels);
            ++checks;
        }
        // Arbitrary horizontal strips also preserve the complete image, including
        // line antialiasing at the upper/lower curve boundary and focus borders.
        for (int y = 0; y < 180; y += 2) {
            const auto reference = pixels;
            const lv_area_t strip{0, y, 319, y + 1};
            lv_obj_invalidate_area(grid.getElement(), &strip);
            lv_refr_now(display);
            assert(reference == pixels);
            ++checks;
        }
    }
    std::printf("partial_labels=%u partial_curves=%u pixel_checks=%u\n", partialLabels, partialCurves, checks);
    std::fflush(stdout);
    assert(partialLabels == 0 && partialCurves == 0);
    lv_deinit();
}

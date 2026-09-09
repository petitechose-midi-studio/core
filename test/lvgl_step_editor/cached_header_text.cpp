#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/common/CompactMetricRow.hpp"
#include "ui/common/TrackHeaderRow.hpp"
#include "ui/sequencer/SequencerCcLaneGrid.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main(int argc, char** argv) {
    const bool reference = argc > 1 && std::strcmp(argv[1], "--reference") == 0;
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_medium = fonts.inter_13_bold = font;
    fonts.inter_14_medium = fonts.inter_14_semibold = font;
    standalone_fonts.icons_14 = standalone_fonts.icons_16 = font;
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
    const char* cases[] = {"Short", "1234567", "1234567890123456789012345678901",
        "1234567890123456789012345678901234567890123456789012345678901234567890",
        "1234567890123456789012345678901234567890123456789012345678901234567tail",
        "New", "", nullptr};
    const auto verify = [&](const char* name, auto render) {
        for (const auto* text : cases) {
            render(text);
            lv_refr_now(display);
            const auto expected = pixels;
            flushes = 0;
            render(text);
            lv_refr_now(display);
            std::printf("%s length=%zu repeated_flushes=%u\n", name, text ? std::strlen(text) : 0, flushes);
            if (!reference) assert(flushes == 0);
            assert(expected == pixels);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            assert(expected == pixels);
        }
    };
    {
        core::ui::TrackHeaderRow row(lv_screen_active());
        core::ui::TrackHeaderRowProps props{};
        verify("track", [&](const char* text) { props.leftText = text; row.render(props); });
    }
    {
        core::ui::CompactMetricRow row;
        assert(row.create(lv_screen_active()));
        std::array<core::ui::CompactMetricProps, 2> props{};
        verify("metric", [&](const char* text) {
            props[0] = {.icon = "M", .value = text};
            row.render(props);
        });
        lv_obj_delete(row.element());
    }
    for (auto layout : {core::ui::SequencerCcLaneGridLayout::OVERLAY, core::ui::SequencerCcLaneGridLayout::EMBEDDED}) {
        core::ui::SequencerCcLaneGrid grid(lv_screen_active(), layout);
        core::ui::SequencerCcLaneGridProps props{.visible = true};
        verify(layout == core::ui::SequencerCcLaneGridLayout::OVERLAY ? "cc_overlay" : "cc_embedded",
            [&](const char* text) {
                props.title = props.meta = props.hint = text;
                grid.render(props);
            });
    }
    lv_display_delete(display);
    lv_deinit();
}

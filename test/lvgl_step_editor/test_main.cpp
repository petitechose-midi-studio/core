#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "ui/sequencer/SequencerStepEditOverlay.hpp"
#include <ms/ui/font/CoreFonts.hpp>

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main(int argc, char** argv) {
    const bool reference = argc > 1 && std::strcmp(argv[1], "--reference") == 0;
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_medium = fonts.inter_13_bold = font;
    fonts.inter_14_medium = fonts.inter_14_semibold = font;
    standalone_fonts.icons_12 = standalone_fonts.icons_14 = standalone_fonts.icons_16 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, 320, 210);
    lv_obj_update_layout(parent);
    {
        core::ui::SequencerStepEditOverlay editor(parent);
        core::ui::SequencerStepEditOverlayProps props{
            .visible = true, .stepBadge = "S1", .title = "Step",
            .focusLabel = "State", .enabled = true,
            .state = {.key = "State", .value = "On", .icon = "S"},
        };
        props.properties[0] = {.key = "Pitch", .value = "C3", .icon = "P"};
        props.properties[1] = {.key = "Velocity", .value = "96", .icon = "V"};
        props.properties[2] = {.key = "Gate", .value = "50%", .icon = "G"};
        props.properties[4] = {.key = "Chance", .value = "100%", .icon = "?"};
        bool opening = false;
        lv_obj_add_event_cb(lv_obj_get_child(editor.getElement(), 0), [](lv_event_t* e) {
            const auto* opening = static_cast<const bool*>(lv_event_get_user_data(e));
            if (*opening) assert(!lv_obj_is_visible(lv_event_get_target_obj(e)));
        }, LV_EVENT_SIZE_CHANGED, &opening);
        for (int pass = 0; pass < 4; ++pass) {
            editor.render({.visible = false});
            // Reopen unchanged, then resize/rebind, then use the chord layout.
            if (pass == 2) {
                lv_obj_set_size(parent, 280, 190);
                ++props.dataRevision;
                props.title = "Snare";
            }
            if (pass == 3) {
                ++props.dataRevision;
                props.chordDetailLayout = true;
            }
            // Mirror the real registry's early reveal before presenter render.
            lv_obj_clear_flag(editor.getElement(), LV_OBJ_FLAG_HIDDEN);
            opening = !reference;
            editor.render(props);
            lv_refr_now(display);
            opening = false;
            assert(lv_obj_is_visible(editor.getElement()));
            const auto first = pixels;
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            assert(first == pixels);
            uint64_t hash = 14695981039346656037ULL;
            for (auto pixel : pixels) {
                hash = (hash ^ (pixel & 255U)) * 1099511628211ULL;
                hash = (hash ^ (pixel >> 8U)) * 1099511628211ULL;
            }
            std::printf("editor=%d rgb565=%016llx\n", pass, static_cast<unsigned long long>(hash));
            editor.render(props);
        }
    }
    lv_display_delete(display);
    lv_deinit();
}

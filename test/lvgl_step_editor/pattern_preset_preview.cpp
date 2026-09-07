#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <lvgl.h>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/sequencer/SequencerPatternPresetPreview.hpp"
#include "ui/font/StandaloneFonts.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;
static uintptr_t ownerBegin{}, ownerEnd{};
static unsigned metadataCalls{};
static bool retained = true;
static std::array<char, 48> timing{}, content{};

// Observe the actual draw descriptor, then let real LVGL render normally.
// The text must belong to the retained widget, not a draw callback's stack.
static void observeLabel(lv_layer_t* layer, const lv_draw_label_dsc_t* dsc,
                         const lv_area_t* area) {
    const bool isContent = std::strncmp(dsc->text, "Micro ", 6) == 0;
    if (isContent || std::strncmp(dsc->text, "Instrument ·", 13) == 0 ||
        std::strstr(dsc->text, " lanes ·")) {
        ++metadataCalls;
        const auto address = reinterpret_cast<uintptr_t>(dsc->text);
        retained &= address >= ownerBegin && address < ownerEnd;
        auto& text = isContent ? content : timing;
        std::snprintf(text.data(), text.size(), "%s", dsc->text);
    }
    lv_draw_label(layer, dsc, area);
}
#define lv_draw_label observeLabel
#include "../../src/ui/sequencer/SequencerPatternPresetPreview.cpp"
#undef lv_draw_label

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts.inter_12_medium = fonts.inter_13_medium = fonts.inter_13_bold = font;
    fonts.inter_14_medium = fonts.inter_14_semibold = font;
    standalone_fonts.icons_12 = font;
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    using namespace core::ui::sequencer;
    namespace seq = core::state::sequencer;
    SequencerPatternPresetPreview preview;
    ownerBegin = reinterpret_cast<uintptr_t>(&preview);
    ownerEnd = ownerBegin + sizeof(preview);
    preview.create(lv_screen_active());
    for (bool drum : {false, true}) {
        seq::SequencerPatternPresetDescriptor source{};
        source.visual.valid = true;
        source.visual.visibleStepCount = 16;
        source.patternLength = 128;
        source.stepsPerBeat = 32;
        source.visual.microSequenceCount = 7;
        source.visual.cycleStateCount = 3;
        source.visual.ccLaneCount = 2;
        source.metadata.trackKind = drum ? seq::SequencerTrackKind::DRUM : seq::SequencerTrackKind::INSTRUMENT;
        source.drumLaneCount = source.visual.laneCount = 8;
        source.visual.melodicEnabledMask = 0xffff;
        for (unsigned i = 0; i < 16; ++i) {
            source.visual.notes[i] = 48 + i;
            source.visual.velocities[i] = 8 * i;
        }
        preview.render({.descriptor = &source, .revision = drum ? 2U : 1U, .visible = true});
        source = {}; // Presenter lifetime must not matter to the next draw.
        lv_refr_now(display);
        assert(std::strcmp(timing.data(), drum ? "8 lanes · 128 steps · 1/128" : "Instrument · 128 steps · 1/128") == 0);
        assert(std::strcmp(content.data(), "Micro 7 · Cycle 3 · CC 2") == 0);
        const auto reference = pixels;
        lv_obj_invalidate(preview.element());
        lv_refr_now(display);
        assert(pixels == reference);
        uint64_t hash = 14695981039346656037ULL;
        for (auto p : pixels) hash = (hash ^ p) * 1099511628211ULL;
        std::printf("%s pixel_hash=%llu\n", drum ? "drum" : "melodic", static_cast<unsigned long long>(hash));
        preview.render({.visible = false});
    }
    std::printf("metadata_calls=%u retained=%d owner_bytes=%zu\n", metadataCalls, retained, sizeof(preview));
    std::fflush(stdout);
    assert(metadataCalls >= 8);
    assert(retained);
    lv_deinit();
}

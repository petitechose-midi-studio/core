#include <array>
#include <cassert>
#include <cstdio>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/sequencer/DrumOverviewSurface.hpp"
#include "state/sequencer/SequencerUiState.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

int main() {
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
    {
        using namespace core::state::sequencer;
        DrumTrackState track;
        track.reset();
        track.pattern.setStepEnabled(0, 0, true);
        track.pattern.setStepEnabled(0, 1, true);
        DrumSequencerState projection;
        projection.drumTrack = &track;
        projection.phase = DrumSequencerPhase::GRID;
        projection.playbackActive = true;
        core::ui::sequencer::DrumOverviewSurface surface(lv_screen_active());
        core::ui::sequencer::DrumOverviewSurfaceProps props{.visible = true, .projection = &projection};
        surface.render(props);
        lv_refr_now(display);
        unsigned invalidations = 0;
        const auto count = +[](lv_event_t* e) {
            ++*static_cast<unsigned*>(lv_event_get_user_data(e));
        };
        lv_display_add_event_cb(display, count, LV_EVENT_INVALIDATE_AREA, &invalidations);
        unsigned pixelChecks = 0;
        const auto checkPixels = [&] {
            ++pixelChecks;
            lv_refr_now(display);
            const auto partial = pixels;
            lv_obj_invalidate(surface.getElement());
            lv_refr_now(display);
            if (partial != pixels) std::fprintf(stderr, "partial repaint differs at check %u\n", pixelChecks);
            assert(partial == pixels);
        };
        // A certain root note has no changing chance marker.
        projection.chanceDecisionValidMask = 1;
        projection.chanceDecisionPlayedMask = 1;
        surface.render(props);
        assert(invalidations == 0U);
        checkPixels();
        // A probabilistic root must still repaint both its old and new marker.
        track.pattern.setStepProbability(0, 0, 50);
        track.pattern.setStepProbability(0, 1, 50);
        ++props.authoredRevision;
        surface.render(props);
        checkPixels();
        for (uint8_t step : {1, 0}) {
            projection.chanceDecisionSteps[0] = step;
            projection.chanceDecisionPlayedMask ^= 1;
            invalidations = 0;
            surface.render(props);
            assert(invalidations > 0U);
            checkPixels();
        }
        // Cycle/micro feedback remains live even when the root chance is 100%.
        track.pattern.setStepProbability(0, 0, 100);
        ++props.authoredRevision;
        projection.resolvedPage.contextKey = 0;
        projection.resolvedPage.validMask = 1;
        projection.resolvedPage.cyclePresentMask = 1;
        projection.resolvedPage.velocity[0] = 100;
        projection.resolvedPage.gate[0] = 70;
        surface.render(props);
        checkPixels();
        for (bool played : {true, false, true}) {
            projection.resolvedPage.playedMask = played ? 1U : 0U;
            invalidations = 0;
            surface.render(props);
            assert(invalidations > 0U);
            checkPixels();
        }
        projection.resolvedPage.microLength[0] = 4;
        projection.resolvedPage.microMask[0] = 5;
        projection.playheadValidMask = 1;
        surface.render(props);
        checkPixels();
        for (uint8_t phase : {64, 128, 192, 0}) {
            projection.playheadPhasesQ8[0] = phase;
            surface.render(props);
            checkPixels();
        }
        // Resolved notes can cross several neighboring steps.
        projection.resolvedPage.microLength[0] = 0;
        for (uint16_t gate : {600, 50, 1000, 100}) {
            projection.resolvedPage.gate[0] = gate;
            surface.render(props);
            checkPixels();
        }
        projection.resolvedPage.validMask = 3;
        projection.resolvedPage.playedMask = 3;
        projection.resolvedPage.cyclePresentMask = 3;
        projection.resolvedPage.velocity[1] = 40;
        projection.resolvedPage.gate[1] = 300;
        for (int8_t nudge : {-90, 90, 0}) {
            projection.resolvedPage.nudge[1] = nudge;
            surface.render(props);
            checkPixels();
        }
        // Adjacent/disjoint authored lanes retain exact repaint coverage.
        for (uint8_t lane : {0, 1, 4, 7}) {
            track.pattern.setStepEnabled(lane, lane, true);
            track.pattern.setStepGate(lane, lane, 400);
            ++props.authoredRevision;
            surface.render(props);
            checkPixels();
        }
        lv_display_remove_event_cb_with_user_data(display, count, &invalidations);
    }
    lv_display_delete(display);
    lv_deinit();
}

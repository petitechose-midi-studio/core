#include <cassert>
#include <iostream>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerPatternEditorOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

namespace seq = core::state::sequencer;

static_assert(sizeof(seq::SequencerPatternEditorState) <= 128U);

void test_open_retains_exact_owner_page_and_wrapped_navigation() {
    seq::SequencerState state;
    assert(state.pattern.setContentLength(20));
    state.page.set(2);
    state.focusedStep.set(17);

    assert(seq::openPatternEditor(state, 3));
    assert(state.patternEditor.active.get());
    assert(state.patternEditor.ownerTrack == 3);
    assert(state.patternEditor.windowStart == 16);
    assert(state.patternEditor.focusedField == seq::SequencerPatternEditorField::LENGTH);
    assert(state.patternEditor.focusedLayer == seq::SequencerPatternEditorLayer::NOTES);

    assert(seq::movePatternEditorWindow(state, 1));
    assert(state.patternEditor.windowStart == 0);
    assert(state.page.get() == 0);
    assert(state.focusedStep.get() == 0);
    assert(seq::movePatternEditorWindow(state, -1));
    assert(state.patternEditor.windowStart == 16);
    assert(state.page.get() == 2);

    assert(seq::movePatternEditorField(state, -1));
    assert(state.patternEditor.focusedField == seq::SequencerPatternEditorField::NUDGE);
    assert(seq::movePatternEditorLayer(state, -1));
    assert(state.patternEditor.focusedLayer == seq::SequencerPatternEditorLayer::REGION);
    assert(state.patternEditor.focusedField ==
           seq::SequencerPatternEditorField::LOOP_START);
    assert(seq::setPatternEditorNavigationMode(
        state,
        seq::SequencerPatternEditorNavigationMode::WINDOWS
    ));
    assert(state.patternEditor.navigationMode ==
           seq::SequencerPatternEditorNavigationMode::WINDOWS);

    seq::closePatternEditor(state);
    assert(!state.patternEditor.active.get());
    assert(state.patternEditor.navigationMode ==
           seq::SequencerPatternEditorNavigationMode::FIELDS);
}

void test_fields_use_canonical_pattern_authorities_and_exact_marker_ranges() {
    seq::SequencerState state;
    assert(seq::openPatternEditor(state, 0));

    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::LENGTH,
        12
    ));
    assert(state.pattern.length.get() == 12);
    assert(state.pattern.loopEnd == 12);

    assert(seq::setPatternPlaybackRegion(state.pattern, {12, 2, 4, 10}));
    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::PLAY_START,
        3
    ));
    assert(seq::patternPlaybackRegion(state.pattern).playStart == 3);

    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::LOOP_START,
        8
    ));
    auto region = seq::patternPlaybackRegion(state.pattern);
    assert(region.loopStart == 8);
    assert(region.loopEnd == 10);

    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::LOOP_END,
        7
    ));
    region = seq::patternPlaybackRegion(state.pattern);
    assert(region.loopEnd == 9);

    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::SWING,
        99
    ));
    assert(state.pattern.swingOffsetPercent.get() ==
           seq::SequencerPatternState::MAX_PATTERN_SWING_OFFSET_PERCENT);
    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::NUDGE,
        -99
    ));
    assert(state.pattern.patternNudgePercent.get() ==
           seq::SequencerPatternState::MIN_PATTERN_NUDGE_PERCENT);

    const auto countRange = seq::patternEditorValueRange(
        state,
        seq::SequencerPatternEditorField::COUNT
    );
    assert(!countRange.editable());
    assert(!seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::COUNT,
        1
    ));
}

void test_division_is_direct_and_length_repairs_window() {
    seq::SequencerState state;
    assert(state.pattern.setContentLength(20));
    assert(seq::setPatternPlaybackRegion(state.pattern, {20, 2, 5, 18}));
    state.page.set(2);
    assert(seq::openPatternEditor(state, 0));
    assert(state.patternEditor.windowStart == 16);

    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::DIVISION,
        5
    ));
    assert(state.pattern.stepsPerBeat.get() == 8U);

    assert(seq::setPatternEditorFieldValue(
        state,
        seq::SequencerPatternEditorField::LENGTH,
        8
    ));
    assert(state.patternEditor.windowStart == 0);
    assert(state.page.get() == 0);
    assert(state.focusedStep.get() < 8);
    assert(seq::patternPlaybackRegion(state.pattern).isValid());
}

void test_layers_are_dense_and_offer_only_first_free_lane_before_region() {
    seq::SequencerState state;
    state.pattern.ccLanes = core::app::makeExtmemUnique<
        seq::SequencerCcLaneBank>();
    assert(state.pattern.ccLanes);
    state.pattern.ccLanes->lanes[2].occupied = true;
    assert(seq::openPatternEditor(state, 0));

    assert(seq::patternEditorVisibleLayerCount(state) == 4U);
    assert(seq::patternEditorVisibleLayerAt(state, 0) ==
           seq::SequencerPatternEditorLayer::NOTES);
    assert(seq::patternEditorVisibleLayerAt(state, 1) ==
           seq::SequencerPatternEditorLayer::CC3);
    assert(seq::patternEditorVisibleLayerAt(state, 2) ==
           seq::SequencerPatternEditorLayer::CC1);
    assert(seq::patternEditorLayerIsAdd(
        state, seq::SequencerPatternEditorLayer::CC1));
    assert(!seq::patternEditorLayerIsAdd(
        state, seq::SequencerPatternEditorLayer::CC3));
    assert(seq::patternEditorVisibleLayerAt(state, 3) ==
           seq::SequencerPatternEditorLayer::REGION);

    assert(seq::movePatternEditorLayer(state, -1));
    assert(state.patternEditor.focusedLayer ==
           seq::SequencerPatternEditorLayer::REGION);
}

void test_reset_closes_session_and_restores_compact_defaults() {
    seq::SequencerState state;
    state.page.set(1);
    assert(seq::openPatternEditor(state, 7));
    state.patternEditor.focusedField = seq::SequencerPatternEditorField::LOOP_END;
    state.patternEditor.focusedLayer = seq::SequencerPatternEditorLayer::CC4;
    state.reset();
    assert(!state.patternEditor.active.get());
    assert(state.patternEditor.ownerTrack == 0);
    assert(state.patternEditor.windowStart == 0);
    assert(state.patternEditor.focusedField == seq::SequencerPatternEditorField::LENGTH);
    assert(state.patternEditor.focusedLayer == seq::SequencerPatternEditorLayer::NOTES);
}

}  // namespace

int main() {
    test_open_retains_exact_owner_page_and_wrapped_navigation();
    test_fields_use_canonical_pattern_authorities_and_exact_marker_ranges();
    test_division_is_direct_and_length_repairs_window();
    test_layers_are_dense_and_offer_only_first_free_lane_before_region();
    test_reset_closes_session_and_restores_compact_defaults();
    std::cout << "All SequencerPatternEditorOps tests passed.\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/sequencer/SequencerClipRegionOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

using core::state::sequencer::SequencerClipPlaybackRegion;
using core::state::sequencer::SequencerClipSnapshot;
using core::state::sequencer::SequencerClipState;
using core::state::sequencer::SequencerPatternSnapshot;
using core::state::sequencer::SequencerPatternState;
using core::state::sequencer::clipPlaybackRegion;
using core::state::sequencer::insertedClipPlaybackRegion;
using core::state::sequencer::removedClipPlaybackRegion;
using core::state::sequencer::resizedClipPlaybackRegion;
using core::state::sequencer::sequencerTicksPerStep;
using core::state::sequencer::setClipPlaybackRegion;

void expectRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t length,
    uint8_t playStart,
    uint8_t loopStart,
    uint8_t loopEnd
) {
    assert(region.isValid());
    assert(region.contentLength == length);
    assert(region.playStart == playStart);
    assert(region.loopStart == loopStart);
    assert(region.loopEnd == loopEnd);
}

void test_default_and_rejected_regions_leave_state_canonical() {
    SequencerPatternState pattern;
    SequencerClipState clip;
    expectRegion(clipPlaybackRegion(pattern, clip), 8, 0, 0, 8);

    const uint32_t revision = pattern.patternTimingRevision.get();
    assert(!setClipPlaybackRegion(pattern, clip, {16, 9, 8, 16}));
    assert(!setClipPlaybackRegion(pattern, clip, {16, 0, 8, 8}));
    assert(!setClipPlaybackRegion(pattern, clip, {0, 0, 0, 0}));
    expectRegion(clipPlaybackRegion(pattern, clip), 8, 0, 0, 8);
    assert(pattern.patternTimingRevision.get() == revision);
}

void test_division_conversion_rejects_noncanonical_values() {
    assert(sequencerTicksPerStep(0U) == 0U);
    assert(sequencerTicksPerStep(5U) == 0U);
    assert(sequencerTicksPerStep(25U) == 0U);
    assert(sequencerTicksPerStep(4U) == 6U);

    SequencerPatternState pattern;
    SequencerClipState clip;
    const uint8_t division = pattern.stepsPerBeat.get();
    assert(!core::state::sequencer::setClipPatternStepsPerBeat(
        pattern,
        clip,
        0U
    ));
    assert(!core::state::sequencer::setClipPatternStepsPerBeat(
        pattern,
        clip,
        25U
    ));
    assert(pattern.stepsPerBeat.get() == division);
}

void test_set_and_resize_are_single_timing_mutations() {
    core::state::sequencer::SequencerState state;
    const uint32_t initialRevision = state.clipRevision.get();
    assert(setClipPlaybackRegion(state, {8, 1, 2, 8}));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 8, 1, 2, 8);
    assert(state.clipRevision.get() == initialRevision + 1U);

    assert(core::state::sequencer::resizeClipPatternContent(state, 16));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 16, 1, 2, 16);
    assert(state.clipRevision.get() == initialRevision + 2U);

    assert(setClipPlaybackRegion(state, {16, 6, 10, 12}));
    assert(core::state::sequencer::resizeClipPatternContent(state, 8));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 8, 6, 7, 8);

    const uint32_t finalRevision = state.clipRevision.get();
    assert(!core::state::sequencer::resizeClipPatternContent(state, 0));
    assert(!core::state::sequencer::resizeClipPatternContent(state, 129));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 8, 6, 7, 8);
    assert(state.clipRevision.get() == finalRevision);
}

void test_pure_resize_preserves_partial_loop_and_extends_full_loop() {
    expectRegion(resizedClipPlaybackRegion({8, 1, 2, 8}, 16), 16, 1, 2, 16);
    expectRegion(resizedClipPlaybackRegion({8, 1, 2, 6}, 16), 16, 1, 2, 6);
    expectRegion(resizedClipPlaybackRegion({16, 6, 10, 16}, 8), 8, 6, 7, 8);
    assert(!resizedClipPlaybackRegion({8, 0, 0, 8}, 0).isValid());
}

void test_insert_and_remove_shift_or_collapse_each_boundary() {
    expectRegion(
        insertedClipPlaybackRegion({16, 2, 4, 12}, 4, 4),
        20,
        2,
        8,
        16
    );
    expectRegion(
        insertedClipPlaybackRegion({8, 0, 0, 8}, 8, 8),
        16,
        0,
        0,
        16
    );
    expectRegion(
        removedClipPlaybackRegion({20, 2, 8, 16}, 4, 6),
        14,
        2,
        4,
        10
    );
    expectRegion(
        removedClipPlaybackRegion({16, 0, 8, 16}, 8, 8),
        8,
        0,
        7,
        8
    );
    assert(!insertedClipPlaybackRegion({8, 0, 0, 8}, 9, 1).isValid());
    assert(!removedClipPlaybackRegion({8, 0, 0, 8}, 0, 8).isValid());
}

void test_snapshot_round_trip_preserves_region_exactly() {
    SequencerPatternState source;
    SequencerClipState sourceClip;
    assert(setClipPlaybackRegion(source, sourceClip, {24, 3, 7, 19}));

    SequencerPatternSnapshot snapshot{};
    SequencerClipSnapshot clipSnapshot{};
    core::state::sequencer::captureSnapshot(source, snapshot);
    core::state::sequencer::captureSnapshot(sourceClip, clipSnapshot);
    assert(snapshot.length == 24);

    SequencerPatternState restored;
    SequencerClipState restoredClip;
    core::state::sequencer::applySnapshot(restored, snapshot);
    core::state::sequencer::applySnapshot(restoredClip, clipSnapshot);
    expectRegion(clipPlaybackRegion(restored, restoredClip), 24, 3, 7, 19);
}

void test_page_transforms_keep_region_and_cc_lane_in_lockstep() {
    core::state::sequencer::SequencerState state;
    assert(setClipPlaybackRegion(state, {16, 0, 8, 16}));
    auto* bank = core::state::sequencer::ensureSequencerCcLaneBank(state.pattern);
    assert(bank != nullptr);
    core::state::sequencer::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74;
    assert(core::state::sequencer::createSequencerCcLane(*bank, 0, draft).changed());
    assert(core::state::sequencer::setSequencerCcLaneEvent(
        *bank,
        0,
        10,
        91
    ).changed());
    assert(core::state::sequencer::setSequencerCcLaneTransition(
        *bank,
        0,
        10,
        core::state::sequencer::SequencerCcLaneTransition::EASE_OUT
    ).changed());

    assert(core::state::sequencer::insertPage(state, 1));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 24, 0, 16, 24);
    bank = state.pattern.ccLanes.get();
    assert(bank != nullptr);
    assert(!bank->lanes[0].activeMask.test(10));
    assert(bank->lanes[0].activeMask.test(18));
    assert(bank->lanes[0].values[18] == 91);
    assert(core::state::sequencer::sequencerCcLaneTransition(
        bank->lanes[0],
        18
    ) == core::state::sequencer::SequencerCcLaneTransition::EASE_OUT);

    assert(core::state::sequencer::deletePage(state, 0));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 16, 0, 8, 16);
    assert(bank->lanes[0].activeMask.test(10));
    assert(bank->lanes[0].values[10] == 91);

    assert(core::state::sequencer::rotatePattern(state, 1));
    expectRegion(clipPlaybackRegion(state.pattern, state.clip), 16, 0, 8, 16);
    assert(!bank->lanes[0].activeMask.test(10));
    assert(bank->lanes[0].activeMask.test(11));
    assert(bank->lanes[0].values[11] == 91);
}

}  // namespace

int main() {
    test_default_and_rejected_regions_leave_state_canonical();
    test_division_conversion_rejects_noncanonical_values();
    test_set_and_resize_are_single_timing_mutations();
    test_pure_resize_preserves_partial_loop_and_extends_full_loop();
    test_insert_and_remove_shift_or_collapse_each_boundary();
    test_snapshot_round_trip_preserves_region_exactly();
    test_page_transforms_keep_region_and_cc_lane_in_lockstep();
    std::cout << "SequencerClipRegionOps tests passed\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>

#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

using core::state::sequencer::SequencerPatternPlaybackRegion;
using core::state::sequencer::SequencerPatternSnapshot;
using core::state::sequencer::SequencerPatternState;
using core::state::sequencer::insertedPatternPlaybackRegion;
using core::state::sequencer::patternPlaybackRegion;
using core::state::sequencer::removedPatternPlaybackRegion;
using core::state::sequencer::resizedPatternPlaybackRegion;
using core::state::sequencer::setPatternPlaybackRegion;

void expectRegion(
    const SequencerPatternPlaybackRegion& region,
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
    expectRegion(patternPlaybackRegion(pattern), 8, 0, 0, 8);

    const uint32_t revision = pattern.patternTimingRevision.get();
    assert(!setPatternPlaybackRegion(pattern, {16, 9, 8, 16}));
    assert(!setPatternPlaybackRegion(pattern, {16, 0, 8, 8}));
    assert(!setPatternPlaybackRegion(pattern, {0, 0, 0, 0}));
    expectRegion(patternPlaybackRegion(pattern), 8, 0, 0, 8);
    assert(pattern.patternTimingRevision.get() == revision);
}

void test_set_and_resize_are_single_timing_mutations() {
    SequencerPatternState pattern;
    const uint32_t initialRevision = pattern.patternTimingRevision.get();
    assert(setPatternPlaybackRegion(pattern, {8, 1, 2, 8}));
    expectRegion(patternPlaybackRegion(pattern), 8, 1, 2, 8);
    assert(pattern.patternTimingRevision.get() == initialRevision + 1U);

    assert(core::state::sequencer::resizePatternContent(pattern, 16));
    expectRegion(patternPlaybackRegion(pattern), 16, 1, 2, 16);
    assert(pattern.patternTimingRevision.get() == initialRevision + 2U);

    assert(setPatternPlaybackRegion(pattern, {16, 6, 10, 12}));
    assert(core::state::sequencer::resizePatternContent(pattern, 8));
    expectRegion(patternPlaybackRegion(pattern), 8, 6, 7, 8);

    const uint32_t finalRevision = pattern.patternTimingRevision.get();
    assert(!core::state::sequencer::resizePatternContent(pattern, 0));
    assert(!core::state::sequencer::resizePatternContent(pattern, 129));
    expectRegion(patternPlaybackRegion(pattern), 8, 6, 7, 8);
    assert(pattern.patternTimingRevision.get() == finalRevision);
}

void test_pure_resize_preserves_partial_loop_and_extends_full_loop() {
    expectRegion(resizedPatternPlaybackRegion({8, 1, 2, 8}, 16), 16, 1, 2, 16);
    expectRegion(resizedPatternPlaybackRegion({8, 1, 2, 6}, 16), 16, 1, 2, 6);
    expectRegion(resizedPatternPlaybackRegion({16, 6, 10, 16}, 8), 8, 6, 7, 8);
    assert(!resizedPatternPlaybackRegion({8, 0, 0, 8}, 0).isValid());
}

void test_insert_and_remove_shift_or_collapse_each_boundary() {
    expectRegion(
        insertedPatternPlaybackRegion({16, 2, 4, 12}, 4, 4),
        20,
        2,
        8,
        16
    );
    expectRegion(
        insertedPatternPlaybackRegion({8, 0, 0, 8}, 8, 8),
        16,
        0,
        0,
        16
    );
    expectRegion(
        removedPatternPlaybackRegion({20, 2, 8, 16}, 4, 6),
        14,
        2,
        4,
        10
    );
    expectRegion(
        removedPatternPlaybackRegion({16, 0, 8, 16}, 8, 8),
        8,
        0,
        7,
        8
    );
    assert(!insertedPatternPlaybackRegion({8, 0, 0, 8}, 9, 1).isValid());
    assert(!removedPatternPlaybackRegion({8, 0, 0, 8}, 0, 8).isValid());
}

void test_snapshot_round_trip_preserves_region_exactly() {
    SequencerPatternState source;
    assert(setPatternPlaybackRegion(source, {24, 3, 7, 19}));

    SequencerPatternSnapshot snapshot{};
    core::state::sequencer::captureSnapshot(source, snapshot);
    assert(snapshot.length == 24);
    assert(snapshot.playStart == 3);
    assert(snapshot.loopStart == 7);
    assert(snapshot.loopEnd == 19);

    SequencerPatternState restored;
    core::state::sequencer::applySnapshot(restored, snapshot);
    expectRegion(patternPlaybackRegion(restored), 24, 3, 7, 19);
}

void test_page_transforms_keep_region_and_cc_lane_in_lockstep() {
    core::state::sequencer::SequencerState state;
    assert(setPatternPlaybackRegion(state.pattern, {16, 0, 8, 16}));
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
    expectRegion(patternPlaybackRegion(state.pattern), 24, 0, 16, 24);
    bank = state.pattern.ccLanes.get();
    assert(bank != nullptr);
    assert(!bank->lanes[0].activeMask.test(10));
    assert(bank->lanes[0].activeMask.test(18));
    assert(bank->lanes[0].values[18] == 91);
    assert(core::state::sequencer::sequencerCcLaneTransition(
        bank->lanes[0],
        18
    ) == core::state::sequencer::SequencerCcLaneTransition::EASE_OUT);

    assert(core::state::sequencer::removePage(state, 0));
    expectRegion(patternPlaybackRegion(state.pattern), 16, 0, 8, 16);
    assert(bank->lanes[0].activeMask.test(10));
    assert(bank->lanes[0].values[10] == 91);

    assert(core::state::sequencer::rotatePattern(state, 1));
    expectRegion(patternPlaybackRegion(state.pattern), 16, 0, 8, 16);
    assert(!bank->lanes[0].activeMask.test(10));
    assert(bank->lanes[0].activeMask.test(11));
    assert(bank->lanes[0].values[11] == 91);
}

void test_page_clipboard_never_imports_or_moves_root_markers() {
    core::state::sequencer::SequencerState source;
    assert(setPatternPlaybackRegion(source.pattern, {16, 2, 6, 14}));
    source.pattern.note[0] = 72;
    source.pattern.setEnabled(0, true);
    core::state::SequencerPageClipboard clipboard{};
    assert(core::handler::capturePageClipboard(source, 0, clipboard));

    core::state::sequencer::SequencerState destination;
    assert(setPatternPlaybackRegion(destination.pattern, {16, 1, 4, 12}));
    core::handler::pastePageClipboard(destination, clipboard, nullptr, 2);
    expectRegion(patternPlaybackRegion(destination.pattern), 24, 1, 4, 12);
    assert(destination.pattern.note[16] == 72);
    assert(destination.pattern.isEnabled(16));
}

}  // namespace

int main() {
    test_default_and_rejected_regions_leave_state_canonical();
    test_set_and_resize_are_single_timing_mutations();
    test_pure_resize_preserves_partial_loop_and_extends_full_loop();
    test_insert_and_remove_shift_or_collapse_each_boundary();
    test_snapshot_round_trip_preserves_region_exactly();
    test_page_transforms_keep_region_and_cc_lane_in_lockstep();
    test_page_clipboard_never_imports_or_moves_root_markers();
    std::cout << "SequencerPatternRegionOps tests passed\n";
    return 0;
}

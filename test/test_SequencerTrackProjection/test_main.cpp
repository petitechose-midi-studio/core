#include <cassert>
#include <cstring>
#include <iostream>

#include "state/CoreState.hpp"
#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"
#include "ui/sequencer/SequencerStepGridViewModelBuilder.hpp"
#include "support/CoreStorages.hpp"

namespace core::ui::sequencer::grid {

StepGridFrameState buildStepGridFrameState(
    const core::state::sequencer::SequencerState&,
    oc::note::sequencer::StepSequencerScaleSettings,
    bool
) {
    StepGridFrameState frame{};
    frame.tiles[0].inPattern = true;
    frame.tiles[0].enabled = true;
    return frame;
}

}  // namespace core::ui::sequencer::grid

// These firmware-only view-model sources are outside the native source filter.
#include "ui/sequencer/StepPropertyVisuals.cpp"
#include "ui/sequencer/SequencerHeaderViewModelBuilder.cpp"
#include "ui/sequencer/SequencerStepGridViewModelBuilder.cpp"

namespace {

core::ui::sequencer::SequencerViewModelSource sourceFor(
    core::state::CoreState& state
) {
    return {
        .sequencer = state.sequencer,
        .tracks = state.sequencerTracks,
        .projectTracks = state.projectTracks,
        .trackNavigation = state.trackNavigation,
        .navigationFocus = state.structureNavigationFocus,
        .sharedTrackActive = state.sharedTrackActive,
        .sharedTrackEnabledMask = state.sharedTrackEnabledMask,
        .structureClipboard = state.structureClipboard,
        .statusBar = state.statusBar,
        .projectNavigation = state.projectNavigation,
        .trackActivations = state.sequencerTrackActivations,
    };
}

void testEmptyTrackPreviewProjectsNoMusicalState() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings);
    state.sequencer.pattern.setEnabled(0U, true);
    state.sequencer.pattern.note[0U] = 72U;

    auto source = sourceFor(state);
    auto frame = core::ui::sequencer::buildSequencerStepGridProps(source);
    assert(frame.tiles[0].inPattern);
    assert(frame.tiles[0].enabled);

    state.structureNavigationFocus.set(
        core::state::StructureNavigationFocus::TRACK
    );
    state.trackNavigation.previewTrackIndex.set(1U);
    state.trackNavigation.previewAddSlot.set(true);
    assert(core::ui::sequencer::sequencerPreviewingEmptyTrack(source));

    frame = core::ui::sequencer::buildSequencerStepGridProps(source);
    for (const auto& tile : frame.tiles) {
        assert(!tile.inPattern);
        assert(!tile.enabled);
        assert(tile.noteEvents.count == 0U);
    }

    const auto header =
        core::ui::sequencer::buildSequencerHeaderBarProps(source);
    assert(std::strcmp(header.leftText, "Track") == 0);
    assert(header.previewTrack == 1U);
    assert(header.length == 0U);
    assert(header.pageText[0] == '\0');
    assert(header.metrics[0].icon[0] == '\0');
    assert(header.metrics[1].icon[0] == '\0');
}

void testDrumTrackAndPatternProjectTheSameMusicalHeader() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings);
    assert(state.sequencerTracks.setTrackKind(
        0U,
        core::state::sequencer::SequencerTrackKind::DRUM,
        true,
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
    ));
    auto& drum = state.sequencer.drumSequencer;
    drum.phase = core::state::sequencer::DrumSequencerPhase::GRID;
    drum.targetTrack = 0U;
    drum.drumTrack = &state.sequencerTracks.drumTrack(0U);
    drum.selectedLane = 1U;

    auto source = sourceFor(state);
    state.structureNavigationFocus.set(
        core::state::StructureNavigationFocus::PAGE
    );
    const auto pattern =
        core::ui::sequencer::buildSequencerHeaderBarProps(source);
    assert(std::strcmp(pattern.leftText, "Pattern") == 0);
    assert(pattern.metrics[0].value[0] != '\0');
    assert(pattern.metrics[1].value[0] != '\0');
    assert(pattern.pageText[0] != '\0');

    state.structureNavigationFocus.set(
        core::state::StructureNavigationFocus::TRACK
    );
    const auto track =
        core::ui::sequencer::buildSequencerHeaderBarProps(source);
    assert(std::strcmp(track.leftText, "Track") == 0);
    assert(track.metrics[0].value == pattern.metrics[0].value);
    assert(track.metrics[1].value == pattern.metrics[1].value);
    assert(track.pageText == pattern.pageText);
}

}  // namespace

int main() {
    testEmptyTrackPreviewProjectsNoMusicalState();
    testDrumTrackAndPatternProjectTheSameMusicalHeader();
    std::cout << "Sequencer Track projection tests passed\n";
    return 0;
}

#include "state/sequencer/SequencerTrackBankOps.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <config/PlatformCompat.hpp>
#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM bool copyPatternToEditor(SequencerState& target, const SequencerPatternState& source) {
    const uint8_t focusedBefore = target.focusedStep.get();

    if (!copyPatternState(target.pattern, source)) return false;

    const uint8_t length = target.pattern.length.get();
    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    return true;
}

FLASHMEM bool copyEditorToPattern(SequencerPatternState& target, const SequencerState& source) {
    return copyPatternState(target, source.pattern);
}

FLASHMEM void resetTransientTrackState(SequencerState& state) {
    state.stepEdit.reset();
    state.stepPropertyInlineSelector.reset();
    state.stepInlineFeedback.reset();
    state.patternQuickControls.reset();
    state.contentView.reset();
}

}  // namespace

FLASHMEM bool initializeTrackBankFromActive(
    SequencerTrackBankState& bank,
    const SequencerState& active
) {
    bank.reset();
    if (!copyEditorToPattern(bank.track(0), active)) return false;
    bank.syncSharedTrackState(0x0001, 0);
    return true;
}

FLASHMEM bool storeActiveTrack(SequencerTrackBankState& bank, const SequencerState& active) {
    return copyEditorToPattern(bank.track(bank.activeTrackIndex()), active);
}

FLASHMEM bool storeActiveTrackPreservingGraph(
    SequencerTrackBankState& bank,
    const SequencerState& active
) {
    auto& target = bank.track(bank.activeTrackIndex());
    const bool targetHasGraph = graphView(target) != nullptr;
    const bool sourceHasGraph = graphView(active.pattern) != nullptr;
    const bool graphSynchronized =
        targetHasGraph == sourceHasGraph &&
        target.graphRevision.get() == active.pattern.graphRevision.get();

    if (!graphSynchronized) {
        return copyEditorToPattern(target, active);
    }

    copyPatternStatePreservingGraph(target, active.pattern);
    return true;
}

FLASHMEM bool switchActiveTrack(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t nextTrack
) {
    const uint8_t current = bank.activeTrackIndex();
    const uint8_t clampedNext = SequencerTrackBankState::clampTrackIndex(nextTrack);
    if (clampedNext == current) {
        return false;
    }

    if (!storeActiveTrack(bank, active)) return false;
    if (!copyPatternToEditor(active, bank.track(clampedNext))) return false;
    resetTransientTrackState(active);

    bank.syncSharedTrackState(bank.currentEnabledMask(), clampedNext);
    return true;
}

FLASHMEM void captureTrackBankSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerTrackBankSnapshot& out
) {
    const uint8_t activeTrack = SequencerTrackBankState::clampTrackIndex(bank.activeTrackIndex());
    out.activeTrack = activeTrack;
    out.enabledMask = bank.currentEnabledMask();
    out.mutedMask = bank.currentMutedMask();
    out.projectScaleRevision = bank.projectScaleRevisionSignal().get();
    out.projectScaleSettings = bank.projectScaleSettings();

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        captureSnapshot((i == activeTrack) ? active.pattern : bank.track(i), out.tracks[i]);
    }
}

FLASHMEM void applyTrackBankSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerTrackBankSnapshot& snapshot
) {
    bank.reset();
    bank.syncSharedTrackState(snapshot.enabledMask, snapshot.activeTrack);
    bank.setMutedMask(snapshot.mutedMask);
    bank.setProjectScaleSettings(snapshot.projectScaleSettings);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        applySnapshot(bank.track(i), snapshot.tracks[i]);
    }

    const uint8_t activeTrack = bank.activeTrackIndex();
    applySnapshotToEditor(active, snapshot.tracks[activeTrack]);
    resetTransientTrackState(active);
}

FLASHMEM void installTrackBankState(
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerTrackBankState& stagedBank,
    SequencerState& stagedActive
) {
    SequencerTrackBankSnapshot snapshot;
    captureTrackBankSnapshot(stagedBank, stagedActive, snapshot);

    std::array<core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>,
               SequencerTrackBankState::TRACK_COUNT> graphs{};
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        graphs[i] = std::move(stagedBank.track(i).graph);
    }
    auto editorGraph = std::move(stagedActive.pattern.graph);

    applyTrackBankSnapshot(bank, active, snapshot);
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        bank.track(i).graph = std::move(graphs[i]);
        bank.track(i).graphRevision.set(snapshot.tracks[i].graphRevision);
    }
    const uint8_t activeTrack = bank.activeTrackIndex();
    active.pattern.graph = std::move(editorGraph);
    active.pattern.graphRevision.set(snapshot.tracks[activeTrack].graphRevision);
}

}  // namespace core::state::sequencer

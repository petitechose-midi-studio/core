#include "state/sequencer/SequencerTrackBankOps.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM void copyFlatPatternToEditor(
    SequencerState& target,
    const SequencerPatternState& source
) {
    const uint8_t focusedBefore = target.focusedStep.get();
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source, snapshot);
    applySnapshotToEditorPreservingGraph(target, snapshot);
    target.pattern.ccLaneRevision.set(source.ccLaneRevision.get());

    const uint8_t length = target.pattern.length.get();
    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
}

FLASHMEM bool copyEditorToPattern(SequencerPatternState& target, const SequencerState& source) {
    return copyPatternState(target, source.pattern);
}

FLASHMEM void copyFlatEditorToPattern(
    SequencerPatternState& target,
    const SequencerState& source
) {
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source.pattern, snapshot);
    applySnapshotPreservingGraph(target, snapshot);
    target.ccLaneRevision.set(source.pattern.ccLaneRevision.get());
}

FLASHMEM void exchangeColdPayload(
    SequencerPatternState& left,
    SequencerPatternState& right
) {
    using std::swap;
    swap(left.graph, right.graph);
    swap(left.ccLanes, right.ccLanes);
}

}  // namespace

FLASHMEM void resetTransientTrackState(SequencerState& state) {
    if (state.stepContentDraft.active.get()) {
        state.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    state.stepEdit.reset();
    state.contextSelector.reset();
    state.ccLaneUi.reset();
    state.stepPropertyInlineSelector.reset();
    state.stepInlineFeedback.reset();
    state.patternQuickControls.reset();
    state.contentView.reset();
    state.stepContentDraft.resetSession();
}

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

    return copyPatternStatePreservingGraph(target, active.pattern);
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
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::TRACK
        );
        return false;
    }

    auto& outgoing = bank.track(current);
    auto& incoming = bank.track(clampedNext);

    // Signals in the retained editor must keep their addresses because UI
    // bindings subscribe to them. Copy only the flat values, then rotate the
    // two PSRAM-owned payload pointers through the editor. The active bank slot
    // is intentionally a spare while that Track is edited, as it already was
    // for flat values before this refactor.
    copyFlatEditorToPattern(outgoing, active);
    exchangeColdPayload(active.pattern, outgoing);
    copyFlatPatternToEditor(active, incoming);
    exchangeColdPayload(active.pattern, incoming);
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
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return;
    }
    bank.reset();
    bank.syncSharedTrackState(snapshot.enabledMask, snapshot.activeTrack);
    bank.setProjectScaleSettings(snapshot.projectScaleSettings);
    bank.projectScaleRevisionSignal().set(snapshot.projectScaleRevision);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        applySnapshot(bank.track(i), snapshot.tracks[i]);
    }

    const uint8_t activeTrack = bank.activeTrackIndex();
    applySnapshotToEditor(active, snapshot.tracks[activeTrack]);
    resetTransientTrackState(active);
}

}  // namespace core::state::sequencer

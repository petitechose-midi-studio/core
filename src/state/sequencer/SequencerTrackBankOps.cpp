#include "state/sequencer/SequencerTrackBankOps.hpp"

#include <algorithm>
#include <config/PlatformCompat.hpp>
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

FLASHMEM void resetTrackBank(SequencerTrackBankState& bank, SequencerState& active) {
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::RESET);
        return;
    }
    bank.reset();
    active.selectPattern(bank.track(0U), bank.clip(0U));
    active.resetEditorState();
}

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

FLASHMEM bool switchActiveTrack(
    SequencerTrackBankState& bank, SequencerState& active, uint8_t nextTrack
) {
    const uint8_t next = SequencerTrackBankState::clampTrackIndex(nextTrack);
    if (next == bank.activeTrackIndex()) return false;
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::TRACK);
        return false;
    }
    active.selectPattern(bank.track(next), bank.clip(next));
    const uint8_t length = std::max<uint8_t>(1U, active.pattern().length.get());
    const uint8_t focused = std::min<uint8_t>(active.focusedStep.get(), length - 1U);
    active.focusedStep.set(focused);
    active.page.set(active.pageForStep(focused));
    active.bumpClipRevision();
    resetTransientTrackState(active);
    bank.syncSharedTrackState(bank.currentEnabledMask(), next);
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
        captureSnapshot(bank.track(i), out.tracks[i]);
        out.clips[i] = bank.clip(i);
    }
}

FLASHMEM void applyTrackBankSnapshot(
    SequencerTrackBankState& bank, SequencerState& active,
    const SequencerTrackBankSnapshot& snapshot
) {
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::PROJECT_LOAD);
        return;
    }
    bank.reset();
    bank.syncSharedTrackState(snapshot.enabledMask, snapshot.activeTrack);
    bank.setProjectScaleSettings(snapshot.projectScaleSettings);
    bank.projectScaleRevisionSignal().set(snapshot.projectScaleRevision);
    for (uint8_t i = 0U; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        applySnapshot(bank.track(i), snapshot.tracks[i]);
        bank.clip(i) = snapshot.clips[i];
    }
    active.selectPattern(bank.track(bank.activeTrackIndex()), bank.clip(bank.activeTrackIndex()));
    const uint8_t focused = std::min<uint8_t>(active.focusedStep.get(), active.pattern().length.get() - 1U);
    active.focusedStep.set(focused);
    active.page.set(active.pageForStep(focused));
    active.bumpClipRevision();
    resetTransientTrackState(active);
}

} // namespace core::state::sequencer

#include "state/sequencer/SequencerTrackBankOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM void copyPatternToEditor(SequencerState& target, const SequencerPatternState& source) {
    const uint8_t focusedBefore = target.focusedStep.get();

    copyPatternState(target.pattern, source);

    const uint8_t length = target.pattern.length.get();
    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
}

FLASHMEM void copyEditorToPattern(SequencerPatternState& target, const SequencerState& source) {
    copyPatternState(target, source.pattern);
}

FLASHMEM void resetTransientTrackState(SequencerState& state) {
    state.stepEdit.reset();
    state.stepPropertyInlineSelector.reset();
    state.stepInlineFeedback.reset();
    state.patternQuickControls.reset();
    state.contentView.reset();
}

}  // namespace

FLASHMEM void initializeTrackBankFromActive(SequencerTrackBankState& bank, const SequencerState& active) {
    bank.reset();
    copyEditorToPattern(bank.track(0), active);
    bank.syncSharedTrackState(0x0001, 0);
}

FLASHMEM void storeActiveTrack(SequencerTrackBankState& bank, const SequencerState& active) {
    copyEditorToPattern(bank.track(bank.activeTrackIndex()), active);
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

    storeActiveTrack(bank, active);
    copyPatternToEditor(active, bank.track(clampedNext));
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
    bank.reset();
    bank.syncSharedTrackState(snapshot.enabledMask, snapshot.activeTrack);
    bank.setProjectScaleSettings(snapshot.projectScaleSettings);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        applySnapshot(bank.track(i), snapshot.tracks[i]);
    }

    const uint8_t activeTrack = bank.activeTrackIndex();
    applySnapshotToEditor(active, snapshot.tracks[activeTrack]);
    resetTransientTrackState(active);
}

}  // namespace core::state::sequencer

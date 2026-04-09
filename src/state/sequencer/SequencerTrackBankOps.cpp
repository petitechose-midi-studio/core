#include "state/sequencer/SequencerTrackBankOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM void copyPersistentState(SequencerState& target, const SequencerState& source) {
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source, snapshot);
    applySnapshot(target, snapshot);

    const uint8_t len = target.length.get();
    const uint8_t focused =
        (len == 0)
            ? 0
            : static_cast<uint8_t>(std::min<uint16_t>(source.focusedStep.get(), len - 1U));

    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.activeStepProperty.set(source.activeStepProperty.get());
}

FLASHMEM void resetTransientTrackState(SequencerState& state) {
    state.stepEdit.reset();
    state.stepPropertyInlineSelector.reset();
    state.stepInlineFeedback.reset();
    state.patternQuickControls.reset();
    state.rangeSelection.reset();
}

}  // namespace

FLASHMEM void initializeTrackBankFromActive(SequencerTrackBankState& bank, const SequencerState& active) {
    bank.reset();
    copyPersistentState(bank.track(0), active);
    bank.activeTrack.set(0);
    bank.enabledMask.set(0x0001);
    bank.selector.reset(0);
    bank.selector.snapshotEnabledMask = 0x0001;
}

FLASHMEM void storeActiveTrack(SequencerTrackBankState& bank, const SequencerState& active) {
    copyPersistentState(bank.track(bank.activeTrack.get()), active);
}

FLASHMEM bool switchActiveTrack(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t nextTrack
) {
    const uint8_t current = bank.activeTrack.get();
    const uint8_t clampedNext = SequencerTrackBankState::clampTrackIndex(nextTrack);
    if (clampedNext == current) {
        bank.selector.reset(current);
        return false;
    }

    storeActiveTrack(bank, active);
    copyPersistentState(active, bank.track(clampedNext));
    resetTransientTrackState(active);

    bank.activeTrack.set(clampedNext);
    bank.selector.reset(clampedNext);
    return true;
}

FLASHMEM void captureTrackBankSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerTrackBankSnapshot& out
) {
    const uint8_t activeTrack = SequencerTrackBankState::clampTrackIndex(bank.activeTrack.get());
    out.activeTrack = activeTrack;
    out.enabledMask = bank.enabledMask.get();

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& source = (i == activeTrack) ? active : bank.track(i);
        captureSnapshot(source, out.tracks[i]);
    }
}

FLASHMEM void applyTrackBankSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerTrackBankSnapshot& snapshot
) {
    bank.reset();
    bank.enabledMask.set(snapshot.enabledMask == 0 ? 0x0001 : snapshot.enabledMask);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        applySnapshot(bank.track(i), snapshot.tracks[i]);
    }

    const uint8_t activeTrack = SequencerTrackBankState::clampTrackIndex(snapshot.activeTrack);
    applySnapshot(active, snapshot.tracks[activeTrack]);
    resetTransientTrackState(active);

    bank.activeTrack.set(activeTrack);
    bank.selector.reset(activeTrack);
    bank.selector.snapshotEnabledMask = bank.enabledMask.get();
}

}  // namespace core::state::sequencer

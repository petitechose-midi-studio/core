#include "state/sequencer/SequencerPersistenceWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

FLASHMEM bool SequencerPersistenceWorkflow::savePatternSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    const auto status = state.sequencerPersistence.savePatternSlotStatus(slotIndex, state.sequencer);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Save pattern slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

FLASHMEM persistence::SlotLoadStatus SequencerPersistenceWorkflow::loadPatternSlot(CoreState& state,
                                                                          uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

    if (state.statusBar.playing.get()) {
        SequencerState staged;
        const persistence::SlotLoadStatus status =
            state.sequencerPersistence.loadPatternSlot(slotIndex, staged);
        if (status == persistence::SlotLoadStatus::OK) {
            state.queuePendingSequencerApply(staged);
        }
        return status;
    }

    state.clearPendingSequencerApply();
    const persistence::SlotLoadStatus status =
        state.sequencerPersistence.loadPatternSlot(slotIndex, state.sequencer);
    if (status == persistence::SlotLoadStatus::OK) {
        storeActiveTrack(state.sequencerTracks, state.sequencer);
        state.persistSequencerWorkspace();
    }

    return status;
}

FLASHMEM bool SequencerPersistenceWorkflow::erasePatternSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    const auto status = state.sequencerPersistence.erasePatternSlotStatus(slotIndex);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Erase pattern slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

FLASHMEM bool SequencerPersistenceWorkflow::saveSetSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    storeActiveTrack(state.sequencerTracks, state.sequencer);
    const auto status = state.sequencerPersistence.saveSetSlotStatus(
        slotIndex,
        state.sequencerTracks,
        state.sequencer
    );
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Save set slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

FLASHMEM persistence::SlotLoadStatus SequencerPersistenceWorkflow::loadSetSlot(
    CoreState& state,
    uint8_t slotIndex,
    bool merge
) {
    if (!state.isSequencerPersistenceReady()) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

    if (state.statusBar.playing.get()) {
        SequencerTrackBankState stagedBank;
        SequencerState staged;
        stagedBank.reset();
        staged.reset();
        const persistence::SlotLoadStatus status =
            state.sequencerPersistence.loadSetSlot(slotIndex, stagedBank, staged);
        if (status == persistence::SlotLoadStatus::OK) {
            if (merge) {
                state.queuePendingSequencerApply(staged, true);
            } else {
                SequencerTrackBankSnapshot snapshot;
                captureTrackBankSnapshot(stagedBank, staged, snapshot);
                state.queuePendingSequencerBankApply(snapshot);
            }
        }
        return status;
    }

    state.clearPendingSequencerApply();
    SequencerTrackBankState stagedBank;
    SequencerState staged;
    stagedBank.reset();
    staged.reset();
    const persistence::SlotLoadStatus status =
        state.sequencerPersistence.loadSetSlot(slotIndex, stagedBank, staged);
    if (status == persistence::SlotLoadStatus::OK) {
        if (merge) {
            SequencerPatternSnapshot snapshot;
            captureSnapshot(staged, snapshot);
            mergeSnapshotIntoCurrent(state.sequencer, snapshot);
            storeActiveTrack(state.sequencerTracks, state.sequencer);
        } else {
            SequencerTrackBankSnapshot snapshot;
            captureTrackBankSnapshot(stagedBank, staged, snapshot);
            applyTrackBankSnapshot(state.sequencerTracks, state.sequencer, snapshot);
        }
        state.persistSequencerWorkspace();
    }

    return status;
}

FLASHMEM bool SequencerPersistenceWorkflow::eraseSetSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    const auto status = state.sequencerPersistence.eraseSetSlotStatus(slotIndex);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Erase set slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

}  // namespace core::state::sequencer

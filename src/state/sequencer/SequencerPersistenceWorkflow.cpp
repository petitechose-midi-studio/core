#include "state/sequencer/SequencerPersistenceWorkflow.hpp"

#include <oc/log/Log.hpp>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

bool SequencerPersistenceWorkflow::savePatternSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    const auto status = state.sequencerPersistence.savePatternSlotStatus(slotIndex, state.sequencer);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Save pattern slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

persistence::SlotLoadStatus SequencerPersistenceWorkflow::loadPatternSlot(CoreState& state,
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
        state.persistSequencerWorkspace();
    }

    return status;
}

bool SequencerPersistenceWorkflow::erasePatternSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    const auto status = state.sequencerPersistence.erasePatternSlotStatus(slotIndex);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Erase pattern slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

bool SequencerPersistenceWorkflow::saveSetSlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isSequencerPersistenceReady()) return false;
    const auto status = state.sequencerPersistence.saveSetSlotStatus(slotIndex, state.sequencer);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[SequencerPersistence] Save set slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

persistence::SlotLoadStatus SequencerPersistenceWorkflow::loadSetSlot(
    CoreState& state,
    uint8_t slotIndex,
    bool merge
) {
    if (!state.isSequencerPersistenceReady()) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

    if (state.statusBar.playing.get()) {
        SequencerState staged;
        const persistence::SlotLoadStatus status =
            state.sequencerPersistence.loadSetSlot(slotIndex, staged);
        if (status == persistence::SlotLoadStatus::OK) {
            state.queuePendingSequencerApply(staged, merge);
        }
        return status;
    }

    state.clearPendingSequencerApply();
    SequencerState staged;
    const persistence::SlotLoadStatus status =
        state.sequencerPersistence.loadSetSlot(slotIndex, staged);
    if (status == persistence::SlotLoadStatus::OK) {
        SequencerPatternSnapshot snapshot;
        captureSnapshot(staged, snapshot);
        if (merge) {
            mergeSnapshotIntoCurrent(state.sequencer, snapshot);
        } else {
            applySnapshot(state.sequencer, snapshot);
        }
        state.persistSequencerWorkspace();
    }

    return status;
}

bool SequencerPersistenceWorkflow::eraseSetSlot(CoreState& state, uint8_t slotIndex) {
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

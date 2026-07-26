#include "state/sequencer/SequencerPersistenceWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM core::app::ExtmemUniquePtr<SequencerState> makeSequencerStateScratch() {
    return core::app::makeExtmemUnique<SequencerState>();
}

FLASHMEM core::app::ExtmemUniquePtr<SequencerTrackBankState> makeTrackBankScratch() {
    return core::app::makeExtmemUnique<SequencerTrackBankState>();
}

}  // namespace

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
        auto staged = makeSequencerStateScratch();
        if (!staged) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        const persistence::SlotLoadStatus status =
            state.sequencerPersistence.loadPatternSlot(slotIndex, *staged);
        if (status == persistence::SlotLoadStatus::OK &&
            !state.queuePendingSequencerApply(*staged)) {
            return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        }
        return status;
    }

    state.clearPendingSequencerApply();
    auto staged = makeSequencerStateScratch();
    if (!staged) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
    const persistence::SlotLoadStatus status =
        state.sequencerPersistence.loadPatternSlot(slotIndex, *staged);
    if (status == persistence::SlotLoadStatus::OK) {
        if (!state.clearSequencerHistory()) {
            return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        }
        auto& bankTarget = state.sequencerTracks.track(
            state.sequencerTracks.activeTrackIndex()
        );
        if (!copyPatternState(bankTarget, staged->pattern)) {
            return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        }
        installPatternStateToEditor(state.sequencer, staged->pattern);
        state.markSequencerProjectMutated();
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
    if (!storeActiveTrack(state.sequencerTracks, state.sequencer)) return false;
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
        auto stagedBank = makeTrackBankScratch();
        auto staged = makeSequencerStateScratch();
        if (!stagedBank || !staged) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        stagedBank->reset();
        staged->reset();
        const persistence::SlotLoadStatus status =
            state.sequencerPersistence.loadSetSlot(slotIndex, *stagedBank, *staged);
        if (status == persistence::SlotLoadStatus::OK) {
            if (merge) {
                if (!state.queuePendingSequencerApply(*staged, true)) {
                    return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
                }
            } else {
                if (!state.queuePendingSequencerBankApply(*stagedBank, *staged)) {
                    return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
                }
            }
        }
        return status;
    }

    state.clearPendingSequencerApply();
    auto stagedBank = makeTrackBankScratch();
    auto staged = makeSequencerStateScratch();
    if (!stagedBank || !staged) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
    stagedBank->reset();
    staged->reset();
    const persistence::SlotLoadStatus status =
        state.sequencerPersistence.loadSetSlot(slotIndex, *stagedBank, *staged);
    if (status == persistence::SlotLoadStatus::OK) {
        if (!state.clearSequencerHistory()) {
            return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        }
        if (merge) {
            auto& bankTarget = state.sequencerTracks.track(
                state.sequencerTracks.activeTrackIndex()
            );
            if (!copyGraph(bankTarget, staged->pattern)) {
                return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
            }
            mergePatternStateIntoCurrent(state.sequencer, staged->pattern);
            if (!storeActiveTrack(state.sequencerTracks, state.sequencer)) {
                return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
            }
        } else {
            installTrackBankState(
                state.sequencerTracks,
                state.sequencer,
                *stagedBank,
                *staged
            );
        }
        state.setSharedTrackState(
            state.sequencerTracks.currentEnabledMask(),
            state.sequencerTracks.activeTrackIndex()
        );
        state.markSequencerProjectMutated();
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

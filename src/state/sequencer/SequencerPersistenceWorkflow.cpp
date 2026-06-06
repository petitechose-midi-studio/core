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

FLASHMEM core::app::ExtmemUniquePtr<SequencerTrackBankSnapshot> makeTrackBankSnapshotScratch() {
    return core::app::makeExtmemUnique<SequencerTrackBankSnapshot>();
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
        if (status == persistence::SlotLoadStatus::OK) {
            state.queuePendingSequencerApply(*staged);
        }
        return status;
    }

    state.clearPendingSequencerApply();
    const persistence::SlotLoadStatus status =
        state.sequencerPersistence.loadPatternSlot(slotIndex, state.sequencer);
    if (status == persistence::SlotLoadStatus::OK) {
        storeActiveTrack(state.sequencerTracks, state.sequencer);
        state.clearSequencerHistory();
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
        auto stagedBank = makeTrackBankScratch();
        auto staged = makeSequencerStateScratch();
        if (!stagedBank || !staged) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
        stagedBank->reset();
        staged->reset();
        const persistence::SlotLoadStatus status =
            state.sequencerPersistence.loadSetSlot(slotIndex, *stagedBank, *staged);
        if (status == persistence::SlotLoadStatus::OK) {
            if (merge) {
                state.queuePendingSequencerApply(*staged, true);
            } else {
                state.queuePendingSequencerBankApply(*stagedBank, *staged);
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
        if (merge) {
            SequencerPatternSnapshot snapshot;
            captureSnapshot(staged->pattern, snapshot);
            mergeSnapshotIntoCurrent(state.sequencer, snapshot);
            copyGraph(state.sequencer.pattern, staged->pattern);
            storeActiveTrack(state.sequencerTracks, state.sequencer);
        } else {
            auto snapshot = makeTrackBankSnapshotScratch();
            if (!snapshot) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;
            captureTrackBankSnapshot(*stagedBank, *staged, *snapshot);
            applyTrackBankSnapshot(state.sequencerTracks, state.sequencer, *snapshot);
            for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
                copyGraph(state.sequencerTracks.track(i), stagedBank->track(i));
            }
            copyGraph(state.sequencer.pattern, staged->pattern);
        }
        state.setSharedTrackState(
            state.sequencerTracks.currentEnabledMask(),
            state.sequencerTracks.activeTrackIndex()
        );
        state.clearSequencerHistory();
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

#include "state/CoreState.hpp"

#include <new>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerCcLanePatternOps.hpp"
#include "sequencer/SequencerContentViewOps.hpp"
#include "sequencer/SequencerStructureHistory.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::state {

namespace {

FLASHMEM shared::SharedTrackCoordinator::StateRefs sharedTrackRefs(CoreState& state) {
    return shared::SharedTrackCoordinator::StateRefs{
        state.sharedTrackActive,
        state.sharedTrackEnabledMask,
        state.pages,
        state.sequencerTracks,
        state.sequencer,
    };
}

}  // namespace

FLASHMEM bool CoreState::queuePendingSequencerApply(
    sequencer::SequencerState& staged,
    bool merge
) {
    return queueSequencerApply_(staged, merge);
}

FLASHMEM bool CoreState::queuePendingSequencerBankApply(
    sequencer::SequencerTrackBankState& stagedBank,
    sequencer::SequencerState& staged
) {
    return queueSequencerBankApply_(stagedBank, staged);
}

FLASHMEM void CoreState::clearPendingSequencerApply() {
    clearPendingSequencerApply_();
}

bool CoreState::hasPendingSequencerApply() const {
    return sequencerDomain_.pendingApply && sequencerDomain_.pendingApply->valid;
}

uint16_t CoreState::currentSharedTrackEnabledMask() const {
    return sharedTrackEnabledMask.get();
}

uint8_t CoreState::currentSharedActiveTrack() const {
    return sharedTrackActive.get();
}

bool CoreState::setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    return setSharedTrackState_(enabledMask, activeTrack, true);
}

void CoreState::publishPreparedSequencerTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() ||
         activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK
        );
        return;
    }
    const auto result = shared::SharedTrackCoordinator::publishPreparedSequencerState(
        sharedTrackRefs(*this),
        enabledMask,
        activeTrack
    );
    if (result.changed) {
        requestSharedTrackPersist_();
    }
}

bool CoreState::refreshSharedTrackStateFromMacroPages() {
    return refreshSharedTrackStateFromMacroPages_(true);
}

bool CoreState::refreshSharedTrackStateFromSequencer() {
    return refreshSharedTrackStateFromSequencer_(true);
}

FLASHMEM persistence::PersistenceWriteStatus CoreState::recoverPersistenceFromRamAfterStorageReopen() {
    auto status = settings.saveAllStatus(
        midiSync,
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = settings.saveDataManagerMacroShortcutLeftStatus(
        static_cast<uint8_t>(dataManager.macroShortcutLeft.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerMacroShortcutRightStatus(
        static_cast<uint8_t>(dataManager.macroShortcutRight.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerSeqShortcutLeftStatus(
        static_cast<uint8_t>(dataManager.seqShortcutLeft.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerSeqShortcutRightStatus(
        static_cast<uint8_t>(dataManager.seqShortcutRight.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.commitStatus();
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = macroPersistence.initStatus();
    macroDomain_.persistenceReady = status == persistence::PersistenceWriteStatus::OK;
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = sequencerPersistence.initStatus();
    sequencerDomain_.persistenceReady = status == persistence::PersistenceWriteStatus::OK;
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    sharedTrackPersistPending_ = false;
    sharedTrackPersistTimestampMs_ = 0;
    return persistence::PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreState::queueSequencerApply_(
    sequencer::SequencerState& staged,
    bool merge
) {
    commitSequencerPatternHistoryCoalescing();
    return CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

FLASHMEM bool CoreState::queueSequencerBankApply_(
    sequencer::SequencerTrackBankState& stagedBank,
    sequencer::SequencerState& staged
) {
    commitSequencerPatternHistoryCoalescing();
    return CoreStateLifecycle::queuePendingSequencerBankApply(*this, stagedBank, staged);
}

FLASHMEM void CoreState::requestProjectSessionSave_() {
    if (!projectSessionTrackingEnabled_) return;

    projectSessionSavePending_ = true;
    projectSessionSaveTimestampMs_ = oc::time::millis();
}

FLASHMEM void CoreState::markSequencerProjectMutated_() {
    if (!sequencer::storeActiveTrack(sequencerTracks, sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph");
    }
    markProjectMutated();
}

FLASHMEM void CoreState::requestSharedTrackPersist_() {
    sharedTrackPersistPending_ = true;
    sharedTrackPersistTimestampMs_ = oc::time::millis();
}

FLASHMEM void CoreState::persistSharedTrackState_() {
    if (!sharedTrackPersistPending_) return;

    const auto persistStatus = settings.saveSharedTrackStateStatus(
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (persistStatus == persistence::PersistenceWriteStatus::OK) {
        sharedTrackPersistPending_ = false;
        sharedTrackPersistTimestampMs_ = 0;
        return;
    }

    OC_LOG_WARN("[CoreState] Failed to persist shared track state: {}",
                persistence::persistenceWriteStatusLabel(persistStatus));
    if (persistStatus == persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE) {
        sharedTrackPersistPending_ = true;
        sharedTrackPersistTimestampMs_ = oc::time::millis();
    } else {
        sharedTrackPersistPending_ = false;
        sharedTrackPersistTimestampMs_ = 0;
    }
}

FLASHMEM void CoreState::clearPendingSequencerApply_() {
    CoreStateLifecycle::clearPendingSequencerApply(*this);
}

FLASHMEM bool CoreState::refreshSharedTrackStateFromMacroPages_(bool persist) {
    const uint16_t enabledMask = shared::SharedTrackCoordinator::sanitizeEnabledMask(
        pages.currentTrackEnabledMask()
    );
    const uint8_t activeTrack = shared::SharedTrackCoordinator::sanitizeActiveTrack(
        enabledMask,
        pages.currentActiveTrack()
    );
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() ||
         activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK
        );
        return false;
    }
    const auto result = shared::SharedTrackCoordinator::refreshFromMacroPages(sharedTrackRefs(*this));
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

FLASHMEM bool CoreState::refreshSharedTrackStateFromSequencer_(bool persist) {
    const uint16_t enabledMask = shared::SharedTrackCoordinator::sanitizeEnabledMask(
        sequencerTracks.currentEnabledMask()
    );
    const uint8_t activeTrack = shared::SharedTrackCoordinator::sanitizeActiveTrack(
        enabledMask,
        sequencerTracks.activeTrackIndex()
    );
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() ||
         activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK
        );
        return false;
    }
    const auto result = shared::SharedTrackCoordinator::refreshFromSequencer(sharedTrackRefs(*this));
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

FLASHMEM bool CoreState::setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack, bool persist) {
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() ||
         activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK
        );
        return false;
    }
    commitSequencerPatternHistoryCoalescing();

    const auto result = shared::SharedTrackCoordinator::apply(
        sharedTrackRefs(*this),
        enabledMask,
        activeTrack
    );
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

}  // namespace core::state

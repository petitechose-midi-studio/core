#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <new>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>
#include <utility>

#include "state/CoreState.hpp"

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"

namespace core::state {

namespace {

FLASHMEM shared::SharedTrackCoordinator::StateRefs sharedTrackRefs(CoreState& state) {
    return shared::SharedTrackCoordinator::StateRefs{
        state.sharedTrackActive, state.sharedTrackEnabledMask, state.pages, state.sequencerTracks,
        state.sequencer,
    };
}

}  // namespace

FLASHMEM bool CoreState::queuePendingSequencerApply(sequencer::SequencerState& staged, bool merge) {
    return queueSequencerApply_(staged, merge);
}

FLASHMEM bool CoreState::queuePendingSequencerBankApply(
    sequencer::SequencerTrackBankState& stagedBank, sequencer::SequencerState& staged) {
    return queueSequencerBankApply_(stagedBank, staged);
}

FLASHMEM void CoreState::clearPendingSequencerApply() { clearPendingSequencerApply_(); }

bool CoreState::hasPendingSequencerApply() const {
    return sequencerDomain_.pendingApply && sequencerDomain_.pendingApply->valid;
}

uint16_t CoreState::currentSharedTrackEnabledMask() const { return sharedTrackEnabledMask.get(); }

uint8_t CoreState::currentSharedActiveTrack() const { return sharedTrackActive.get(); }

bool CoreState::setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    return setSharedTrackState_(enabledMask, activeTrack);
}

bool CoreState::publishPreparedSequencerTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK);
        return false;
    }
    const auto result = shared::SharedTrackCoordinator::publishPreparedSequencerState(
        sharedTrackRefs(*this), enabledMask, activeTrack);
    return result.ok;
}

FLASHMEM void CoreState::
reconcilePreparedSequencerActiveTrackPresentation() noexcept {
    macro::MacroWorkflow::syncActivePagePresentation(macros, pages, macroUi);
}

FLASHMEM void CoreState::reconcilePreparedMacroTrackTransfer(uint16_t capturedTrackMask) {
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((capturedTrackMask & static_cast<uint16_t>(1U << track)) == 0U) { continue; }
        (void)macroUi.manualOverrides.clearTrack(track);
    }
    macroUi.refreshManualOverrideMask(pages.currentActiveTrack(), pages.currentActivePage());
    macroUi.automationEditRevision.set(macroUi.automationEditRevision.get() + 1U);
    macroUi.runtimeProjectionRevision.set(macro::nextMacroRuntimeProjectionRevision(
        macroUi.runtimeProjectionRevision.get(), macro::kMacroRuntimeProjectionDirtyConfig));
    macro::MacroWorkflow::syncRuntimeFromActivePage(macros, pages);
    configRevision.set(
        macro::nextMacroConfigRevision(configRevision.get(), macro::kMacroConfigDirtyAll));
    project::reconcileProjectModulatorNavigationAfterHistory(projectNavigation,
                                                             pages.control.authored.modulation);
}

bool CoreState::refreshSharedTrackStateFromMacroPages() {
    return refreshSharedTrackStateFromMacroPages_();
}

bool CoreState::refreshSharedTrackStateFromSequencer() {
    return refreshSharedTrackStateFromSequencer_();
}

FLASHMEM persistence::PersistenceWriteStatus CoreState::recoverSettingsFromRamAfterStorageReopen() {
    return deviceSettingsStore.saveAllStatus(midiSync);
}

FLASHMEM bool CoreState::queueSequencerApply_(sequencer::SequencerState& staged, bool merge) {
    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }
    return CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

FLASHMEM bool CoreState::queueSequencerBankApply_(sequencer::SequencerTrackBankState& stagedBank,
                                                  sequencer::SequencerState& staged) {
    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }
    return CoreStateLifecycle::queuePendingSequencerBankApply(*this, stagedBank, staged);
}

FLASHMEM project::ProjectSaveToken CoreState::requestProjectSessionSave_() {
    if (!projectSessionControl_.trackingEnabled) {
        return projectSessionSaveToken();
    }

    if (projectSessionControl_.requestId == UINT32_MAX &&
        !advanceProjectSessionIdentity_()) {
        return projectSessionSaveToken();
    }

    ++projectSessionControl_.requestId;
    projectSessionControl_.savePending = true;
    projectSessionControl_.requestTimestampMs = oc::time::millis();
    return projectSessionSaveToken();
}

FLASHMEM void CoreState::markSequencerProjectMutated_() {
    if (!sequencer::storeActiveTrack(sequencerTracks, sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph");
    }
    markProjectMutated();
}

FLASHMEM void CoreState::clearPendingSequencerApply_() {
    CoreStateLifecycle::clearPendingSequencerApply(*this);
}

FLASHMEM bool CoreState::refreshSharedTrackStateFromMacroPages_() {
    const uint16_t enabledMask =
        shared::SharedTrackCoordinator::sanitizeEnabledMask(pages.currentTrackEnabledMask());
    const uint8_t activeTrack = shared::SharedTrackCoordinator::sanitizeActiveTrack(
        enabledMask, pages.currentActiveTrack());
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK);
        return false;
    }
    const bool changesTrackState =
        enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get();
    if (changesTrackState && commitSequencerPatternHistoryCoalescing_() ==
                                 SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }
    // Preserve the Macro-authored request across the commit barrier. Active
    // Pattern publication may reconcile shared state from the still-current
    // Sequencer Track; re-reading MacroPages afterwards would then lose the
    // transition that caused this call.
    const auto result =
        shared::SharedTrackCoordinator::apply(sharedTrackRefs(*this), enabledMask, activeTrack);
    return result.changed;
}

FLASHMEM bool CoreState::refreshSharedTrackStateFromSequencer_() {
    const uint16_t enabledMask =
        shared::SharedTrackCoordinator::sanitizeEnabledMask(sequencerTracks.currentEnabledMask());
    const uint8_t activeTrack = shared::SharedTrackCoordinator::sanitizeActiveTrack(
        enabledMask, sequencerTracks.activeTrackIndex());
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK);
        return false;
    }
    const bool changesTrackState =
        enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get();
    if (changesTrackState && commitSequencerPatternHistoryCoalescing_() ==
                                 SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }
    const auto result =
        shared::SharedTrackCoordinator::refreshFromSequencer(sharedTrackRefs(*this));
    return result.changed;
}

FLASHMEM bool CoreState::setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack) {
    if (sequencer.stepContentDraft.active.get() &&
        (enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get())) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK);
        return false;
    }
    const bool changesTrackState =
        enabledMask != sharedTrackEnabledMask.get() || activeTrack != sharedTrackActive.get();
    if (changesTrackState && commitSequencerPatternHistoryCoalescing_() ==
                                 SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }

    const auto result =
        shared::SharedTrackCoordinator::apply(sharedTrackRefs(*this), enabledMask, activeTrack);
    return result.changed;
}

}  // namespace core::state

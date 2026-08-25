#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "state/CoreState.hpp"

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/CoreStateBootstrap.hpp"
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

FLASHMEM bool closeClipMutationBoundary(CoreState& state) {
    if (state.sequencer.stepContentDraft.rejectTransitionIfActive(
            sequencer::SequencerStepContentDraftBlockedTransition::TRACK)) {
        return false;
    }
    return state.commitSequencerPatternHistoryCoalescingOutcome() !=
               sequencer::SequencerPatternHistoryCommitOutcome::Failed &&
        state.commitSequencerDrumHistoryCoalescingOutcome() !=
               sequencer::SequencerPatternHistoryCommitOutcome::Failed;
}

FLASHMEM uint32_t clipLoopTicks(
    const CoreState& state,
    sequencer::SequencerClipAddress address
) {
    if (!sequencer::SequencerClipGridState::validAddress(address) ||
        !state.sequencerClips.isOccupied(address)) {
        return 0U;
    }
    uint16_t loopStart = 0U;
    uint16_t loopEnd = 0U;
    if (state.sequencerClips.isResident(address)) {
        const auto& clip = sequencer::canonicalTrackClip(
            state.sequencerTracks, state.sequencer, address.track);
        loopStart = clip.loopStartTick;
        loopEnd = clip.loopEndTick;
    } else {
        const auto* document = state.sequencerClips.inactiveDocument(address);
        if (document == nullptr) return 0U;
        loopStart = document->clip.loopStartTick;
        loopEnd = document->clip.loopEndTick;
    }
    return loopEnd > loopStart
        ? static_cast<uint32_t>(loopEnd - loopStart)
        : 0U;
}

}  // namespace

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
    if (result.ok) {
        sequencerClips.synchronizeEnabledTracks(result.enabledMask);
        sequencerClipLaunches.synchronizeEnabledTracks(
            sequencerClips, result.enabledMask);
    }
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

FLASHMEM bool CoreState::switchSequencerClipForEditing(
    sequencer::SequencerClipAddress target
) {
    if (!closeClipMutationBoundary(*this) ||
        !sequencer::switchResidentSequencerClip(
            sequencerClips, sequencerTracks, sequencer, target)) {
        return false;
    }
    sequencer::refreshContentView(sequencer);
    sequencer.contentView.bump();
    markSequencerProjectMutated_();
    return true;
}

FLASHMEM bool CoreState::requestSequencerClipLaunch(
    sequencer::SequencerClipAddress target,
    sequencer::SequencerClipLaunchQuantization quantization
) {
    if (!sequencer::SequencerClipGridState::validAddress(target)) {
        return false;
    }
    const uint16_t bit = static_cast<uint16_t>(1U << target.track);
    if ((sequencerTrackActivations.pendingTrackMask() & bit) != 0U) {
        return false;
    }
    return sequencerClipLaunches.request(
        target,
        sequencerClips,
        statusBar.playing.get(),
        quantization,
        clipLoopTicks(*this, target));
}

FLASHMEM bool CoreState::requestSequencerTrackStop(
    uint8_t track,
    sequencer::SequencerClipLaunchQuantization quantization
) {
    if (track >= sequencer::SequencerClipGridState::TRACK_COUNT) return false;
    const uint16_t bit = static_cast<uint16_t>(1U << track);
    if ((currentSharedTrackEnabledMask() & bit) == 0U) {
        return false;
    }
    return sequencerClipLaunches.requestStop(
        track,
        statusBar.playing.get(),
        quantization
    );
}

FLASHMEM bool CoreState::requestSequencerSceneLaunch(
    uint8_t slot,
    sequencer::SequencerClipLaunchQuantization quantization
) {
    if (slot >= sequencer::SequencerClipGridState::SLOT_COUNT) return false;
    const uint16_t enabledMask = currentSharedTrackEnabledMask();
    uint16_t sceneMask = 0U;
    std::array<uint32_t, sequencer::SequencerClipLaunchQueue::TRACK_COUNT>
        loopTicks{};
    for (uint8_t track = 0U;
         track < sequencer::SequencerClipLaunchQueue::TRACK_COUNT;
         ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((enabledMask & bit) == 0U) continue;
        const sequencer::SequencerClipAddress address{track, slot};
        if (sequencerClips.slotKind(address) ==
            sequencer::SequencerLauncherSlotKind::EMPTY) {
            continue;
        }
        sceneMask = static_cast<uint16_t>(sceneMask | bit);
        loopTicks[track] = clipLoopTicks(*this, address);
    }
    if (sceneMask == 0U ||
        (sequencerTrackActivations.pendingTrackMask() & sceneMask) != 0U) {
        return false;
    }
    return sequencerClipLaunches.requestScene(
        slot,
        sequencerClips,
        enabledMask,
        statusBar.playing.get(),
        quantization,
        &loopTicks
    );
}

FLASHMEM bool CoreState::setSequencerStopSlot(
    sequencer::SequencerClipAddress target,
    bool stop
) {
    if (!closeClipMutationBoundary(*this) ||
        !sequencer::SequencerClipGridState::validAddress(target) ||
        !sequencerTracks.isTrackEnabled(target.track) ||
        sequencerClipLaunches.references(target)) {
        return false;
    }
    const bool changed = stop
        ? sequencerClips.setStop(target)
        : sequencerClips.clearStop(target);
    if (changed) markSequencerProjectMutated_();
    return changed;
}

FLASHMEM bool CoreState::setSequencerClipBehavior(
    sequencer::SequencerClipAddress target,
    sequencer::SequencerLauncherBehavior behavior
) {
    if (!closeClipMutationBoundary(*this) ||
        !sequencerClips.setClipBehavior(target, behavior)) {
        return false;
    }
    sequencerClipLaunches.refreshBehavior(target, behavior);
    markSequencerProjectMutated_();
    return true;
}

FLASHMEM bool CoreState::setSequencerSceneBehavior(
    uint8_t slot,
    sequencer::SequencerLauncherBehavior behavior
) {
    if (!closeClipMutationBoundary(*this) ||
        !sequencerClips.setSceneBehavior(slot, behavior)) {
        return false;
    }
    markSequencerProjectMutated_();
    return true;
}

FLASHMEM bool CoreState::createSequencerClip(
    sequencer::SequencerClipAddress target
) {
    if (!sequencer::SequencerClipGridState::validAddress(target) ||
        !sequencerTracks.isTrackEnabled(target.track) ||
        sequencerClips.isOccupied(target)) {
        return false;
    }
    const auto kind = sequencerTracks.trackKind(target.track);
    sequencer::SequencerClipDocumentPtr document;
    if (!sequencer::createEmptySequencerClipDocument(
            kind,
            kind == sequencer::SequencerTrackKind::DRUM
                ? &sequencerTracks.drumTrack(target.track)
                : nullptr,
            document)) {
        return false;
    }
    return installSequencerClip(target, std::move(document), false);
}

FLASHMEM bool CoreState::installSequencerClip(
    sequencer::SequencerClipAddress target,
    sequencer::SequencerClipDocumentPtr document,
    bool duplicate,
    sequencer::SequencerLauncherBehavior behavior
) {
    if (!document || !closeClipMutationBoundary(*this) ||
        !sequencer::SequencerClipGridState::validAddress(target) ||
        sequencerClips.slotKind(target) !=
            sequencer::SequencerLauncherSlotKind::EMPTY ||
        !sequencerTracks.isTrackEnabled(target.track) ||
        document->trackKind != sequencerTracks.trackKind(target.track)) {
        return false;
    }
    auto change = sequencer::prepareSequencerClipInstallChange(
        duplicate ? sequencer::SequencerClipStructureAction::DUPLICATE_CLIP
                  : sequencer::SequencerClipStructureAction::CREATE,
        target,
        std::move(document),
        behavior);
    if (!change || !sequencerHistory.canRecordClipStructure(*change) ||
        !sequencer::applySequencerClipStructureChange(
            sequencerClips, *change, true)) {
        return false;
    }
    sequencerHistory.commitAdmittedClipStructure(std::move(change));
    markSequencerProjectMutated_();
    return true;
}

FLASHMEM bool CoreState::deleteSequencerClip(
    sequencer::SequencerClipAddress target
) {
    if (!closeClipMutationBoundary(*this) ||
        !sequencer::canDeleteSequencerClip(
            sequencerClips,
            sequencerClipLaunches,
            target,
            statusBar.playing.get())) {
        return false;
    }
    const bool resident = sequencerClips.isResident(target);
    auto change = resident
        ? sequencer::prepareSequencerResidentClipDeleteChange(
              sequencerClips, sequencerTracks, sequencer, target)
        : sequencer::prepareSequencerClipDeleteChange(
              sequencerClips, target);
    if (!change || !sequencerHistory.canRecordClipStructure(*change) ||
        !sequencer::applySequencerClipStructureChange(
            sequencerClips,
            sequencerTracks,
            sequencer,
            *change,
            true)) {
        return false;
    }
    sequencerClipLaunches.synchronizeEnabledTracks(
        sequencerClips, sequencerTracks.currentEnabledMask());
    sequencerHistory.commitAdmittedClipStructure(std::move(change));
    markSequencerProjectMutated_();
    return true;
}

FLASHMEM bool CoreState::moveSequencerClip(
    sequencer::SequencerClipAddress source,
    sequencer::SequencerClipAddress destination
) {
    if (!closeClipMutationBoundary(*this) ||
        sequencerClipLaunches.references(source) ||
        !sequencer::canTransferSequencerClip(
            sequencerClips,
            sequencerTracks,
            source,
            destination,
            sequencer::SequencerClipStructureAction::MOVE)) {
        return false;
    }
    auto change = sequencer::prepareSequencerClipMoveChange(
        sequencerClips, source, destination);
    if (!change || !sequencerHistory.canRecordClipStructure(*change) ||
        !sequencer::applySequencerClipStructureChange(
            sequencerClips, *change, true)) {
        return false;
    }
    sequencerHistory.commitAdmittedClipStructure(std::move(change));
    markSequencerProjectMutated_();
    return true;
}

FLASHMEM bool CoreState::duplicateSequencerClip(
    sequencer::SequencerClipAddress source,
    sequencer::SequencerClipAddress destination
) {
    if (!sequencer::canTransferSequencerClip(
            sequencerClips,
            sequencerTracks,
            source,
            destination,
            sequencer::SequencerClipStructureAction::DUPLICATE_CLIP)) {
        return false;
    }

    sequencer::SequencerClipDocumentPtr document;
    if (sequencerClips.isResident(source)) {
        const auto kind = sequencerTracks.trackKind(source.track);
        if (!sequencer::captureSequencerClipDocument(
                sequencer::canonicalTrackPattern(
                    sequencerTracks, sequencer, source.track),
                sequencer::canonicalTrackClip(
                    sequencerTracks, sequencer, source.track),
                kind,
                kind == sequencer::SequencerTrackKind::DRUM
                    ? &sequencerTracks.drumTrack(source.track)
                    : nullptr,
                document)) {
            return false;
        }
    } else {
        const auto* sourceDocument = sequencerClips.inactiveDocument(source);
        if (sourceDocument == nullptr ||
            !sequencer::cloneSequencerClipDocument(*sourceDocument, document)) {
            return false;
        }
    }
    return installSequencerClip(
        destination,
        std::move(document),
        true,
        sequencerClips.clipBehavior(source)
    );
}

FLASHMEM persistence::PersistenceWriteStatus CoreState::recoverSettingsFromRamAfterStorageReopen() {
    return deviceSettingsStore.reconcileAllStatus(midiSync, midiNoteDisplay);
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
    if (result.ok) {
        sequencerClips.synchronizeEnabledTracks(result.enabledMask);
        sequencerClipLaunches.synchronizeEnabledTracks(
            sequencerClips, result.enabledMask);
    }
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
    if (result.ok) {
        sequencerClips.synchronizeEnabledTracks(result.enabledMask);
        sequencerClipLaunches.synchronizeEnabledTracks(
            sequencerClips, result.enabledMask);
    }
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
    if (result.ok) {
        sequencerClips.synchronizeEnabledTracks(result.enabledMask);
        sequencerClipLaunches.synchronizeEnabledTracks(
            sequencerClips, result.enabledMask);
    }
    return result.changed;
}

}  // namespace core::state

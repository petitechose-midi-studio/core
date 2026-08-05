#include "state/CoreStateLifecycle.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {

FLASHMEM void reprojectActiveMacroManualOverrides(CoreState& state) {
    const uint8_t track = state.pages.currentActiveTrack();
    const uint8_t page = state.pages.currentActivePage();
    state.macroUi.refreshManualOverrideMask(track, page);

    for (uint8_t macro = 0; macro < core::state::macro::MACRO_COUNT; ++macro) {
        float manualValue = 0.0f;
        if (!state.macroUi.manualOverrides.valueFor(
                core::state::macro::MacroAutomationSlotAddress{
                    .track = track,
                    .page = page,
                    .macro = macro,
                },
                manualValue
            )) {
            continue;
        }
        core::state::macro::MacroWorkflow::setRuntimeValue(state.macros, macro, manualValue);
    }
}

}  // namespace

void CoreStateLifecycle::updateMutationCoalescers_(CoreState& state) {
    if (state.macroDomain_.mutationCoalescer) {
        state.macroDomain_.mutationCoalescer->update();
    }
    if (state.sequencerDomain_.mutationCoalescer) {
        state.sequencerDomain_.mutationCoalescer->update();
    }
}

FLASHMEM void CoreStateLifecycle::flushMutationCoalescers_(CoreState& state) {
    if (state.macroDomain_.mutationCoalescer) {
        state.macroDomain_.mutationCoalescer->flush();
    }
    if (state.sequencerDomain_.mutationCoalescer) {
        state.sequencerDomain_.mutationCoalescer->flush();
    }
}

FLASHMEM void CoreStateLifecycle::persistFactoryDefaults_(CoreState& state) {
    const auto saveStatus =
        state.deviceSettingsStore.saveAllStatus(state.midiSync);
    if (saveStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist default device settings during factory reset: {}",
                    persistence::persistenceWriteStatusLabel(saveStatus));
        return;
    }

}

FLASHMEM void CoreStateLifecycle::resetMacroDomain_(CoreState& state) {
    state.pages.initDefaults();
    state.midiSync.reset();
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.macroEdit.reset();
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    state.trackNavigation.reset();
}

FLASHMEM void CoreStateLifecycle::resetSequencerDomain_(CoreState& state) {
    state.sequencer.reset();
    state.sequencerTracks.reset();
    if (!sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to initialize sequencer track bank");
    }
    state.requestSequencerRuntimeProjectReset();
}

FLASHMEM void CoreStateLifecycle::resetUiState_(CoreState& state) {
    state.viewSelector.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    // Factory reset already cleared Project-scoped Macro runtime in
    // resetMacroDomain_. This second pass owns UI/session state only.
    state.macroUi.resetInteraction();
    state.projectNavigation.reset();
    state.projectTrackEditor.reset();
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.hideAll();
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    state.refreshSharedTrackStateFromMacroPages_();
}

void CoreStateLifecycle::update(CoreState& state) {
    const uint32_t nowMs = oc::time::millis();
    state.statusBar.updateTransient(nowMs);
    state.sequencer.updateUi(nowMs);
    state.updateSequencerPatternHistoryCoalescing(nowMs);
    state.updateMacroValueHistoryCoalescing(nowMs);
    updateMutationCoalescers_(state);
}

FLASHMEM void CoreStateLifecycle::flush(CoreState& state) {
    state.flushMacroValueHistoryCoalescing();
    state.projectSettingsHistory.endCoalescing();
    flushMutationCoalescers_(state);
}

FLASHMEM void CoreStateLifecycle::flushProjectMutationCoalescing(CoreState& state) {
    state.flushMacroValueHistoryCoalescing();
    state.projectSettingsHistory.endCoalescing();
    flushMutationCoalescers_(state);
}

FLASHMEM void CoreStateLifecycle::consumeProjectReplacementMutationCoalescing(
    CoreState& state
) {
    if (state.macroDomain_.mutationCoalescer) {
        state.macroDomain_.mutationCoalescer->consumePendingChangesWithoutAction();
    }
    if (state.sequencerDomain_.mutationCoalescer) {
        state.sequencerDomain_.mutationCoalescer->consumePendingChangesWithoutAction();
    }
}

FLASHMEM void CoreStateLifecycle::resetStandaloneTransientUi(CoreState& state) {
    sequencer::abandonStepContentDraft(state.sequencer);
    state.macroEdit.reset();
    state.macroUi.resetInteraction();
    reprojectActiveMacroManualOverrides(state);
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.patternEditor.reset();
    state.sequencer.contextSelector.reset();
    state.sequencer.presetLibrary.reset();
    state.sequencer.stepPropertyInlineSelector.reset();
    state.sequencer.patternQuickControls.reset();
    state.sequencer.structureUi.reset();
    state.projectNavigation.resetTransient();
    state.projectTrackEditor.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
}

FLASHMEM void CoreStateLifecycle::resetMusicalProject(CoreState& state) {
    const bool historyBoundaryCleared = state.clearProjectHistory();
    state.project.reset();
    state.projectTracks.reset();
    state.pages.initDefaults();

    state.sequencer.reset();
    state.sequencerTracks.reset();
    if (!sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to initialize sequencer track bank");
    }
    state.requestSequencerRuntimeProjectReset();

    state.setSharedTrackState_(macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK, 0);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.statusBar.tempo.set(120.0f);
    if (!state.statusBar.tempoLocked.get()) {
        state.statusBar.tempoDisplay.set(120.0f);
    }

    state.macroEdit.reset();
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    state.requestMacroRuntimeOwnerActivation();
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.patternEditor.reset();
    state.sequencer.contextSelector.reset();
    state.sequencer.presetLibrary.reset();
    state.sequencer.stepPropertyInlineSelector.reset();
    state.sequencer.patternQuickControls.reset();
    state.sequencer.structureUi.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    state.projectNavigation.reset();
    state.projectTrackEditor.reset();

    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));

    if (!historyBoundaryCleared) {
        // A full Project replacement no longer needs rollback facts. If the
        // transient pair was already inconsistent, discard every stale owner
        // only after the replacement has made the old graph unreachable.
        OC_LOG_ERROR(
            "[CoreState] Invalid Modulator audition discarded by Project reset"
        );
        state.sequencerDomain_.coalescedPatternHistory.clear();
        state.macroHistory.clear();
        state.sequencerHistory.clear();
        state.projectTrackHistory.clear();
        state.projectHistory.clear();
    }

    flushMutationCoalescers_(state);
    state.publishProjectSessionReplacement_();
}

FLASHMEM void CoreStateLifecycle::factoryReset(CoreState& state) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    const auto resetStatus = state.deviceSettingsStore.factoryResetStatus();
    if (resetStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Device settings factory reset failed: {}",
                    persistence::persistenceWriteStatusLabel(resetStatus));
    }
    const bool historyBoundaryCleared = state.clearProjectHistory();
    resetMacroDomain_(state);
    state.requestMacroRuntimeOwnerActivation();
    resetSequencerDomain_(state);
    state.project.reset();
    state.projectTracks.reset();
    resetUiState_(state);
    if (!historyBoundaryCleared) {
        OC_LOG_ERROR(
            "[CoreState] Invalid Modulator audition discarded by factory reset"
        );
        state.sequencerDomain_.coalescedPatternHistory.clear();
        state.macroHistory.clear();
        state.sequencerHistory.clear();
        state.projectTrackHistory.clear();
        state.projectHistory.clear();
    }
    persistFactoryDefaults_(state);
    state.publishProjectSessionReplacement_();
}

}  // namespace core::state

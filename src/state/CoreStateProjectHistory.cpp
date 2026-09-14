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

constexpr bool macroHistoryResumesManualOverride(macro::MacroHistoryActionKind kind) {
    switch (kind) {
        case macro::MacroHistoryActionKind::CONVERT_AUTOMATION:
        case macro::MacroHistoryActionKind::PASTE_SLOT:
        case macro::MacroHistoryActionKind::PASTE_DESTINATION:
        case macro::MacroHistoryActionKind::PASTE_AUTOMATION:
        case macro::MacroHistoryActionKind::CLEAR_AUTOMATION:
        case macro::MacroHistoryActionKind::DELETE_SLOT:
        case macro::MacroHistoryActionKind::RECORD_AUTOMATION:
        case macro::MacroHistoryActionKind::CREATE_SLOT: return true;
        default: return false;
    }
}

constexpr bool macroHistoryTouchesProjectGraph(macro::MacroHistoryActionKind kind) {
    switch (kind) {
        case macro::MacroHistoryActionKind::PASTE_DESTINATION:
        case macro::MacroHistoryActionKind::PASTE_AUTOMATION:
        case macro::MacroHistoryActionKind::CLEAR_AUTOMATION:
        case macro::MacroHistoryActionKind::RECORD_AUTOMATION:
        case macro::MacroHistoryActionKind::AUTOMATION_STATE:
        case macro::MacroHistoryActionKind::STATIC_VALUE_EDIT:
        case macro::MacroHistoryActionKind::CREATE_SLOT:
        case macro::MacroHistoryActionKind::MANUAL_OVERRIDE_STATE:
        case macro::MacroHistoryActionKind::CONFIG_EDIT: return false;
        default: return true;
    }
}

FLASHMEM bool applyMacroProjectHistory(CoreState& state, bool redo,
                                       macro::MacroHistoryActionKind actionKind) {
    core::state::macro::MacroAutomationSlotAddress address{};
    const bool touchesDurableState =
        redo ? state.macroHistory.projectHistoryRedoTouchesDurableState()
             : state.macroHistory.projectHistoryUndoTouchesDurableState();
    const bool applied =
        redo ? state.macroHistory.redo(state.pages, &address, &state.macroUi.manualOverrides,
                                       &state.projectTracks)
             : state.macroHistory.undo(state.pages, &address, &state.macroUi.manualOverrides,
                                       &state.projectTracks);
    if (!applied) return false;

    (void)state.refreshSharedTrackStateFromMacroPages();
    if (macroHistoryResumesManualOverride(actionKind)) {
        (void)state.macroUi.manualOverrides.resume(address);
    } else if (actionKind == macro::MacroHistoryActionKind::STATIC_VALUE_EDIT) {
        float ignored = 0.0f;
        if (state.macroUi.manualOverrides.valueFor(address, ignored)) {
            (void)state.macroUi.manualOverrides.activate(
                address, state.pages.pageData(address.track, address.page).values[address.macro]);
        }
    }
    if (actionKind == macro::MacroHistoryActionKind::PAGE_STRUCTURE) {
        // Manual overrides are runtime-only and are not part of the durable
        // Page history payload. Clear the affected Track rather than retaining
        // an address that may now refer to a different compacted Page.
        (void)state.macroUi.manualOverrides.clearTrack(address.track);
    }
    state.macroUi.refreshManualOverrideMask(state.pages.currentActiveTrack(),
                                            state.pages.currentActivePage());
    state.macroUi.automationEditRevision.set(state.macroUi.automationEditRevision.get() + 1U);
    state.macroUi.runtimeProjectionRevision.set(
        core::state::macro::nextMacroRuntimeProjectionRevision(
            state.macroUi.runtimeProjectionRevision.get(),
            core::state::macro::kMacroRuntimeProjectionDirtyConfig));
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.macroUi.previewAddPageSlot.set(false);
    state.macroUi.syncPreviewPage(state.pages.currentActivePage());
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get(), core::state::macro::kMacroConfigDirtyAll));
    if (macroHistoryTouchesProjectGraph(actionKind)) {
        core::state::project::reconcileProjectModulatorNavigationAfterHistory(
            state.projectNavigation, state.pages.control.authored().modulation);
    }
    if (touchesDurableState) state.markProjectMutated();
    return true;
}

}  // namespace

FLASHMEM const char* project::projectHistoryBlockLabel(ProjectHistoryBlockReason reason) {
    switch (reason) {
        case ProjectHistoryBlockReason::NONE: return "";
        case ProjectHistoryBlockReason::DRAFT: return "Finish draft";
        case ProjectHistoryBlockReason::AUDITION: return "Finish audition";
        case ProjectHistoryBlockReason::CAPTURE: return "Finish capture";
        case ProjectHistoryBlockReason::GESTURE: return "Release gesture";
        case ProjectHistoryBlockReason::SELECTION: return "Exit selection";
        case ProjectHistoryBlockReason::PRESET_PREVIEW: return "Finish preview";
        case ProjectHistoryBlockReason::LOCAL_EDITOR: return "Finish edit";
        case ProjectHistoryBlockReason::PROJECT_CHANGE: return "Finish Project action";
    }
    return "Unavailable";
}

FLASHMEM project::ProjectHistoryBlockReason CoreState::projectHistoryBlockReason() const {
    using Reason = project::ProjectHistoryBlockReason;
    const auto& seq = sequencer;
    const auto& drum = seq.drumSequencer;
    const auto& nav = projectNavigation;
    const auto& clips = seq.clipWorkspace;
    const auto guardHeld = [](const contextual::GuardedActionState& guard) {
        return guard.phase == contextual::GuardedActionPhase::PRESSED ||
               guard.phase == contextual::GuardedActionPhase::ARMED;
    };

    // Check semantic owners even when a child has hidden its parent overlay.
    if (seq.stepContentDraft.active.get() || seq.quickControlsDraft.active() ||
        seq.ccLaneUi.mode == sequencer::SequencerCcLaneUiMode::LANE_SETTINGS ||
        seq.ccLaneUi.mode == sequencer::SequencerCcLaneUiMode::TRANSITION_PICKER ||
        drum.laneEditor.active || projectTrackEditor.textEditing ||
        (projectTrackEditor.active &&
         projectTrackEditor.draftKind != projectTrackEditor.currentKind)) {
        return Reason::DRAFT;
    }
    if (macroHistory.hasPendingModulatorAuditionTransaction(pages) ||
        pages.control.audition.active()) return Reason::AUDITION;
    if (macroUi.automationTake.phase != macro::MacroAutomationTakePhase::IDLE ||
        macroUi.recordedShapeCapture.active() || macroUi.automationTakeHistory ||
        macroUi.automationTakeDomain) return Reason::CAPTURE;
    if (seq.patternPresetPreview.active()) return Reason::PRESET_PREVIEW;

    // Published Track activations and Clip launches already have exact history
    // transitions. Do not mistake their realtime queue for an uncommitted draft.
    if (trackNavigation.selection.active.get() || macroUi.pageSelection.active.get() ||
        macroUi.slotSelection.active.get() || seq.structureUi.pageSelection.active.get() ||
        seq.structureUi.stepSelection.active.get() || drum.laneSelection.active ||
        clips.selectionActive()) return Reason::SELECTION;
    if (projectTrackHistory.hasPendingGesture() || trackNavigation.hold.active() ||
        macroUi.pageHold.active() || seq.structureUi.pageHold.active() ||
        seq.structureUi.trackPaste.navigationBlocked() || nav.physicalHoldActive.get() ||
        macroUi.contextSelector.visible || seq.contextSelector.visible ||
        macroUi.performanceOverlayMode.get() != macro::MacroPerformanceOverlayMode::NONE ||
        seq.patternQuickControls.selecting.get() || seq.stepContentSelector.selecting.get() ||
        seq.stepPropertyInlineSelector.selecting.get() || drum.selectorVisible() ||
        clips.quickSelectorVisible || clips.stopLayerActive || clips.removeHoldActive ||
        guardHeld(seq.ccLaneUi.actionGuard.get()) || guardHeld(seq.presetLibrary.actionGuard.get()) ||
        guardHeld(macroEdit.contextGuard.get()) || guardHeld(nav.modulatorGuard.get()) ||
        guardHeld(nav.modulatorClipboardGuard.get())) {
        return Reason::GESTURE;
    }
    if (nav.currentNode.get() == project::ProjectNodeId::NEW_PROJECT_CONFIRM ||
        nav.currentNode.get() == project::ProjectNodeId::LOAD_PROJECT_CONFIRM ||
        nav.currentNode.get() == project::ProjectNodeId::LOAD_PROJECT) {
        return Reason::PROJECT_CHANGE;
    }
    // These owners retain a local buffer/preview or a private publication
    // boundary. Finish through the owner; Undo must never close/flush it for us.
    if (macroEdit.flowPhase.get() != MacroEditFlowPhase::CLOSED ||
        seq.patternEditor.active.get() || seq.presetLibrary.visible.get() ||
        deviceSettings.selector.visible.get() || patternPitchSettings.selector.visible.get() ||
        drum.pickerVisible() || nav.creatingModulatorSource || nav.modulatorReturn.active() ||
        nav.currentNode.get() == project::ProjectNodeId::SAVE_AS_PROJECT_NAME ||
        nav.currentNode.get() == project::ProjectNodeId::RENAME_PROJECT_NAME ||
        nav.currentNode.get() == project::ProjectNodeId::MODULATOR_SOURCE_RENAME ||
        clips.editorActive()) return Reason::LOCAL_EDITOR;

    // Fail closed for a newly introduced overlay until its owner is qualified.
    // The live editors listed here already reconcile history and disappearing targets.
    switch (overlays.current()) {
        case core::ui::OverlayType::NONE:
        case core::ui::OverlayType::VIEW_SELECTOR:
        case core::ui::OverlayType::SEQ_CC_LANE:
        case core::ui::OverlayType::SEQ_TRACK_EDIT:
        case core::ui::OverlayType::SEQ_STEP_EDIT:
        case core::ui::OverlayType::PATTERN_PITCH_SETTINGS: return Reason::NONE;
        default: return Reason::LOCAL_EDITOR;
    }
}

FLASHMEM void CoreState::formatProjectHistoryLabel(
    project::ProjectHistoryDirection direction, char* out, size_t capacity
) const {
    if (!out || capacity == 0U) return;
    const bool undo = direction == project::ProjectHistoryDirection::Undo;
    const auto reason = projectHistoryBlockReason();
    if (reason != project::ProjectHistoryBlockReason::NONE) {
        std::snprintf(out, capacity, "%s: %s", undo ? "Undo" : "Redo",
                      project::projectHistoryBlockLabel(reason));
    } else if (undo) {
        projectHistory.formatUndoLabel(out, capacity);
    } else {
        projectHistory.formatRedoLabel(out, capacity);
    }
}

FLASHMEM bool CoreState::prepareProjectHistoryInteraction() {
    if (projectHistoryBlockReason() != project::ProjectHistoryBlockReason::NONE) return false;

    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }
    flushMacroValueHistoryCoalescing();
    projectSettingsHistory.endCoalescing();
    return true;
}

FLASHMEM sequencer::SequencerTrackStructureChronologyResult
CoreState::openSequencerTrackStructureChronologyBoundary() {
    using BoundaryStatus =
        sequencer::SequencerTrackStructureChronologyStatus;
    using Outcome = sequencer::SequencerPatternHistoryCommitOutcome;

    // Neither transient owner may be reordered or implicitly cancelled by a
    // Track topology command. Malformed audition pairs are rejected by the
    // same fail-closed predicate.
    if (macroHistory.hasPendingModulatorAuditionTransaction(pages)) {
        return {BoundaryStatus::MacroAuditionBlocked, Outcome::NoPending};
    }
    if (projectTrackHistory.hasPendingGesture()) {
        return {BoundaryStatus::ProjectTrackGestureBlocked, Outcome::NoPending};
    }

    const auto patternOutcome =
        commitSequencerPatternHistoryCoalescingOutcome();
    if (patternOutcome == Outcome::Failed) {
        return {BoundaryStatus::PatternFailed, Outcome::Failed};
    }

    // This is the canonical lifecycle authority for Macro value, Settings and
    // both generic Macro/Sequencer mutation coalescers. Their publications are
    // complete before the Track transaction captures its failure checkpoint.
    CoreStateLifecycle::flushProjectMutationCoalescing(*this);
    return {BoundaryStatus::Opened, patternOutcome};
}

FLASHMEM bool CoreState::undoProjectHistory() {
    return applyProjectHistory(project::ProjectHistoryDirection::Undo);
}

FLASHMEM bool CoreState::redoProjectHistory() {
    return applyProjectHistory(project::ProjectHistoryDirection::Redo);
}

FLASHMEM bool CoreState::applyProjectHistory(project::ProjectHistoryDirection direction) {
    if (sequencer.stepContentDraft.rejectTransitionIfActive(
            sequencer::SequencerStepContentDraftBlockedTransition::HISTORY)) {
        return false;
    }
    if (!prepareProjectHistoryInteraction()) return false;
    const bool redo = direction == project::ProjectHistoryDirection::Redo;
    const auto* entry = redo ? projectHistory.peekRedo() : projectHistory.peekUndo();
    if (entry == nullptr) return false;

    if (entry->domain == project::ProjectHistoryDomain::Macro) {
        return (redo ? macroHistory.projectHistoryRedoIdentity()
                     : macroHistory.projectHistoryUndoIdentity()) == entry->identity &&
               applyMacroProjectHistory(
                   *this, redo, static_cast<macro::MacroHistoryActionKind>(entry->actionKind));
    }
    if (entry->domain == project::ProjectHistoryDomain::Sequencer) {
        return (redo ? sequencerHistory.projectHistoryRedoIdentity()
                     : sequencerHistory.projectHistoryUndoIdentity()) == entry->identity &&
               (redo ? redoSequencerHistory() : undoSequencerHistory());
    }
    if (entry->domain == project::ProjectHistoryDomain::Settings) {
        if ((redo ? projectSettingsHistory.projectHistoryRedoIdentity()
                  : projectSettingsHistory.projectHistoryUndoIdentity()) != entry->identity ||
            !(redo ? projectSettingsHistory.redo(statusBar, projectNavigation)
                   : projectSettingsHistory.undo(statusBar, projectNavigation))) {
            return false;
        }
        markProjectMutated();
        return true;
    }
    if ((redo ? projectTrackHistory.projectHistoryRedoIdentity()
              : projectTrackHistory.projectHistoryUndoIdentity()) != entry->identity) return false;
    auto tracks = project::ProjectTrackDomainServices::fromCoreState(*this);
    return redo ? tracks.redo() : tracks.undo();
}

FLASHMEM bool CoreState::clearProjectHistory() {
    const bool hadModulatorTransaction = macroHistory.hasPendingModulatorAuditionTransaction(pages);
    if (!macroHistory.abortPendingModulatorAudition(pages)) return false;
    if (hadModulatorTransaction) {
        core::state::project::reconcileProjectModulatorNavigationAfterHistory(
            projectNavigation, pages.control.authored().modulation, false);
    }
    if (projectTrackHistory.hasPendingGesture()) {
        (void)project::ProjectTrackDomainServices::fromCoreState(*this).cancelGesture();
    }
    sequencerDomain_.coalescedPatternHistory.clear();
    sequencerDomain_.coalescedDrumHistory.clear();
    macroHistory.clear();
    sequencerHistory.clear();
    projectTrackHistory.clear();
    projectSettingsHistory.clear();
    projectHistory.clear();
    return true;
}

}  // namespace core::state

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
#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "sequencer/SequencerCcLanePatternOps.hpp"
#include "sequencer/SequencerContentViewOps.hpp"
#include "sequencer/SequencerStructureHistory.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::state {

namespace {

constexpr bool macroHistoryResumesManualOverride(
    macro::MacroHistoryActionKind kind
) {
    switch (kind) {
        case macro::MacroHistoryActionKind::CONVERT_AUTOMATION:
        case macro::MacroHistoryActionKind::PASTE_SLOT:
        case macro::MacroHistoryActionKind::PASTE_DESTINATION:
        case macro::MacroHistoryActionKind::PASTE_AUTOMATION:
        case macro::MacroHistoryActionKind::CLEAR_AUTOMATION:
        case macro::MacroHistoryActionKind::REMOVE_SLOT:
        case macro::MacroHistoryActionKind::RECORD_AUTOMATION:
        case macro::MacroHistoryActionKind::CREATE_SLOT:
            return true;
        default:
            return false;
    }
}

constexpr bool macroHistoryTouchesProjectGraph(
    macro::MacroHistoryActionKind kind
) {
    switch (kind) {
        case macro::MacroHistoryActionKind::PASTE_DESTINATION:
        case macro::MacroHistoryActionKind::PASTE_AUTOMATION:
        case macro::MacroHistoryActionKind::CLEAR_AUTOMATION:
        case macro::MacroHistoryActionKind::RECORD_AUTOMATION:
        case macro::MacroHistoryActionKind::AUTOMATION_STATE:
        case macro::MacroHistoryActionKind::STATIC_VALUE_EDIT:
        case macro::MacroHistoryActionKind::CREATE_SLOT:
        case macro::MacroHistoryActionKind::MANUAL_OVERRIDE_STATE:
        case macro::MacroHistoryActionKind::CONFIG_EDIT:
            return false;
        default:
            return true;
    }
}

FLASHMEM bool applyMacroProjectHistory(
    CoreState& state,
    bool redo,
    macro::MacroHistoryActionKind actionKind
) {
    core::state::macro::MacroAutomationSlotAddress address{};
    const bool touchesDurableState = redo
        ? state.macroHistory.projectHistoryRedoTouchesDurableState()
        : state.macroHistory.projectHistoryUndoTouchesDurableState();
    const bool applied = redo
        ? state.macroHistory.redo(
              state.pages,
              &address,
              &state.macroUi.manualOverrides,
              &state.projectTracks
          )
        : state.macroHistory.undo(
              state.pages,
              &address,
              &state.macroUi.manualOverrides,
              &state.projectTracks
          );
    if (!applied) return false;

    (void)state.refreshSharedTrackStateFromMacroPages();
    if (macroHistoryResumesManualOverride(actionKind)) {
        (void)state.macroUi.manualOverrides.resume(address);
    } else if (actionKind == macro::MacroHistoryActionKind::STATIC_VALUE_EDIT) {
        float ignored = 0.0f;
        if (state.macroUi.manualOverrides.valueFor(address, ignored)) {
            (void)state.macroUi.manualOverrides.activate(
                address,
                state.pages.pageData(address.track, address.page)
                    .values[address.macro]
            );
        }
    }
    if (actionKind == macro::MacroHistoryActionKind::PAGE_STRUCTURE) {
        // Manual overrides are runtime-only and are not part of the durable
        // Page history payload. Clear the affected Track rather than retaining
        // an address that may now refer to a different compacted Page.
        (void)state.macroUi.manualOverrides.clearTrack(address.track);
    }
    state.macroUi.refreshManualOverrideMask(
        state.pages.currentActiveTrack(),
        state.pages.currentActivePage()
    );
    state.macroUi.automationEditRevision.set(
        state.macroUi.automationEditRevision.get() + 1U
    );
    state.macroUi.runtimeProjectionRevision.set(
        core::state::macro::nextMacroRuntimeProjectionRevision(
            state.macroUi.runtimeProjectionRevision.get(),
            core::state::macro::kMacroRuntimeProjectionDirtyConfig
        )
    );
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.macroUi.previewAddPageSlot.set(false);
    state.macroUi.syncPreviewPage(state.pages.currentActivePage());
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get(),
        core::state::macro::kMacroConfigDirtyAll
    ));
    if (macroHistoryTouchesProjectGraph(actionKind)) {
        core::state::project::reconcileProjectModulatorNavigationAfterHistory(
            state.projectNavigation,
            state.pages.control.authored.modulation
        );
    }
    if (touchesDurableState) state.markProjectMutated();
    return true;
}

}  // namespace

FLASHMEM bool CoreState::prepareProjectHistoryInteraction() {
    // A destination-first audition owns one reserved Macro history delta.
    // Global Undo/Redo must never consume or reorder the surrounding Project
    // chronology until that transaction is explicitly applied or cancelled.
    // The predicate is deliberately fail-closed for malformed transient pairs.
    if (macroHistory.hasPendingModulatorAuditionTransaction(pages)) return false;
    if (projectTrackHistory.hasPendingGesture()) return false;

    const bool hadPending = hasPendingSequencerPatternHistoryCoalescing();
    const bool committed = commitSequencerPatternHistoryCoalescing();
    flushMacroValueHistoryCoalescing();
    projectSettingsHistory.endCoalescing();
    return !hadPending || committed ||
           !hasPendingSequencerPatternHistoryCoalescing();
}

FLASHMEM bool CoreState::undoProjectHistory() {
    if (!prepareProjectHistoryInteraction()) return false;
    const auto* entry = projectHistory.peekUndo();
    if (entry == nullptr) return false;

    if (entry->domain == project::ProjectHistoryDomain::Macro) {
        return macroHistory.projectHistoryUndoIdentity() == entry->identity &&
               applyMacroProjectHistory(
                   *this,
                   false,
                   static_cast<macro::MacroHistoryActionKind>(entry->actionKind)
               );
    }
    if (entry->domain == project::ProjectHistoryDomain::Sequencer) {
        return sequencerHistory.projectHistoryUndoIdentity() == entry->identity &&
               undoSequencerHistory();
    }
    if (entry->domain == project::ProjectHistoryDomain::Settings) {
        if (projectSettingsHistory.projectHistoryUndoIdentity() !=
                entry->identity ||
            !projectSettingsHistory.undo(
                statusBar,
                projectNavigation,
                midiSync
            )) {
            return false;
        }
        markProjectMutated();
        return true;
    }
    return projectTrackHistory.projectHistoryUndoIdentity() == entry->identity &&
           project::ProjectTrackDomainServices::fromCoreState(*this).undo();
}

FLASHMEM bool CoreState::redoProjectHistory() {
    if (!prepareProjectHistoryInteraction()) return false;
    const auto* entry = projectHistory.peekRedo();
    if (entry == nullptr) return false;

    if (entry->domain == project::ProjectHistoryDomain::Macro) {
        return macroHistory.projectHistoryRedoIdentity() == entry->identity &&
               applyMacroProjectHistory(
                   *this,
                   true,
                   static_cast<macro::MacroHistoryActionKind>(entry->actionKind)
               );
    }
    if (entry->domain == project::ProjectHistoryDomain::Sequencer) {
        return sequencerHistory.projectHistoryRedoIdentity() == entry->identity &&
               redoSequencerHistory();
    }
    if (entry->domain == project::ProjectHistoryDomain::Settings) {
        if (projectSettingsHistory.projectHistoryRedoIdentity() !=
                entry->identity ||
            !projectSettingsHistory.redo(
                statusBar,
                projectNavigation,
                midiSync
            )) {
            return false;
        }
        markProjectMutated();
        return true;
    }
    return projectTrackHistory.projectHistoryRedoIdentity() == entry->identity &&
           project::ProjectTrackDomainServices::fromCoreState(*this).redo();
}

FLASHMEM bool CoreState::clearProjectHistory() {
    const bool hadModulatorTransaction =
        macroHistory.hasPendingModulatorAuditionTransaction(pages);
    if (!macroHistory.abortPendingModulatorAudition(pages)) return false;
    if (hadModulatorTransaction) {
        core::state::project::reconcileProjectModulatorNavigationAfterHistory(
            projectNavigation,
            pages.control.authored.modulation,
            false
        );
    }
    if (projectTrackHistory.hasPendingGesture()) {
        (void)project::ProjectTrackDomainServices::fromCoreState(*this)
            .cancelGesture();
    }
    sequencerDomain_.coalescedPatternHistory.clear();
    macroHistory.clear();
    sequencerHistory.clear();
    projectTrackHistory.clear();
    projectSettingsHistory.clear();
    projectHistory.clear();
    return true;
}

}  // namespace core::state

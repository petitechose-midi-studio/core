#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructurePageSelectionOps.hpp"
#include "handler/sequencer/SequencerStructureStepPasteWorkflow.hpp"
#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "handler/sequencer/SequencerStructureTrackSelectionOps.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

FLASHMEM SequencerStructureEditWorkflow::SequencerStructureEditWorkflow(StateRefs state)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
    , project_navigation_(state.projectNavigation)
    , structure_clipboard_(state.structureClipboard)
    , shared_tracks_(state.sharedTracks)
    , history_(state.history) {}

FLASHMEM bool SequencerStructureEditWorkflow::canRemoveCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return false;
        return structure_slots::countEnabled(
            currentTrackEnabledMask(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        ) > 1U;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        return sequencer_.focusedStep.get() <
               core::state::sequencer::activeContentLength(sequencer_);
    }
    if (sequencer_.structureUi.previewAddPageSlot.get()) return false;
    return sequencer_.activePageCount() > 1U;
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return structure_clipboard_.hasSequencerTrack();
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        return canPasteFocusedStep();
    }
    return structure_clipboard_.hasSequencerPage();
}

FLASHMEM void SequencerStructureEditWorkflow::beginHoldAction(
    core::state::StructureHoldAction action
) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        track_ui_.hold.begin(action, core::time_compat::millis());
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void SequencerStructureEditWorkflow::clearHoldAction() {
    track_ui_.hold.clear();
    sequencer_.structureUi.pageHold.clear();
}

FLASHMEM void SequencerStructureEditWorkflow::applyBottomLeftTapCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const uint8_t activeTrack = currentActiveTrack();
        const uint16_t historyMask = sequencerStructureHistoryTrackBit(activeTrack);
        auto change = captureTrackHistoryBefore(historyMask);
        if (!toggleSequencerStructureTrackMute(tracks_, activeTrack)) return;
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        eraseFocusedStep();
        return;
    }

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;
    if (clearCurrentSequencerStructurePage(sequencer_)) {
        recordPageHistoryAfter(std::move(before));
    }
}

FLASHMEM void SequencerStructureEditWorkflow::toggleTrackSelectionMute() {
    auto& selection = track_ui_.selection;
    if (!selection.active.get() ||
        selection.scope.get() != core::state::StructureSelectionScope::TRACK) {
        return;
    }

    const uint16_t selectedMask = activeTrackSelectionMask(
        selection.selectedMask.get(),
        currentTrackEnabledMask()
    );
    if (selectedMask == 0) return;

    const uint16_t historyMask = static_cast<uint16_t>(
        selectedMask | sequencerStructureHistoryTrackBit(currentActiveTrack())
    );
    auto change = captureTrackHistoryBefore(historyMask);

    if (!toggleSelectedSequencerStructureTrackMute(tracks_, selectedMask)) return;
    recordTrackHistoryAfter(std::move(change), historyMask);
}

FLASHMEM void SequencerStructureEditWorkflow::eraseFocusedStep() {
    const uint8_t step = sequencer_.focusedStep.get();
    if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    if (!resetActiveContentStep(sequencer_, step, StepResetDepth::Shallow)) return;
    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.focusedStep.set(step);
    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(step));
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::removeCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const auto mutation = structure_slots::removeIndex(
            currentTrackEnabledMask(),
            currentActiveTrack(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (!mutation.changed) return;
        const uint16_t historyMask = static_cast<uint16_t>(
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(mutation.nextActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        applyTrackState(mutation.nextMask, mutation.nextActive);
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        const uint8_t step = sequencer_.focusedStep.get();
        if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

        HistoryPatternSnapshot before;
        if (!capturePageHistoryBefore(before)) return;

        if (!resetActiveContentStep(sequencer_, step, StepResetDepth::Deep)) return;
        core::state::sequencer::refreshContentView(sequencer_);
        sequencer_.pattern.bumpStepDataRevision();
        sequencer_.focusedStep.set(step);
        sequencer_.page.set(core::state::sequencer::activeContentPageForStep(step));
        recordPageHistoryAfter(std::move(before));
        return;
    }

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;
    if (removeCurrentSequencerStructurePage(sequencer_)) {
        recordPageHistoryAfter(std::move(before));
    }
}

FLASHMEM void SequencerStructureEditWorkflow::copyCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        core::state::sequencer::SequencerPatternSnapshot snapshot;
        core::state::sequencer::captureSnapshot(sequencer_.pattern, snapshot);
        if (!structure_clipboard_.storeSequencerTrack(
            snapshot,
            core::state::sequencer::graphView(sequencer_.pattern)
        )) {
            return;
        }
        return;
    }

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        copyFocusedStep();
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    core::state::SequencerPageClipboard clipboard;
    const uint8_t page = sequencer_.visiblePage();
    if (!capturePageClipboard(sequencer_, page, clipboard)) return;
    if (!structure_clipboard_.storeSequencerPage(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    )) {
        return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (!structure_clipboard_.hasSequencerTrack()) return;
        const uint8_t targetTrack =
            sequencerStructureTrackTarget(track_ui_, currentActiveTrack());
        const uint16_t historyMask = static_cast<uint16_t>(
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(targetTrack)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        if (!pasteCurrentSequencerStructureTrack(
                tracks_,
                sequencer_,
                track_ui_,
                shared_tracks_,
                structure_clipboard_
            )) {
            return;
        }
        syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        pasteFocusedStep();
        return;
    }

    if (!structure_clipboard_.hasSequencerPage()) return;
    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;
    uint8_t targetPage = sequencer_.visiblePage();
    if (sequencer_.structureUi.previewAddPageSlot.get()) {
        targetPage = sequencer_.clampPage(sequencer_.structureUi.previewPageIndex.get());
        if (!createSequencerStructurePage(sequencer_)) return;
    }

    const auto& clipboard = structure_clipboard_.sequencerPage;
    pastePageClipboard(
        sequencer_,
        clipboard,
        structure_clipboard_.sequencerGraph.get(),
        targetPage
    );
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.structureUi.syncPreviewPage(targetPage);
    sequencer_.page.set(targetPage);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(targetPage));
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteSelection() const {
    if (track_ui_.selection.active.get()) {
        return structure_clipboard_.hasSequencerTrackSelection();
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        return structure_clipboard_.hasSequencerPageSelection();
    }
    return false;
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteFocusedStep() const {
    return structure_clipboard_.hasSequencerSteps() &&
           structure_clipboard_.sequencerSteps.rootContext ==
               core::state::sequencer::isRootContentView(sequencer_);
}

FLASHMEM void SequencerStructureEditWorkflow::copyFocusedStep() {
    const uint8_t step = sequencer_.focusedStep.get();

    core::state::SequencerStepsClipboard clipboard;
    if (!captureFocusedStepClipboard(sequencer_, tracks_, step, clipboard)) return;

    structure_clipboard_.storeSequencerSteps(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    );
}

FLASHMEM void SequencerStructureEditWorkflow::pasteFocusedStep() {
    pasteStepClipboardAt(sequencer_.focusedStep.get(), false);
}

FLASHMEM void SequencerStructureEditWorkflow::clearSelection() {
    if (track_ui_.selection.active.get()) {
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = activePageSelectionMask(
        sequencer_,
        selection.selectedMask.get()
    );
    if (selectedMask == 0) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    if (!clearSelectedPages(sequencer_, selectedMask)) return;
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::copySelection() {
    if (track_ui_.selection.active.get()) {
        const uint16_t selectedMask = activeTrackSelectionMask(
            track_ui_.selection.selectedMask.get(),
            currentTrackEnabledMask()
        );
        auto clipboard = captureTrackSelectionClipboard(
            tracks_,
            sequencer_,
            selectedMask
        );
        if (!clipboard) return;
        structure_clipboard_.storeSequencerTrackSelection(std::move(clipboard));
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = activePageSelectionMask(
        sequencer_,
        selection.selectedMask.get()
    );

    core::state::SequencerPageSelectionClipboard clipboard;
    if (!capturePageSelectionClipboard(sequencer_, selectedMask, clipboard)) return;
    structure_clipboard_.storeSequencerPageSelection(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    );
}

FLASHMEM void SequencerStructureEditWorkflow::copyStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;

    core::state::SequencerStepsClipboard clipboard;
    if (!captureStepSelectionClipboard(
            sequencer_,
            tracks_,
            selection.selectedMask.get(),
            clipboard
        )) {
        return;
    }
    structure_clipboard_.storeSequencerSteps(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    );
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelectionShallow() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;

    const uint8_t activeLength = core::state::sequencer::activeContentLength(sequencer_);
    const auto selectedMask = selection.selectedMask.get();
    uint8_t first = 0;
    uint8_t last = 0;
    if (!selectedStepRange(selectedMask, activeLength, first, last)) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    bool changed = false;
    for (uint8_t step = first; step <= last; ++step) {
        if (!selectedMask.test(step)) continue;
        changed = resetActiveContentStep(sequencer_, step, StepResetDepth::Shallow) || changed;
    }

    if (!changed) return;
    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelectionDeep() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;

    const uint8_t activeLength = core::state::sequencer::activeContentLength(sequencer_);
    const auto selectedMask = selection.selectedMask.get();
    uint8_t first = 0;
    uint8_t last = 0;
    if (!selectedStepRange(selectedMask, activeLength, first, last)) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    bool changed = false;
    for (uint8_t step = first; step <= last; ++step) {
        if (!selectedMask.test(step)) continue;
        changed = resetActiveContentStep(sequencer_, step, StepResetDepth::Deep) || changed;
    }

    if (!changed) return;
    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::beginStepPastePreview() {
    beginStructureStepPastePreview(sequencer_, structure_clipboard_, project_navigation_);
}

FLASHMEM void SequencerStructureEditWorkflow::clearStepPastePreview() {
    clearStructureStepPastePreview(sequencer_);
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepClipboardAt(
    uint8_t cursorStep,
    bool resetSelection
) {
    if (!structure_clipboard_.hasSequencerSteps()) return;

    const auto mode = structureStepPasteMode(project_navigation_);
    const auto plan = buildStructureStepPastePlan(
        sequencer_,
        structure_clipboard_.sequencerSteps,
        mode,
        cursorStep
    );
    if (plan.blocked || !plan.hasEntries()) {
        clearStepPastePreview();
        return;
    }
    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    if (!commitStructureStepPastePlan(sequencer_, structure_clipboard_, mode, plan)) {
        clearStepPastePreview();
        return;
    }

    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    if (resetSelection) {
        sequencer_.structureUi.stepSelection.reset(plan.firstTarget);
    }
    sequencer_.focusedStep.set(plan.firstTarget);
    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(plan.firstTarget));
    navigation_focus_.set(core::state::StructureNavigationFocus::STEP);
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;
    pasteStepClipboardAt(selection.cursorStep.get(), true);
}

FLASHMEM void SequencerStructureEditWorkflow::pasteSelection() {
    if (track_ui_.selection.active.get()) {
        if (!structure_clipboard_.hasSequencerTrackSelection()) return;
        const auto* clipboard = structure_clipboard_.sequencerTrackSelection.get();
        if (clipboard == nullptr) return;

        const uint8_t cursorTrack =
            core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
                track_ui_.selection.cursorIndex.get()
            );
        const auto targets = buildTrackSelectionPasteTargets(*clipboard, cursorTrack);
        if (!targets.hasTargets()) return;

        const uint8_t previousActive = currentActiveTrack();
        const uint16_t historyMask = static_cast<uint16_t>(
            targets.targetMask | sequencerStructureHistoryTrackBit(previousActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);

        pasteTrackSelectionClipboard(
            tracks_,
            sequencer_,
            *clipboard,
            cursorTrack,
            previousActive
        );

        const uint16_t nextMask = static_cast<uint16_t>(
            currentTrackEnabledMask() | targets.targetMask
        );
        applyTrackState(nextMask, targets.firstTarget);
        cancelSelectionMode();
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get() || !structure_clipboard_.hasSequencerPageSelection()) return;

    const auto plan = buildPageSelectionPastePlan(
        sequencer_,
        structure_clipboard_,
        selection.cursorIndex.get()
    );
    if (!plan.hasEntries()) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    pastePageSelectionClipboard(sequencer_, structure_clipboard_, plan);
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.page.set(plan.firstDestinationPage);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(plan.firstDestinationPage));
    sequencer_.structureUi.syncPreviewPage(plan.firstDestinationPage);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    cancelSelectionMode();
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::deleteSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        const uint16_t trackMask = activeTrackSelectionMask(
            selectedMask,
            currentTrackEnabledMask()
        );
        if (trackMask == 0) return;

        const auto mutation = removeSelectedSequencerStructureTracks(
            currentTrackEnabledMask(),
            trackMask,
            currentActiveTrack()
        );
        if (!mutation.changed) return;
        const uint16_t historyMask = static_cast<uint16_t>(
            trackMask |
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(mutation.nextActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        const bool changed = applyTrackState(mutation.nextMask, mutation.nextActive);
        if (!changed) return;
        cancelSelectionMode();
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    } else {
        const uint16_t pageMask = activePageSelectionMask(sequencer_, selectedMask);
        if (pageMask == 0) return;

        HistoryPatternSnapshot before;
        if (!capturePageHistoryBefore(before)) return;
        if (!removeSelectedPages(sequencer_, pageMask)) return;
        cancelSelectionMode();
        recordPageHistoryAfter(std::move(before));
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::capturePageHistoryBefore(
    HistoryPatternSnapshot& before
) const {
    return captureSequencerPageStructureHistory(sequencer_, before);
}

FLASHMEM void SequencerStructureEditWorkflow::recordPageHistoryAfter(
    HistoryPatternSnapshot before
) {
    recordSequencerPageStructureHistoryChange(
        history_,
        sequencer_,
        std::move(before),
        currentActiveTrack()
    );
}

FLASHMEM SequencerStructureEditWorkflow::HistoryTrackStructureChangePtr
SequencerStructureEditWorkflow::captureTrackHistoryBefore(uint16_t trackMask) const {
    return captureSequencerTrackStructureHistoryBefore(tracks_, sequencer_, trackMask);
}

FLASHMEM void SequencerStructureEditWorkflow::recordTrackHistoryAfter(
    HistoryTrackStructureChangePtr change,
    uint16_t trackMask
) {
    if (!change) return;

    if (!captureSequencerTrackStructureHistoryAfter(
            tracks_,
            sequencer_,
            trackMask,
            *change
        )) {
        return;
    }

    recordSequencerTrackStructureHistoryChange(history_, std::move(change));
}

FLASHMEM void SequencerStructureEditWorkflow::syncPreviewToFocus(
    core::state::StructureNavigationFocus focus
) {
    track_ui_.previewAddSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    syncSequencerPagePreviewToVisible(
        sequencer_,
        focus == core::state::StructureNavigationFocus::PAGE
    );
}

FLASHMEM void SequencerStructureEditWorkflow::cancelSelectionMode() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    const auto scope = selection.scope.get();
    const uint8_t cursor = scope == core::state::StructureSelectionScope::TRACK
        ? currentActiveTrack()
        : sequencer_.visiblePage();
    selection.reset(scope, cursor);
    syncPreviewToFocus(
        scope == core::state::StructureSelectionScope::TRACK
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
}

FLASHMEM uint16_t SequencerStructureEditWorkflow::currentTrackEnabledMask() const {
    return shared_tracks_.enabledMask();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::currentActiveTrack() const {
    return shared_tracks_.activeTrack();
}

FLASHMEM bool SequencerStructureEditWorkflow::applyTrackState(
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    return shared_tracks_.setState(enabledMask, activeTrack);
}

}  // namespace core::handler

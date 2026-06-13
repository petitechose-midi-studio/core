#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

FLASHMEM uint16_t duplicateTrackHistoryMask(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t activeTrack
) {
    uint16_t nextMask = enabledMask;
    uint16_t historyMask = sequencerStructureHistoryTrackBit(activeTrack);

    for (uint8_t source = 0;
         source < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++source) {
        const uint16_t sourceBit = structure_slots::slotBit(source);
        if ((selectedMask & sourceBit) == 0 || (enabledMask & sourceBit) == 0) {
            continue;
        }

        const int dest = structure_slots::firstDisabledIndex(
            nextMask,
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (dest < 0) {
            break;
        }

        const auto destTrack = static_cast<uint8_t>(dest);
        historyMask = static_cast<uint16_t>(
            historyMask |
            sequencerStructureHistoryTrackBit(source) |
            sequencerStructureHistoryTrackBit(destTrack)
        );
        nextMask = static_cast<uint16_t>(nextMask | structure_slots::slotBit(destTrack));
    }

    return historyMask;
}

FLASHMEM void copyPageChildContentFromClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    uint8_t targetPage
) {
    if (!clipboard.sequencerGraph || !clipboard.sequencerGraph->enabled) return;

    const uint8_t sourceStart = static_cast<uint8_t>(
        clipboard.sequencerPage.sourcePage *
        core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    const uint8_t targetStart = static_cast<uint8_t>(
        targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );

    for (uint8_t i = 0; i < clipboard.sequencerPage.count; ++i) {
        const uint8_t sourceStep = static_cast<uint8_t>(sourceStart + i);
        const uint8_t targetStep = static_cast<uint8_t>(targetStart + i);
        if (sourceStep >= core::state::sequencer::SequencerState::MAX_STEPS ||
            targetStep >= core::state::sequencer::SequencerState::MAX_STEPS) {
            break;
        }

        const auto sourceNode = core::state::sequencer::rootStepNodeId(sourceStep);
        const auto targetNode = core::state::sequencer::rootStepNodeId(targetStep);
        core::state::sequencer::clearNodeChildren(sequencer.pattern, targetNode);
        core::state::sequencer::copyNodeChildrenFromGraph(
            sequencer.pattern,
            targetNode,
            *clipboard.sequencerGraph,
            sourceNode
        );
    }
}

}  // namespace

FLASHMEM SequencerStructureEditWorkflow::SequencerStructureEditWorkflow(StateRefs state)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
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
    if (sequencer_.structureUi.previewAddPageSlot.get()) return false;
    return sequencer_.activePageCount() > 1U;
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return structure_clipboard_.hasSequencerTrack();
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

FLASHMEM void SequencerStructureEditWorkflow::eraseCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const uint8_t activeTrack = currentActiveTrack();
        const uint16_t historyMask = sequencerStructureHistoryTrackBit(activeTrack);
        auto change = captureTrackHistoryBefore(historyMask);
        sequencer_.reset();
        sequencer_.pattern.midiChannel.set(activeTrack);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;
    const uint8_t start = sequencer_.pageStartStepClamped(sequencer_.visiblePage());
    const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
    if (core::state::sequencer::clearStepRange(sequencer_, start, end)) {
        recordPageHistoryAfter(std::move(before));
    }
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

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;
    const uint8_t pageIndex = sequencer_.visiblePage();
    if (core::state::sequencer::removePage(sequencer_, pageIndex)) {
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

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    core::state::SequencerPageClipboard clipboard;
    const uint8_t page = sequencer_.visiblePage();
    const uint8_t start = sequencer_.pageStartStepClamped(page);
    const uint8_t len = sequencer_.pattern.length.get();
    const uint8_t count = (start >= len)
        ? 0
        : static_cast<uint8_t>(std::min<uint16_t>(
              core::state::sequencer::SequencerState::STEPS_PER_PAGE,
              static_cast<uint16_t>(len - start)
          ));
    if (count == 0) return;

    clipboard.valid = true;
    clipboard.sourcePage = page;
    clipboard.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t step = static_cast<uint8_t>(start + i);
        clipboard.note[i] = sequencer_.pattern.note[step];
        clipboard.velocity[i] = sequencer_.pattern.velocity[step];
        clipboard.gate[i] = sequencer_.pattern.gate[step];
        clipboard.nudge[i] = sequencer_.pattern.nudge[step];
        clipboard.probability[i] = sequencer_.pattern.probability[step];
        if (sequencer_.pattern.isEnabled(step)) {
            clipboard.enabledMask |= static_cast<uint8_t>(1U << i);
        }
    }
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
        const uint8_t targetTrack = track_ui_.previewAddSlot.get()
            ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
                  track_ui_.previewTrackIndex.get()
              )
            : currentActiveTrack();
        const uint16_t historyMask = static_cast<uint16_t>(
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(targetTrack)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        if (track_ui_.previewAddSlot.get() &&
            !createSequencerStructureTrack(sequencer_, tracks_, track_ui_, shared_tracks_)) {
            return;
        }
        core::state::sequencer::applySnapshotToEditor(sequencer_, structure_clipboard_.sequencerTrack);
        core::state::sequencer::copyGraph(
            sequencer_.pattern,
            structure_clipboard_.sequencerGraph.get(),
            structure_clipboard_.sequencerTrack.graphRevision
        );
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
        recordTrackHistoryAfter(std::move(change), historyMask);
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
    const uint8_t targetStart =
        static_cast<uint8_t>(targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE);
    const uint8_t targetEnd = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(targetStart + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
    core::state::sequencer::clearStepRange(sequencer_, targetStart, targetEnd);
    const uint8_t requiredLength = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS,
        static_cast<uint16_t>(targetStart + std::max<uint8_t>(clipboard.count, 1))
    ));
    if (sequencer_.pattern.length.get() < requiredLength) {
        sequencer_.pattern.length.set(requiredLength);
    }
    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const uint8_t step = static_cast<uint8_t>(targetStart + i);
        sequencer_.pattern.note[step] = clipboard.note[i];
        sequencer_.pattern.velocity[step] = clipboard.velocity[i];
        sequencer_.pattern.gate[step] = clipboard.gate[i];
        sequencer_.pattern.nudge[step] = clipboard.nudge[i];
        sequencer_.pattern.probability[step] = clipboard.probability[i];
        sequencer_.pattern.setEnabled(step, clipboard.isEnabled(i));
    }
    copyPageChildContentFromClipboard(sequencer_, structure_clipboard_, targetPage);
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.structureUi.syncPreviewPage(targetPage);
    sequencer_.page.set(targetPage);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(targetPage));
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::deleteSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    bool changed = false;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        const auto mutation = structure_slots::removeSelected(
            currentTrackEnabledMask(),
            selectedMask,
            currentActiveTrack(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (!mutation.changed) return;
        const uint16_t historyMask = static_cast<uint16_t>(
            selectedMask |
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(mutation.nextActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        changed = applyTrackState(mutation.nextMask, mutation.nextActive);
        if (!changed) return;
        cancelSelectionMode();
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    } else {
        HistoryPatternSnapshot before;
        if (!capturePageHistoryBefore(before)) return;
        const uint8_t pageCount = sequencer_.activePageCount();
        const uint8_t deleteCount = structure_slots::countEnabled(selectedMask, pageCount);
        if (deleteCount > 0 && deleteCount < pageCount) {
            for (int page = static_cast<int>(pageCount) - 1; page >= 0; --page) {
                const uint16_t bit = structure_slots::slotBit(static_cast<uint8_t>(page));
                if ((selectedMask & bit) == 0) continue;
                changed = core::state::sequencer::removePage(
                              sequencer_,
                              static_cast<uint8_t>(page)
                          ) || changed;
            }
        }

        if (!changed) return;
        cancelSelectionMode();
        recordPageHistoryAfter(std::move(before));
    }
}

FLASHMEM void SequencerStructureEditWorkflow::duplicateSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    bool changed = false;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        const uint16_t historyMask = duplicateTrackHistoryMask(
            currentTrackEnabledMask(),
            selectedMask,
            currentActiveTrack()
        );
        auto change = captureTrackHistoryBefore(historyMask);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
            currentTrackEnabledMask(),
            selectedMask,
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT,
            [this](uint8_t source, uint8_t dest) {
                const auto& sourceTrack =
                    (source == currentActiveTrack()) ? sequencer_.pattern : tracks_.track(source);
                core::state::sequencer::copyPatternState(tracks_.track(dest), sourceTrack);
            }
        );

        if (result.firstDuplicated < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
            changed = applyTrackState(result.nextMask, result.firstDuplicated);
        }
        if (!changed) return;
        cancelSelectionMode();
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    } else {
        HistoryPatternSnapshot before;
        if (!capturePageHistoryBefore(before)) return;
        const auto plan = core::state::sequencer::buildPageDuplicatePlan(
            sequencer_,
            selectedMask,
            selection.cursorIndex.get()
        );
        if (!plan.movesAnyPage()) return;
        changed = core::state::sequencer::duplicatePagesFromPlan(sequencer_, plan);
        if (!changed) return;
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

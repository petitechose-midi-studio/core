#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "state/StructureClipboardPastePlan.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

FLASHMEM bool selectedStepRange(
    const oc::note::sequencer::StepBitMask128& mask,
    uint8_t activeLength,
    uint8_t& outFirst,
    uint8_t& outLast
) {
    bool found = false;
    outFirst = 0;
    outLast = 0;
    for (uint8_t step = 0; step < activeLength; ++step) {
        if (!mask.test(step)) continue;
        if (!found) {
            outFirst = step;
            found = true;
        }
        outLast = step;
    }
    return found;
}

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks
) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        tracks.projectScaleSettings(),
        sequencer.pattern.scalePolicy,
        sequencer.pattern.scaleOverride
    );
}

enum class StepResetDepth : uint8_t {
    Shallow,
    Deep,
};

FLASHMEM bool resetActiveContentStep(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    StepResetDepth depth
) {
    if (step >= core::state::sequencer::activeContentLength(sequencer)) return false;

    bool changed = false;
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    if (core::state::sequencer::isRootContentView(sequencer)) {
        changed = sequencer.pattern.isEnabled(step) || changed;
        sequencer.pattern.setEnabled(step, false);
        changed = sequencer.setStepDataAt(
            step,
            core::state::sequencer::SequencerState::DEFAULT_NOTE,
            core::state::sequencer::SequencerState::DEFAULT_VELOCITY,
            core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT,
            0,
            core::state::sequencer::SequencerState::DEFAULT_PROBABILITY
        ) || changed;
        changed = (depth == StepResetDepth::Shallow
            ? core::state::sequencer::resetStepNodePayloadPreservingChildren(
                  sequencer.pattern,
                  nodeId
              )
            : core::state::sequencer::resetStepNodePayload(
                  sequencer.pattern,
                  nodeId
              )) || changed;
        return changed;
    }

    changed = (depth == StepResetDepth::Shallow
        ? core::state::sequencer::resetStepNodePayloadPreservingChildren(
              sequencer.pattern,
              nodeId,
              core::state::sequencer::SequencerGraphNodeResetMode::DISABLED_OVERRIDE
          )
        : core::state::sequencer::resetStepNodePayload(
              sequencer.pattern,
              nodeId,
              core::state::sequencer::SequencerGraphNodeResetMode::DISABLED_OVERRIDE
          )) || changed;
    if (changed) sequencer.contentView.bump();
    return changed;
}

FLASHMEM bool appendStepClipboardEntry(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    uint8_t firstStep,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    core::state::SequencerStepsClipboard& clipboard
) {
    if (clipboard.count >= clipboard.entries.size()) return false;

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        scaleSettings
    );
    if (!projection.valid) return false;

    auto& entry = clipboard.entries[clipboard.count++];
    entry.valid = true;
    entry.offset = static_cast<uint8_t>(step - firstStep);
    entry.enabled = projection.enabled;
    entry.sourceNodeId = projection.nodeId;
    if (clipboard.rootContext) {
        entry.note = projection.parentNote;
        entry.velocity = projection.parentVelocity;
        entry.gate = projection.parentGate;
        entry.nudge = projection.parentNudge;
        entry.probability = projection.parentProbability;
    } else {
        entry.note = projection.note;
        entry.velocity = projection.velocity;
        entry.gate = projection.gate;
        entry.nudge = projection.nudge;
        entry.probability = projection.probability;
    }
    return true;
}

FLASHMEM bool writeRootStepFromClipboardEntry(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepClipboardEntry& entry,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetStep
) {
    if (targetStep >= core::state::sequencer::SequencerState::MAX_STEPS) return false;

    sequencer.pattern.setEnabled(targetStep, entry.enabled);
    (void)sequencer.setStepDataAt(
        targetStep,
        entry.note,
        entry.velocity,
        entry.gate,
        entry.nudge,
        entry.probability
    );

    const auto targetNode = core::state::sequencer::rootStepNodeId(targetStep);
    if (sourceGraph != nullptr &&
        entry.sourceNodeId != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID) {
        return core::state::sequencer::copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNode,
            *sourceGraph,
            entry.sourceNodeId
        );
    }

    (void)core::state::sequencer::resetStepNodePayload(sequencer.pattern, targetNode);
    return true;
}

FLASHMEM bool writeChildStepFromClipboardEntry(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepClipboardEntry& entry,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetStep
) {
    if (sourceGraph == nullptr ||
        entry.sourceNodeId == oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID ||
        targetStep >= core::state::sequencer::activeContentLength(sequencer)) {
        return false;
    }

    const auto targetNode = core::state::sequencer::activeContentStepNodeId(sequencer, targetStep);
    if (!core::state::sequencer::copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNode,
            *sourceGraph,
            entry.sourceNodeId
        )) {
        return false;
    }
    sequencer.contentView.bump();
    return true;
}

FLASHMEM uint8_t firstSelectedIndex(uint16_t mask, uint8_t limit) {
    for (uint8_t index = 0; index < limit; ++index) {
        if ((mask & structure_slots::slotBit(index)) != 0) return index;
    }
    return limit;
}

FLASHMEM bool capturePageClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t page,
    core::state::SequencerPageClipboard& clipboard
) {
    clipboard.reset();
    if (page >= core::state::sequencer::SequencerState::PAGE_COUNT) return false;

    const uint8_t start = static_cast<uint8_t>(
        page * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    const uint8_t len = sequencer.pattern.length.get();
    const uint8_t count = (start >= len)
        ? 0
        : static_cast<uint8_t>(std::min<uint16_t>(
              core::state::sequencer::SequencerState::STEPS_PER_PAGE,
              static_cast<uint16_t>(len - start)
          ));
    if (count == 0) return false;

    clipboard.valid = true;
    clipboard.sourcePage = page;
    clipboard.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t step = static_cast<uint8_t>(start + i);
        clipboard.note[i] = sequencer.pattern.note[step];
        clipboard.velocity[i] = sequencer.pattern.velocity[step];
        clipboard.gate[i] = sequencer.pattern.gate[step];
        clipboard.nudge[i] = sequencer.pattern.nudge[step];
        clipboard.probability[i] = sequencer.pattern.probability[step];
        if (sequencer.pattern.isEnabled(step)) {
            clipboard.enabledMask |= static_cast<uint8_t>(1U << i);
        }
    }
    return true;
}

FLASHMEM void copyPageStepContentFromGraph(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerPageClipboard& clipboard,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetPage
) {
    if (sourceGraph == nullptr || !sourceGraph->enabled) return;

    const uint8_t sourceStart = static_cast<uint8_t>(
        clipboard.sourcePage *
        core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    const uint8_t targetStart = static_cast<uint8_t>(
        targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const uint8_t sourceStep = static_cast<uint8_t>(sourceStart + i);
        const uint8_t targetStep = static_cast<uint8_t>(targetStart + i);
        if (sourceStep >= core::state::sequencer::SequencerState::MAX_STEPS ||
            targetStep >= core::state::sequencer::SequencerState::MAX_STEPS) {
            break;
        }

        const auto sourceNode = core::state::sequencer::rootStepNodeId(sourceStep);
        const auto targetNode = core::state::sequencer::rootStepNodeId(targetStep);
        core::state::sequencer::copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNode,
            *sourceGraph,
            sourceNode
        );
    }
}

FLASHMEM void pastePageClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerPageClipboard& clipboard,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetPage
) {
    const uint8_t targetStart =
        static_cast<uint8_t>(targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE);
    const uint8_t targetEnd = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(
            targetStart + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1
        )
    ));
    core::state::sequencer::clearStepRange(sequencer, targetStart, targetEnd);

    const uint8_t requiredLength = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS,
        static_cast<uint16_t>(targetStart + std::max<uint8_t>(clipboard.count, 1))
    ));
    if (sequencer.pattern.length.get() < requiredLength) {
        sequencer.pattern.length.set(requiredLength);
    }

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const uint8_t step = static_cast<uint8_t>(targetStart + i);
        sequencer.pattern.note[step] = clipboard.note[i];
        sequencer.pattern.velocity[step] = clipboard.velocity[i];
        sequencer.pattern.gate[step] = clipboard.gate[i];
        sequencer.pattern.nudge[step] = clipboard.nudge[i];
        sequencer.pattern.probability[step] = clipboard.probability[i];
        sequencer.pattern.setEnabled(step, clipboard.isEnabled(i));
    }

    copyPageStepContentFromGraph(sequencer, clipboard, sourceGraph, targetPage);
}

}  // namespace

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
        const bool nextMuted = !tracks_.isTrackMuted(activeTrack);
        if (!tracks_.setTrackMuted(activeTrack, nextMuted)) return;
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        eraseFocusedStep();
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

FLASHMEM void SequencerStructureEditWorkflow::toggleTrackSelectionMute() {
    auto& selection = track_ui_.selection;
    if (!selection.active.get() ||
        selection.scope.get() != core::state::StructureSelectionScope::TRACK) {
        return;
    }

    const uint16_t selectedMask = static_cast<uint16_t>(
        selection.selectedMask.get() & currentTrackEnabledMask()
    );
    if (selectedMask == 0) return;

    bool anyAudible = false;
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint16_t bit = structure_slots::slotBit(track);
        if ((selectedMask & bit) == 0) continue;
        anyAudible = anyAudible || !tracks_.isTrackMuted(track);
    }

    const uint16_t historyMask = static_cast<uint16_t>(
        selectedMask | sequencerStructureHistoryTrackBit(currentActiveTrack())
    );
    auto change = captureTrackHistoryBefore(historyMask);

    bool changed = false;
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint16_t bit = structure_slots::slotBit(track);
        if ((selectedMask & bit) == 0) continue;
        changed = tracks_.setTrackMuted(track, anyAudible) || changed;
    }

    if (!changed) return;
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
    if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

    core::state::SequencerStepsClipboard clipboard;
    clipboard.valid = true;
    clipboard.rootContext = core::state::sequencer::isRootContentView(sequencer_);
    clipboard.span = 1;

    if (!appendStepClipboardEntry(
            sequencer_,
            step,
            step,
            effectiveScaleSettings(sequencer_, tracks_),
            clipboard
        )) {
        return;
    }

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

    const uint16_t selectedMask = static_cast<uint16_t>(
        selection.selectedMask.get() & structure_slots::prefixMask(sequencer_.activePageCount())
    );
    if (selectedMask == 0) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    bool changed = false;
    for (uint8_t page = 0; page < core::state::sequencer::SequencerState::PAGE_COUNT; ++page) {
        if ((selectedMask & structure_slots::slotBit(page)) == 0) continue;
        const uint8_t start = static_cast<uint8_t>(
            page * core::state::sequencer::SequencerState::STEPS_PER_PAGE
        );
        const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
            core::state::sequencer::SequencerState::MAX_STEPS - 1,
            static_cast<uint16_t>(
                start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1
            )
        ));
        changed = core::state::sequencer::clearStepRange(sequencer_, start, end) || changed;
    }

    if (!changed) return;
    sequencer_.pattern.bumpStepDataRevision();
    recordPageHistoryAfter(std::move(before));
}

FLASHMEM void SequencerStructureEditWorkflow::copySelection() {
    if (track_ui_.selection.active.get()) {
        const uint16_t selectedMask = static_cast<uint16_t>(
            track_ui_.selection.selectedMask.get() & currentTrackEnabledMask()
        );
        const uint8_t firstTrack = firstSelectedIndex(
            selectedMask,
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (firstTrack >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) return;

        auto clipboard = core::app::makeExtmemUnique<core::state::SequencerTrackSelectionClipboard>();
        if (!clipboard) return;
        clipboard->valid = true;

        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        for (uint8_t track = firstTrack;
             track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
             ++track) {
            if ((selectedMask & structure_slots::slotBit(track)) == 0) continue;
            if (clipboard->count >= clipboard->tracks.size()) break;

            auto& entry = clipboard->tracks[clipboard->count++];
            entry.valid = true;
            entry.offset = static_cast<uint8_t>(track - firstTrack);
            core::state::sequencer::captureSnapshot(tracks_.track(track), entry.snapshot);
            if (!core::state::cloneSequencerGraph(
                    entry.graph,
                    core::state::sequencer::graphView(tracks_.track(track))
                )) {
                return;
            }
        }

        if (clipboard->count == 0) return;
        structure_clipboard_.storeSequencerTrackSelection(std::move(clipboard));
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = static_cast<uint16_t>(
        selection.selectedMask.get() & structure_slots::prefixMask(sequencer_.activePageCount())
    );
    const uint8_t firstPage = firstSelectedIndex(
        selectedMask,
        core::state::sequencer::SequencerState::PAGE_COUNT
    );
    if (firstPage >= core::state::sequencer::SequencerState::PAGE_COUNT) return;

    core::state::SequencerPageSelectionClipboard clipboard;
    clipboard.valid = true;
    clipboard.sourceFirstPage = firstPage;

    for (uint8_t page = firstPage;
         page < core::state::sequencer::SequencerState::PAGE_COUNT;
         ++page) {
        if ((selectedMask & structure_slots::slotBit(page)) == 0) continue;
        if (clipboard.count >= clipboard.pages.size()) break;

        auto& entry = clipboard.pages[clipboard.count];
        if (!capturePageClipboard(sequencer_, page, entry)) continue;
        ++clipboard.count;
    }

    if (clipboard.count == 0) return;
    structure_clipboard_.storeSequencerPageSelection(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    );
}

FLASHMEM void SequencerStructureEditWorkflow::copyStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;

    const uint8_t activeLength = core::state::sequencer::activeContentLength(sequencer_);
    uint8_t first = 0;
    uint8_t last = 0;
    const auto selectedMask = selection.selectedMask.get();
    if (!selectedStepRange(selectedMask, activeLength, first, last)) return;

    const auto scaleSettings = effectiveScaleSettings(sequencer_, tracks_);
    core::state::SequencerStepsClipboard clipboard;
    clipboard.valid = true;
    clipboard.rootContext = core::state::sequencer::isRootContentView(sequencer_);
    clipboard.span = static_cast<uint8_t>(last - first + 1U);

    for (uint8_t step = first; step <= last; ++step) {
        if (!selectedMask.test(step)) continue;
        if (clipboard.count >= clipboard.entries.size()) break;

        (void)appendStepClipboardEntry(sequencer_, step, first, scaleSettings, clipboard);
    }

    if (clipboard.count == 0) return;
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
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;

    selection.pastePreviewActive.set(true);
    if (!structure_clipboard_.hasSequencerSteps()) {
        selection.pastePreview.set(core::state::sequencer::SequencerStepPastePreview::BLOCKED);
        return;
    }

    const auto mode = core::state::project::sanitizeProjectStepPasteMode(
        project_navigation_.stepPasteMode
    );
    const uint8_t activeLength = core::state::sequencer::activeContentLength(sequencer_);
    const uint8_t maxStep = core::state::sequencer::maxStepCursorForPaste(sequencer_);
    const auto plan = core::state::sequencer::buildStepPastePreviewPlan(
        structure_clipboard_.sequencerSteps,
        core::state::sequencer::isRootContentView(sequencer_),
        selection.cursorStep.get(),
        activeLength,
        maxStep,
        mode
    );
    selection.pastePreview.set(plan.aggregate);
}

FLASHMEM void SequencerStructureEditWorkflow::clearStepPastePreview() {
    auto& selection = sequencer_.structureUi.stepSelection;
    selection.pastePreviewActive.set(false);
    selection.pastePreview.set(core::state::sequencer::SequencerStepPastePreview::NONE);
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepClipboardAt(
    uint8_t cursorStep,
    bool resetSelection
) {
    if (!structure_clipboard_.hasSequencerSteps()) return;

    const auto mode = core::state::project::sanitizeProjectStepPasteMode(
        project_navigation_.stepPasteMode
    );
    const uint8_t activeLength = core::state::sequencer::activeContentLength(sequencer_);
    const uint8_t maxStep = core::state::sequencer::maxStepCursorForPaste(sequencer_);
    const auto plan = core::state::sequencer::buildStepPastePreviewPlan(
        structure_clipboard_.sequencerSteps,
        core::state::sequencer::isRootContentView(sequencer_),
        cursorStep,
        activeLength,
        maxStep,
        mode
    );
    if (plan.blocked || !plan.hasEntries()) {
        clearStepPastePreview();
        return;
    }
    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    if (!core::state::sequencer::resizeActiveContentForStepPaste(
            sequencer_,
            mode,
            plan.lastTarget,
            maxStep
        )) {
        clearStepPastePreview();
        return;
    }

    const auto* sourceGraph = structure_clipboard_.sequencerGraph.get();
    bool changed = false;
    for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& preview = plan.entries[i];
        if (!preview.valid) continue;
        const auto& entry =
            structure_clipboard_.sequencerSteps.entries[preview.clipboardIndex];
        if (!entry.valid) continue;
        changed = structure_clipboard_.sequencerSteps.rootContext
            ? writeRootStepFromClipboardEntry(
                  sequencer_,
                  entry,
                  sourceGraph,
                  preview.targetStep
              ) || changed
            : writeChildStepFromClipboardEntry(
                  sequencer_,
                  entry,
                  sourceGraph,
                  preview.targetStep
              ) || changed;
    }

    if (!changed) {
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
        uint16_t targetMask = 0;
        uint8_t firstTarget = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
        for (uint8_t i = 0; i < clipboard->count; ++i) {
            const auto& entry = clipboard->tracks[i];
            if (!entry.valid) continue;
            const uint16_t target = static_cast<uint16_t>(cursorTrack) + entry.offset;
            if (target >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) continue;

            const uint8_t targetTrack = static_cast<uint8_t>(target);
            targetMask = static_cast<uint16_t>(
                targetMask | structure_slots::slotBit(targetTrack)
            );
            firstTarget = std::min(firstTarget, targetTrack);
        }
        if (targetMask == 0) return;

        const uint8_t previousActive = currentActiveTrack();
        const uint16_t historyMask = static_cast<uint16_t>(
            targetMask | sequencerStructureHistoryTrackBit(previousActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);

        for (uint8_t i = 0; i < clipboard->count; ++i) {
            const auto& entry = clipboard->tracks[i];
            if (!entry.valid) continue;
            const uint16_t target = static_cast<uint16_t>(cursorTrack) + entry.offset;
            if (target >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) continue;

            const uint8_t targetTrack = static_cast<uint8_t>(target);
            core::state::sequencer::applySnapshot(tracks_.track(targetTrack), entry.snapshot);
            core::state::sequencer::copyGraph(
                tracks_.track(targetTrack),
                entry.graph.get(),
                entry.snapshot.graphRevision
            );
            if (targetTrack == previousActive) {
                core::state::sequencer::applySnapshotToEditor(sequencer_, entry.snapshot);
                core::state::sequencer::copyGraph(
                    sequencer_.pattern,
                    entry.graph.get(),
                    entry.snapshot.graphRevision
                );
            }
        }

        const uint16_t nextMask = static_cast<uint16_t>(currentTrackEnabledMask() | targetMask);
        applyTrackState(nextMask, firstTarget);
        cancelSelectionMode();
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get() || !structure_clipboard_.hasSequencerPageSelection()) return;

    const auto& clipboard = structure_clipboard_.sequencerPageSelection;
    const auto plan = core::state::buildSequencerPageSelectionPastePlan(
        clipboard,
        selection.cursorIndex.get(),
        sequencer_.activePageCount()
    );
    if (!plan.hasEntries()) return;

    HistoryPatternSnapshot before;
    if (!capturePageHistoryBefore(before)) return;

    for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& target = plan.entries[i];
        const auto& entry = clipboard.pages[target.clipboardIndex];
        pastePageClipboard(
            sequencer_,
            entry,
            structure_clipboard_.sequencerGraph.get(),
            target.destinationPage
        );
    }

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

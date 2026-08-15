#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <algorithm>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/Timing.hpp>
#include <utility>

#include "handler/sequencer/SequencerDirectTrackStructureTransaction.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureMutationPlan.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureSelectionOps.hpp"
#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "handler/sequencer/SequencerStructureStepPasteWorkflow.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;
namespace contextual = core::state::contextual;
namespace seq = core::state::sequencer;

namespace {

constexpr uint32_t TRACK_PASTE_CANCELLED_MS = 700;
constexpr uint32_t TRACK_PASTE_APPLIED_MS = 1200;

constexpr uint8_t TRACK_SELECTION_HOLD_SCOPE_TRACK = 1U << 0U;
constexpr uint8_t TRACK_SELECTION_HOLD_PLACING = 1U << 1U;
constexpr uint8_t TRACK_SELECTION_HOLD_PASTE_BLOCKED = 1U << 2U;
constexpr uint8_t TRACK_SELECTION_HOLD_PREVIEW_ADD = 1U << 3U;

constexpr uint16_t drumLaneMask(uint8_t laneCount) noexcept {
    return laneCount >= 16U
        ? 0xFFFFU
        : laneCount == 0U
            ? 0U
            : static_cast<uint16_t>((uint16_t{1U} << laneCount) - 1U);
}

FLASHMEM uint8_t countDrumLanes(uint16_t mask) noexcept {
    uint8_t count = 0U;
    while (mask != 0U) {
        count = static_cast<uint8_t>(count + (mask & 1U));
        mask = static_cast<uint16_t>(mask >> 1U);
    }
    return count;
}

FLASHMEM uint8_t firstDrumLane(uint16_t mask) noexcept {
    for (uint8_t lane = 0U; lane < seq::DRUM_MAX_LANES; ++lane) {
        if ((mask & static_cast<uint16_t>(1U << lane)) != 0U) {
            return lane;
        }
    }
    return seq::DRUM_MAX_LANES;
}

constexpr uint16_t drumLaneDestinationMask(
    uint8_t cursor,
    uint8_t count,
    uint8_t laneCount
) noexcept {
    if (cursor >= laneCount || count == 0U) return 0U;
    const uint8_t visibleCount = std::min<uint8_t>(
        count,
        static_cast<uint8_t>(laneCount - cursor)
    );
    const uint16_t prefix = visibleCount >= 16U
        ? 0xFFFFU
        : static_cast<uint16_t>((uint16_t{1U} << visibleCount) - 1U);
    return static_cast<uint16_t>(prefix << cursor);
}

FLASHMEM bool applyDrumStepClipboardEntry(
    seq::DrumPatternState& pattern,
    uint8_t lane,
    uint8_t step,
    const core::state::SequencerStepClipboardEntry& entry
) {
    bool changed = false;
    changed = pattern.setStepEnabled(lane, step, entry.enabled) || changed;
    changed = pattern.setStepVelocity(lane, step, entry.velocity) || changed;
    changed = pattern.setStepGate(lane, step, entry.gate) || changed;
    changed = pattern.setStepNudge(lane, step, entry.nudge) || changed;
    changed = pattern.setStepProbability(lane, step, entry.probability) || changed;
    return changed;
}

FLASHMEM core::state::SequencerStepClipboardEntry defaultDrumStepEntry() {
    core::state::SequencerStepClipboardEntry entry{};
    entry.valid = true;
    entry.enabled = false;
    entry.velocity = seq::DRUM_DEFAULT_VELOCITY;
    entry.gate = seq::DRUM_DEFAULT_GATE_PERCENT;
    entry.nudge = 0;
    entry.probability = seq::DRUM_DEFAULT_PROBABILITY;
    return entry;
}

FLASHMEM seq::SequencerHistoryDescriptor drumStepActionDescriptor(
    const seq::DrumSequencerState& drumUi,
    seq::SequencerHistoryActionKind kind,
    uint8_t lane,
    uint8_t step
) {
    return {
        .kind = kind,
        .trackIndex = drumUi.targetTrack,
        .laneIndex = lane,
        .stepIndex = step,
        .property = seq::StepProperty::NOTE,
    };
}

constexpr uint8_t packTrackSelectionHoldFlags(
    core::state::StructureSelectionScope scope,
    bool placing,
    bool pasteBlocked,
    bool previewAddTrack
) noexcept {
    return static_cast<uint8_t>(
        (scope == core::state::StructureSelectionScope::TRACK
             ? TRACK_SELECTION_HOLD_SCOPE_TRACK
             : 0U) |
        (placing ? TRACK_SELECTION_HOLD_PLACING : 0U) |
        (pasteBlocked ? TRACK_SELECTION_HOLD_PASTE_BLOCKED : 0U) |
        (previewAddTrack ? TRACK_SELECTION_HOLD_PREVIEW_ADD : 0U)
    );
}

enum class PreparedStructureSettlement : uint8_t {
    Failed = 0U,
    NoChange,
    Committed,
};

constexpr uint16_t packPreparedStructureSettlement(
    PreparedStructureSettlement outcome,
    uint8_t finalFocus = 0U
) noexcept {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(outcome) << 8U) | finalFocus);
}

constexpr PreparedStructureSettlement preparedStructureSettlementOutcome(
    uint16_t settlement
) noexcept {
    return static_cast<PreparedStructureSettlement>(settlement >> 8U);
}

constexpr uint8_t preparedStructureSettlementFocus(
    uint16_t settlement
) noexcept {
    return static_cast<uint8_t>(settlement & 0xFFU);
}

}  // namespace

FLASHMEM SequencerStructureEditWorkflow::SequencerStructureEditWorkflow(StateRefs state)
    : sequencer_(state.sequencer), tracks_(state.tracks), navigation_focus_(state.navigationFocus),
      track_ui_(state.trackNavigation), project_navigation_(state.projectNavigation),
      project_tracks_(state.projectTracks), project_track_domain_(state.projectTrackDomain),
      structure_clipboard_(state.structureClipboard), shared_tracks_(state.sharedTracks),
      history_(state.history), macro_pages_(state.macroPages),
      track_activations_(state.trackActivations), status_bar_(state.statusBar) {}

FLASHMEM bool
SequencerStructureEditWorkflow::drumStepActionsAvailable() const {
    const auto& drumUi = sequencer_.drumSequencer;
    return drumUi.gridVisible() && !drumUi.selectorVisible() &&
           !sequencer_.contextSelector.visible &&
           !drumUi.laneAddSlotFocused() && drumUi.drumTrack != nullptr &&
           navigation_focus_.get() ==
               core::state::StructureNavigationFocus::STEP &&
           drumUi.stepInRange(drumUi.selectedLane, drumUi.focusedStep);
}

FLASHMEM bool
SequencerStructureEditWorkflow::canPasteDrumFocusedStep() const {
    if (!drumStepActionsAvailable() ||
        !structure_clipboard_.hasSequencerSteps()) {
        return false;
    }
    const auto& clipboard = structure_clipboard_.sequencerSteps;
    if (!clipboard.drumContext || clipboard.count != 1U ||
        !clipboard.entries[0].valid) {
        return false;
    }
    const auto sourceNodeId = clipboard.entries[0].sourceNodeId;
    if (sourceNodeId ==
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID) {
        return true;
    }
    if (!structure_clipboard_.sequencerGraph) return false;
    return seq::inspectSequencerGraphPayload(
               *structure_clipboard_.sequencerGraph,
               sourceNodeId,
               0U
           ).ok();
}

FLASHMEM bool
SequencerStructureEditWorkflow::canPasteDrumLaneSelection() const {
    const auto& drumUi = sequencer_.drumSequencer;
    const auto& selection = drumUi.laneSelection;
    if (!drumUi.gridVisible() || drumUi.drumTrack == nullptr ||
        navigation_focus_.get() !=
            core::state::StructureNavigationFocus::PAGE ||
        !selection.placementActive() || selection.pasteBlocked ||
        selection.clipboardRevision != structure_clipboard_.revision.get() ||
        !structure_clipboard_.hasSequencerDrumLaneSelection() ||
        selection.clipboardCount !=
            structure_clipboard_.sequencerDrumLaneSelectionCount) {
        return false;
    }
    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        seq::DRUM_MAX_LANES
    );
    if (selection.cursorLane >= laneCount ||
        selection.clipboardCount == 0U ||
        selection.cursorLane + selection.clipboardCount > laneCount) {
        return false;
    }

    const auto& source = *structure_clipboard_.sequencerDrumTrack;
    const uint16_t sourceMask =
        structure_clipboard_.sequencerDrumLaneSelectionMask;
    const auto* graph = structure_clipboard_.sequencerGraph.get();
    for (uint8_t lane = 0U; lane < seq::DRUM_MAX_LANES; ++lane) {
        if ((sourceMask & static_cast<uint16_t>(1U << lane)) == 0U) {
            continue;
        }
        for (uint8_t step = 0U; step < seq::DRUM_MAX_STEPS; ++step) {
            const int16_t slot = source.advancedRootSlot(lane, step);
            if (slot < 0) continue;
            if (graph == nullptr ||
                !seq::inspectSequencerGraphPayload(
                    *graph,
                    seq::rootStepNodeId(static_cast<uint8_t>(slot)),
                    0U
                ).ok()) {
                return false;
            }
        }
    }
    return true;
}

FLASHMEM bool
SequencerStructureEditWorkflow::clearDrumLaneAdvancedContent(
    uint16_t laneMask,
    bool& changed
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.drumTrack == nullptr) return false;
    auto& pattern = seq::authoringPattern(sequencer_);
    bool graphChanged = false;
    for (uint8_t lane = 0U; lane < seq::DRUM_MAX_LANES; ++lane) {
        if ((laneMask & static_cast<uint16_t>(1U << lane)) == 0U) {
            continue;
        }
        for (uint8_t step = 0U; step < seq::DRUM_MAX_STEPS; ++step) {
            const int16_t slot =
                drumUi.drumTrack->advancedRootSlot(lane, step);
            if (slot < 0) continue;
            const auto nodeId =
                seq::rootStepNodeId(static_cast<uint8_t>(slot));
            const auto* graph = seq::graphView(pattern);
            const auto* node = graph == nullptr
                ? nullptr
                : graph->stepNode(nodeId);
            if (node == nullptr) return false;
            if (!seq::isDefaultSequencerGraphNodePayload(*node)) {
                if (!seq::resetStepNodePayload(pattern, nodeId)) return false;
                graphChanged = true;
                changed = true;
            }
            if (!drumUi.drumTrack->releaseAdvancedRootSlot(lane, step)) {
                return false;
            }
            changed = true;
        }
    }
    if (graphChanged && !seq::compactGraph(pattern).ok) return false;
    return true;
}

FLASHMEM bool
SequencerStructureEditWorkflow::drumLaneSelectionPasteMatches() const {
    if (!canPasteDrumLaneSelection()) return false;
    const auto& drumUi = sequencer_.drumSequencer;
    const auto& source = *structure_clipboard_.sequencerDrumTrack;
    const auto* sourceGraph = structure_clipboard_.sequencerGraph.get();
    const auto* targetGraph = seq::graphView(seq::authoringPattern(sequencer_));
    const uint16_t sourceMask =
        structure_clipboard_.sequencerDrumLaneSelectionMask;
    uint8_t destination = drumUi.laneSelection.cursorLane;
    for (uint8_t sourceLane = 0U;
         sourceLane < seq::DRUM_MAX_LANES;
         ++sourceLane) {
        if ((sourceMask & static_cast<uint16_t>(1U << sourceLane)) == 0U) {
            continue;
        }
        if (!seq::sameDrumLanePattern(
                source.pattern.lanes[sourceLane],
                drumUi.drumTrack->pattern.lanes[destination])) {
            return false;
        }
        for (uint8_t step = 0U; step < seq::DRUM_MAX_STEPS; ++step) {
            const int16_t sourceSlot = source.advancedRootSlot(sourceLane, step);
            const int16_t targetSlot =
                drumUi.drumTrack->advancedRootSlot(destination, step);
            if ((sourceSlot < 0) != (targetSlot < 0)) return false;
            if (sourceSlot < 0) continue;
            if (sourceGraph == nullptr || targetGraph == nullptr) return false;
            const auto comparison = seq::compareSequencerGraphPayloads(
                *sourceGraph,
                seq::rootStepNodeId(static_cast<uint8_t>(sourceSlot)),
                *targetGraph,
                seq::rootStepNodeId(static_cast<uint8_t>(targetSlot)),
                0U
            );
            if (!comparison.ok() || !comparison.same) return false;
        }
        ++destination;
    }
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::clearDrumLaneSelection() {
    auto& drumUi = sequencer_.drumSequencer;
    auto& selection = drumUi.laneSelection;
    if (!selection.active || selection.placing || selection.moving ||
        drumUi.drumTrack == nullptr) {
        return;
    }
    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        seq::DRUM_MAX_LANES
    );
    const uint16_t selectedMask = static_cast<uint16_t>(
        selection.selectedMask & drumLaneMask(laneCount)
    );
    if (selectedMask == 0U) return;

    auto descriptor = drumStepActionDescriptor(
        drumUi,
        seq::SequencerHistoryActionKind::DrumLaneContent,
        selection.cursorLane,
        seq::SequencerHistoryDescriptor::INVALID_INDEX
    );
    if (!beginDrumStepActionHistory(descriptor)) return;

    bool changed = false;
    seq::DrumLanePattern empty{};
    empty.reset();
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        if ((selectedMask & static_cast<uint16_t>(1U << lane)) == 0U) {
            continue;
        }
        changed = drumUi.drumTrack->pattern.replaceLanePattern(lane, empty) ||
            changed;
    }
    if (!clearDrumLaneAdvancedContent(selectedMask, changed)) {
        (void)history_.abortCoalescedDrumEdit();
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            core::time_compat::millis()
        );
        return;
    }
    if (changed) drumUi.publishAuthoredMutation();
    (void)sealDrumStepActionHistory(changed, descriptor);
}

FLASHMEM void SequencerStructureEditWorkflow::copyDrumLaneSelection() {
    auto& drumUi = sequencer_.drumSequencer;
    auto& selection = drumUi.laneSelection;
    if (!selection.active || selection.placing || selection.moving ||
        drumUi.drumTrack == nullptr) {
        return;
    }
    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        seq::DRUM_MAX_LANES
    );
    const uint16_t selectedMask = static_cast<uint16_t>(
        selection.selectedMask & drumLaneMask(laneCount)
    );
    if (selectedMask == 0U ||
        !structure_clipboard_.storeSequencerDrumLaneSelection(
            *drumUi.drumTrack,
            selectedMask,
            seq::graphView(seq::authoringPattern(sequencer_))
        )) {
        return;
    }
    selection.placing = true;
    selection.clipboardRevision = structure_clipboard_.revision.get();
    selection.clipboardCount = countDrumLanes(selectedMask);
    selection.destinationMask = drumLaneDestinationMask(
        selection.cursorLane,
        selection.clipboardCount,
        laneCount
    );
    selection.overwriteMask = selection.destinationMask;
    selection.pasteBlocked =
        selection.cursorLane + selection.clipboardCount > laneCount;
    drumUi.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::pasteDrumLaneSelection() {
    if (!canPasteDrumLaneSelection() || drumLaneSelectionPasteMatches()) {
        return;
    }
    auto& drumUi = sequencer_.drumSequencer;
    auto& selection = drumUi.laneSelection;
    const auto& source = *structure_clipboard_.sequencerDrumTrack;
    const auto* sourceGraph = structure_clipboard_.sequencerGraph.get();
    const uint16_t sourceMask =
        structure_clipboard_.sequencerDrumLaneSelectionMask;
    const uint16_t destinationMask = drumLaneDestinationMask(
        selection.cursorLane,
        selection.clipboardCount,
        drumUi.drumTrack->kit.laneCount
    );

    auto descriptor = drumStepActionDescriptor(
        drumUi,
        seq::SequencerHistoryActionKind::DrumLaneContent,
        selection.cursorLane,
        seq::SequencerHistoryDescriptor::INVALID_INDEX
    );
    if (!beginDrumStepActionHistory(descriptor)) return;

    bool changed = false;
    if (!clearDrumLaneAdvancedContent(destinationMask, changed)) {
        (void)history_.abortCoalescedDrumEdit();
        return;
    }

    uint8_t destination = selection.cursorLane;
    for (uint8_t sourceLane = 0U;
         sourceLane < seq::DRUM_MAX_LANES;
         ++sourceLane) {
        if ((sourceMask & static_cast<uint16_t>(1U << sourceLane)) == 0U) {
            continue;
        }
        changed = drumUi.drumTrack->pattern.replaceLanePattern(
            destination,
            source.pattern.lanes[sourceLane]
        ) || changed;
        for (uint8_t step = 0U; step < seq::DRUM_MAX_STEPS; ++step) {
            const int16_t sourceSlot = source.advancedRootSlot(sourceLane, step);
            if (sourceSlot < 0) continue;
            bool mappingChanged = false;
            const int16_t targetSlot = seq::ensureDrumAdvancedRootSlot(
                *drumUi.drumTrack,
                seq::authoringPattern(sequencer_),
                destination,
                step,
                mappingChanged
            );
            if (targetSlot < 0 || sourceGraph == nullptr ||
                !seq::copyStepNodePayloadFromGraph(
                    seq::authoringPattern(sequencer_),
                    seq::rootStepNodeId(static_cast<uint8_t>(targetSlot)),
                    *sourceGraph,
                    seq::rootStepNodeId(static_cast<uint8_t>(sourceSlot))
                )) {
                (void)history_.abortCoalescedDrumEdit();
                sequencer_.historyFeedback.showRejection(
                    seq::SequencerHistoryRejectionReason::HistoryUnavailable,
                    core::time_compat::millis()
                );
                return;
            }
            changed = true;
        }
        ++destination;
    }

    if (changed) drumUi.publishAuthoredMutation();
    if (!sealDrumStepActionHistory(changed, descriptor)) return;
    selection.destinationMask = destinationMask;
    selection.overwriteMask = destinationMask;
    drumUi.bump();
}

FLASHMEM bool SequencerStructureEditWorkflow::beginDrumStepActionHistory(
    seq::SequencerHistoryDescriptor descriptor
) {
    const auto outcome = history_.beginCoalescedDrumEdit(
        descriptor,
        core::time_compat::millis()
    );
    if (seq::sequencerHistoryOpenAccepted(outcome)) return true;
    sequencer_.historyFeedback.showRejection(
        outcome,
        core::time_compat::millis()
    );
    return false;
}

FLASHMEM bool SequencerStructureEditWorkflow::sealDrumStepActionHistory(
    bool changed,
    seq::SequencerHistoryDescriptor descriptor
) {
    if (!history_.sealCoalescedDrumEdit(changed, descriptor)) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            core::time_compat::millis()
        );
        return false;
    }
    const auto outcome = history_.commitCoalescedDrumEditOutcome();
    if (outcome != seq::SequencerPatternHistoryCommitOutcome::Failed) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(
        seq::SequencerHistoryRejectionReason::HistoryUnavailable,
        core::time_compat::millis()
    );
    return false;
}

FLASHMEM bool SequencerStructureEditWorkflow::clearDrumAdvancedStep(
    uint8_t lane,
    uint8_t step,
    bool& changed
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.drumTrack == nullptr) return false;
    const int16_t slot = drumUi.drumTrack->advancedRootSlot(lane, step);
    if (slot < 0) return true;

    auto& pattern = seq::authoringPattern(sequencer_);
    const auto nodeId = seq::rootStepNodeId(static_cast<uint8_t>(slot));
    const auto* graph = seq::graphView(pattern);
    if (graph != nullptr) {
        const auto* node = graph->stepNode(nodeId);
        if (node == nullptr) return false;
        if (!seq::isDefaultSequencerGraphNodePayload(*node)) {
            if (!seq::resetStepNodePayload(pattern, nodeId)) return false;
            const auto compacted = seq::compactGraph(pattern);
            if (!compacted.ok) return false;
            changed = true;
        }
    }
    if (!drumUi.drumTrack->releaseAdvancedRootSlot(lane, step)) {
        return false;
    }
    changed = true;
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::resetDrumFocusedStep(bool deep) {
    if (!drumStepActionsAvailable()) return;
    auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = drumUi.selectedLane;
    const uint8_t step = drumUi.focusedStep;
    auto descriptor = drumStepActionDescriptor(
        drumUi,
        deep
            ? seq::SequencerHistoryActionKind::DrumAdvancedContent
            : seq::SequencerHistoryActionKind::DrumStepPropertyEdit,
        lane,
        step
    );
    if (!beginDrumStepActionHistory(descriptor)) return;

    bool changed = applyDrumStepClipboardEntry(
        drumUi.drumTrack->pattern,
        lane,
        step,
        defaultDrumStepEntry()
    );
    if (deep && !clearDrumAdvancedStep(lane, step, changed)) {
        (void)history_.abortCoalescedDrumEdit();
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            core::time_compat::millis()
        );
        return;
    }
    if (changed) drumUi.publishAuthoredMutation();
    (void)sealDrumStepActionHistory(changed, descriptor);
}

FLASHMEM void SequencerStructureEditWorkflow::copyDrumFocusedStep() {
    if (!drumStepActionsAvailable()) return;
    const auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = drumUi.selectedLane;
    const uint8_t step = drumUi.focusedStep;
    const auto& authored = drumUi.drumTrack->pattern.lanes[lane];

    core::state::SequencerStepsClipboard clipboard{};
    clipboard.valid = true;
    clipboard.rootContext = true;
    clipboard.drumContext = true;
    clipboard.count = 1U;
    clipboard.span = 1U;
    auto& entry = clipboard.entries[0];
    entry.valid = true;
    entry.enabled = drumUi.drumTrack->pattern.stepEnabled(lane, step);
    entry.velocity = authored.velocity[step];
    entry.gate = authored.gate[step];
    entry.nudge = authored.nudge[step];
    entry.probability = authored.probability[step];

    const int16_t slot = drumUi.drumTrack->advancedRootSlot(lane, step);
    const oc::note::sequencer::StepSequencerGraph* graph = nullptr;
    if (slot >= 0) {
        entry.sourceNodeId = seq::rootStepNodeId(static_cast<uint8_t>(slot));
        graph = seq::graphView(seq::authoringPattern(sequencer_));
        if (graph == nullptr ||
            !seq::inspectSequencerGraphPayload(
                 *graph,
                 entry.sourceNodeId,
                 0U
             ).ok()) {
            return;
        }
    }
    (void)structure_clipboard_.storeSequencerSteps(clipboard, graph);
}

FLASHMEM void SequencerStructureEditWorkflow::pasteDrumFocusedStep() {
    if (!canPasteDrumFocusedStep()) return;
    auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = drumUi.selectedLane;
    const uint8_t step = drumUi.focusedStep;
    const auto& entry = structure_clipboard_.sequencerSteps.entries[0];
    auto descriptor = drumStepActionDescriptor(
        drumUi,
        entry.sourceNodeId ==
                oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID &&
                drumUi.drumTrack->advancedRootSlot(lane, step) < 0
            ? seq::SequencerHistoryActionKind::DrumStepPropertyEdit
            : seq::SequencerHistoryActionKind::DrumAdvancedContent,
        lane,
        step
    );
    if (!beginDrumStepActionHistory(descriptor)) return;

    bool changed = false;
    if (!clearDrumAdvancedStep(lane, step, changed)) {
        (void)history_.abortCoalescedDrumEdit();
        return;
    }
    changed = applyDrumStepClipboardEntry(
        drumUi.drumTrack->pattern,
        lane,
        step,
        entry
    ) || changed;

    if (entry.sourceNodeId !=
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID) {
        bool mappingChanged = false;
        const int16_t slot = seq::ensureDrumAdvancedRootSlot(
            *drumUi.drumTrack,
            seq::authoringPattern(sequencer_),
            lane,
            step,
            mappingChanged
        );
        if (slot < 0 || !structure_clipboard_.sequencerGraph ||
            !seq::copyStepNodePayloadFromGraph(
                seq::authoringPattern(sequencer_),
                seq::rootStepNodeId(static_cast<uint8_t>(slot)),
                *structure_clipboard_.sequencerGraph,
                entry.sourceNodeId
            )) {
            (void)history_.abortCoalescedDrumEdit();
            sequencer_.historyFeedback.showRejection(
                seq::SequencerHistoryRejectionReason::HistoryUnavailable,
                core::time_compat::millis()
            );
            return;
        }
        changed = true;
    }

    if (changed) drumUi.publishAuthoredMutation();
    (void)sealDrumStepActionHistory(changed, descriptor);
}

FLASHMEM SequencerPreparedTrackStructureResult
SequencerStructureEditWorkflow::createPreviewedTrackStructure(
    core::state::sequencer::SequencerTrackKind kind,
    core::state::sequencer::DrumKitPreset drumPreset
) {
    using Status = SequencerPreparedTrackStructureStatus;
    if (track_activations_ == nullptr) {
        return {Status::HistoryUnavailable, {}};
    }
    return executeSequencerCreateTrackStructure(
        {
            tracks_,
            sequencer_,
            navigation_focus_,
            track_ui_,
            structure_clipboard_,
            macro_pages_,
            *track_activations_,
            shared_tracks_,
            history_,
        },
        kind,
        drumPreset
    );
}

FLASHMEM bool SequencerStructureEditWorkflow::canRemoveCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return false;
        return structure_slots::countEnabled(
                   currentTrackEnabledMask(),
                   core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) > 1U;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        if (sequencer_.drumSequencer.active()) {
            return drumStepActionsAvailable();
        }
        return sequencer_.focusedStep.get() <
               core::state::sequencer::activeContentLength(sequencer_);
    }
    return sequencer_.activePageCount() > 1U;
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return buildTrackPastePlan().canCommit();
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        if (sequencer_.drumSequencer.active()) {
            return canPasteDrumFocusedStep();
        }
        return canPasteFocusedStep();
    }
    return structure_clipboard_.hasSequencerPage();
}

FLASHMEM void SequencerStructureEditWorkflow::beginHoldAction(
    core::state::StructureHoldAction action) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (action == core::state::StructureHoldAction::PASTE) {
            beginTrackPasteAction(core::time_compat::millis());
            return;
        }
        if (action != core::state::StructureHoldAction::REMOVE ||
            track_ui_.selection.active.get()) {
            return;
        }
        // The physical hold owns an immutable local target. Shared preview
        // state is presentation only: history restore and other global paths
        // may legitimately rewrite it before the long-press callback fires.
        track_hold_intent_ = TrackHoldIntent::CurrentRemove;
        track_hold_target_ = currentActiveTrack();
        track_selection_hold_token_ = {};
        track_ui_.syncPreviewTrack(track_hold_target_);
        track_ui_.hold.begin(action, core::time_compat::millis());
        track_hold_acquisition_id_ = track_ui_.hold.acquisitionId();
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void SequencerStructureEditWorkflow::beginSelectionHoldAction(
    core::state::StructureHoldAction action
) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (action != core::state::StructureHoldAction::REMOVE ||
            !track_ui_.selection.active.get() ||
            track_ui_.selection.scope.get() !=
                core::state::StructureSelectionScope::TRACK ||
            track_ui_.previewAddSlot.get()) {
            return;
        }
        track_hold_intent_ = TrackHoldIntent::SelectionRemove;
        track_hold_target_ = currentActiveTrack();
        track_selection_hold_token_ = {
            .clipboardRevision = track_ui_.selection.clipboardRevision.get(),
            .selectedMask = track_ui_.selection.selectedMask.get(),
            .enabledMask = currentTrackEnabledMask(),
            .destinationMask = track_ui_.selection.destinationMask.get(),
            .overwriteMask = track_ui_.selection.overwriteMask.get(),
            .cursor = track_ui_.selection.cursorIndex.get(),
            .previewTrack = track_ui_.previewTrackIndex.get(),
            .flags = packTrackSelectionHoldFlags(
                track_ui_.selection.scope.get(),
                track_ui_.selection.placing.get(),
                track_ui_.selection.pasteBlocked.get(),
                track_ui_.previewAddSlot.get()
            ),
        };
        track_ui_.hold.begin(action, core::time_compat::millis());
        track_hold_acquisition_id_ = track_ui_.hold.acquisitionId();
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void SequencerStructureEditWorkflow::clearHoldAction() {
    clearTrackRemoveHoldIntent();
    sequencer_.structureUi.pageHold.clear();
}

FLASHMEM void SequencerStructureEditWorkflow::invalidateTrackRemoveHoldIntent() {
    track_hold_intent_ = TrackHoldIntent::None;
    track_hold_target_ =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    track_hold_acquisition_id_ = 0U;
    track_selection_hold_token_ = {};
}

FLASHMEM void SequencerStructureEditWorkflow::clearTrackRemoveHoldIntent() {
    track_ui_.hold.clear();
    invalidateTrackRemoveHoldIntent();
}

FLASHMEM bool
SequencerStructureEditWorkflow::trackRemoveHoldOwnsSharedState() const {
    return trackRemoveHoldPending() &&
           track_ui_.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE &&
           track_ui_.hold.acquisitionId() == track_hold_acquisition_id_;
}

FLASHMEM void
SequencerStructureEditWorkflow::settleConsumedBottomLeftRelease() {
    if (!trackRemoveHoldPending()) {
        clearHoldAction();
        return;
    }
    if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
    invalidateTrackRemoveHoldIntent();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::trackPasteTarget() const {
    if (track_ui_.selection.placementActive()) { return track_ui_.selection.cursorIndex.get(); }
    return sequencerStructureTrackTarget(track_ui_, currentActiveTrack());
}

FLASHMEM core::state::ClipboardTransferPlan SequencerStructureEditWorkflow::buildTrackPastePlan()
    const {
    return core::state::buildSequencerTrackClipboardTransferPlan(
        structure_clipboard_, tracks_, project_tracks_, trackPasteTarget(),
        track_activations_ != nullptr ? track_activations_->pendingTrackMask() : 0);
}

FLASHMEM void SequencerStructureEditWorkflow::setTrackPasteFeedback(
    contextual::OperationFeedbackStatus status, contextual::ContextActionReason reason,
    contextual::OperationFeedbackExpiryPolicy expiry, uint32_t nowMs, uint32_t durationMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    contextual::setOperationFeedback(paste.feedback, contextual::ContextActionId::PASTE,
                                     {
                                         .kind = contextual::ContextEntityKind::TRACK,
                                         .track = paste.plan.entries[0].sourceTrack,
                                         .item = paste.plan.sourceMask,
                                     },
                                     {
                                         .kind = contextual::ContextEntityKind::TRACK,
                                         .track = paste.plan.entries[0].targetTrack,
                                         .item = paste.plan.targetMask,
                                     },
                                     status, reason, expiry, nowMs, durationMs);
}

FLASHMEM bool SequencerStructureEditWorkflow::beginTrackPasteAction(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    const auto plan = buildTrackPastePlan();
    if (!plan.canCommit()) return false;

    uint32_t nextGeneration = paste.interactionGeneration + 1U;
    if (nextGeneration == 0) ++nextGeneration;
    paste.guard = {};
    paste.feedback = {};
    paste.plan = plan;
    paste.clipboardKind = structure_clipboard_.kind.get();
    paste.clipboardRevision = structure_clipboard_.revision.get();
    paste.interactionGeneration = nextGeneration;
    paste.operationGeneration = 0;
    paste.activationGeneration = 0;
    paste.detailVisible = false;
    paste.buttonOwned = true;
    paste.commitConsumed = false;
    if (!contextual::beginGuardedActionPress(
            paste.guard, nowMs,
            static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS))) {
        paste.buttonOwned = false;
        return false;
    }
    setTrackPasteFeedback(contextual::OperationFeedbackStatus::PRESSED,
                          core::state::sequencer::contextualReasonForTrackTransfer(plan.reason),
                          contextual::OperationFeedbackExpiryPolicy::MANUAL, nowMs);
    paste.bump();
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::refreshTrackPastePreview(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (paste.feedback.status == contextual::OperationFeedbackStatus::QUEUED ||
        paste.feedback.status == contextual::OperationFeedbackStatus::APPLIED) {
        return;
    }

    // A terminal transient is user feedback, not a new preflight. Preserve it
    // until its own expiry policy clears it; the following update can then
    // restore PREVIEW from the live clipboard/target pair.
    if (!paste.buttonOwned && paste.feedback.active &&
        (paste.feedback.status == contextual::OperationFeedbackStatus::CANCELLED ||
         paste.feedback.status == contextual::OperationFeedbackStatus::BLOCKED ||
         paste.feedback.status == contextual::OperationFeedbackStatus::FAILED ||
         paste.feedback.status == contextual::OperationFeedbackStatus::CONFLICT)) {
        return;
    }

    if (paste.buttonOwned || paste.gestureActive()) {
        const auto live = core::state::buildSequencerTrackClipboardTransferPlan(
            structure_clipboard_, tracks_, project_tracks_, paste.plan.entries[0].targetTrack,
            track_activations_ != nullptr ? track_activations_->pendingTrackMask() : 0);
        if (paste.clipboardKind != structure_clipboard_.kind.get() ||
            paste.clipboardRevision != structure_clipboard_.revision.get() || !live.canCommit() ||
            !core::state::sameSequencerTrackClipboardTransferIdentity(paste.plan, live)) {
            if (contextual::cancelGuardedAction(paste.guard)) {
                paste.detailVisible = false;
                setTrackPasteFeedback(contextual::OperationFeedbackStatus::BLOCKED,
                                      contextual::ContextActionReason::STALE_TARGET,
                                      contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                                      nowMs, TRACK_PASTE_CANCELLED_MS);
                paste.bump();
            }
            return;
        }
        if (!core::state::sameSequencerTrackClipboardTransferPlan(paste.plan, live)) {
            paste.plan = live;
            paste.bump();
        }
        return;
    }

    const bool trackContext =
        navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK;
    const auto live = trackContext ? buildTrackPastePlan() : core::state::ClipboardTransferPlan{};
    if (!trackContext || !live.canCommit()) {
        const bool changed =
            paste.plan.hasEntries() || paste.detailVisible || paste.feedback.active;
        paste.plan = {};
        paste.clipboardKind = core::state::StructureClipboardKind::NONE;
        paste.clipboardRevision = 0;
        paste.detailVisible = false;
        contextual::clearOperationFeedback(paste.feedback);
        if (changed) paste.bump();
        return;
    }

    const bool planChanged =
        !core::state::sameSequencerTrackClipboardTransferPlan(paste.plan, live);
    const bool feedbackChanged =
        paste.feedback.status != contextual::OperationFeedbackStatus::PREVIEW;
    if (!planChanged && !feedbackChanged) return;
    paste.plan = live;
    paste.clipboardKind = structure_clipboard_.kind.get();
    paste.clipboardRevision = structure_clipboard_.revision.get();
    setTrackPasteFeedback(contextual::OperationFeedbackStatus::PREVIEW,
                          core::state::sequencer::contextualReasonForTrackTransfer(live.reason),
                          contextual::OperationFeedbackExpiryPolicy::MANUAL, nowMs);
    paste.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::updateTrackPasteActivation(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (track_activations_ == nullptr || paste.activationGeneration == 0 ||
        paste.feedback.status != contextual::OperationFeedbackStatus::QUEUED ||
        !paste.plan.hasEntries()) {
        return;
    }

    const auto telemetry = track_activations_->telemetry(paste.plan.entries[0].targetTrack);
    if (telemetry.generation != paste.activationGeneration ||
        telemetry.origin != core::state::sequencer::SequencerTrackActivationOrigin::TRACK_PASTE) {
        return;
    }

    if (telemetry.status == core::state::sequencer::SequencerTrackActivationStatus::CANCELLED) {
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::CANCELLED,
                              contextual::ContextActionReason::FAILED,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
        paste.bump();
    } else if (telemetry.status ==
               core::state::sequencer::SequencerTrackActivationStatus::APPLIED) {
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::APPLIED,
                              contextual::ContextActionReason::NONE,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_APPLIED_MS);
        paste.bump();
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::commitTrackPaste(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (paste.commitConsumed || !paste.plan.canCommit()) return false;
    paste.commitConsumed = true;

    const auto result = executeSequencerTrackTransfer(
        tracks_, project_tracks_, sequencer_, structure_clipboard_, shared_tracks_, history_,
        paste.plan.entries[0].targetTrack, 0, track_activations_,
        status_bar_ != nullptr && status_bar_->playing.get(), &macro_pages_);
    paste.plan = result.plan;
    paste.activationGeneration = result.activationGeneration;
    paste.operationGeneration =
        result.operationId != 0 ? result.operationId : paste.interactionGeneration;

    if (!result.applied()) {
        auto reason = core::state::sequencer::contextualReasonForTrackTransfer(result.plan.reason);
        if (reason == contextual::ContextActionReason::NONE) {
            switch (result.status) {
                case SequencerTrackTransferStatus::STALE:
                    reason = contextual::ContextActionReason::STALE_TARGET;
                    break;
                case SequencerTrackTransferStatus::NO_CHANGE:
                    reason = contextual::ContextActionReason::NO_ACTION;
                    break;
                case SequencerTrackTransferStatus::HISTORY_UNAVAILABLE:
                    reason = contextual::ContextActionReason::HISTORY_UNAVAILABLE;
                    break;
                case SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE:
                    reason = contextual::ContextActionReason::ALLOCATION_UNAVAILABLE;
                    break;
                default: reason = contextual::ContextActionReason::FAILED; break;
            }
        }
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::BLOCKED, reason,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
        paste.bump();
        return false;
    }

    setTrackPasteFeedback(result.activationGeneration != 0
                              ? contextual::OperationFeedbackStatus::QUEUED
                              : contextual::OperationFeedbackStatus::APPLIED,
                          contextual::ContextActionReason::NONE,
                          result.activationGeneration != 0
                              ? contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED
                              : contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                          nowMs, result.activationGeneration != 0 ? 0 : TRACK_PASTE_APPLIED_MS);
    syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
    paste.bump();
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::update(uint32_t nowMs) {
    refreshStructureSelectionPastePreview();
    auto& paste = sequencer_.structureUi.trackPaste;
    updateTrackPasteActivation(nowMs);

    if (contextual::updateOperationFeedback(paste.feedback, nowMs)) { paste.bump(); }

    if (!paste.buttonOwned) {
        refreshTrackPastePreview(nowMs);
        return;
    }

    refreshTrackPastePreview(nowMs);
    if (paste.guard.phase == contextual::GuardedActionPhase::PRESSED &&
        (nowMs - paste.guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        // Guard progress is anchored to the physical press so COMMITTED occurs
        // at the absolute long-press threshold, not one threshold later.
        if (contextual::armGuardedAction(paste.guard, paste.guard.pressedAtMs)) {
            setTrackPasteFeedback(
                contextual::OperationFeedbackStatus::ARMED,
                core::state::sequencer::contextualReasonForTrackTransfer(paste.plan.reason),
                contextual::OperationFeedbackExpiryPolicy::MANUAL, nowMs);
            paste.bump();
        }
    }
    if (paste.guard.phase == contextual::GuardedActionPhase::ARMED &&
        contextual::updateGuardedAction(paste.guard, nowMs)) {
        if (paste.guard.phase == contextual::GuardedActionPhase::COMMITTED) {
            commitTrackPaste(nowMs);
        } else {
            paste.bump();
        }
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteStructureSelection() const {
    if (sequencer_.drumSequencer.laneSelection.placementActive()) {
        return canPasteDrumLaneSelection();
    }
    if (track_ui_.selection.placementActive()) { return buildTrackPastePlan().canCommit(); }
    const auto& pageSelection = sequencer_.structureUi.pageSelection;
    if (!pageSelection.placementActive()) return false;
    return buildPageSelectionPastePlan(sequencer_, structure_clipboard_,
                                       pageSelection.cursorIndex.get())
        .canCommit();
}

FLASHMEM bool
SequencerStructureEditWorkflow::canMoveDrumLaneSelection() const {
    const auto& drumUi = sequencer_.drumSequencer;
    const auto& selection = drumUi.laneSelection;
    if (!selection.active || selection.placing || selection.moving ||
        drumUi.drumTrack == nullptr) {
        return false;
    }
    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        seq::DRUM_MAX_LANES
    );
    const uint16_t selectedMask = static_cast<uint16_t>(
        selection.selectedMask & drumLaneMask(laneCount)
    );
    return countDrumLanes(selectedMask) == 1U;
}

FLASHMEM void SequencerStructureEditWorkflow::beginDrumLaneMove() {
    if (!canMoveDrumLaneSelection()) return;
    auto& drumUi = sequencer_.drumSequencer;
    auto& selection = drumUi.laneSelection;
    const uint8_t source = firstDrumLane(selection.selectedMask);
    if (source >= seq::DRUM_MAX_LANES) return;

    selection.moving = true;
    selection.cursorLane = source;
    selection.destinationMask = static_cast<uint16_t>(1U << source);
    selection.overwriteMask = 0U;
    selection.pasteBlocked = false;
    selection.clipboardCount = 0U;
    selection.clipboardRevision = 0U;
    drumUi.selectedLane = source;
    drumUi.laneAddSlotSelected = false;
    drumUi.ensureSelectedLaneVisible();
    drumUi.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::applyDrumLaneMove() {
    auto& drumUi = sequencer_.drumSequencer;
    auto& selection = drumUi.laneSelection;
    if (!selection.moveActive() || drumUi.drumTrack == nullptr) return;

    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        seq::DRUM_MAX_LANES
    );
    const uint16_t sourceMask = static_cast<uint16_t>(
        selection.selectedMask & drumLaneMask(laneCount)
    );
    const uint8_t source = firstDrumLane(sourceMask);
    const uint8_t target = selection.cursorLane;
    if (countDrumLanes(sourceMask) != 1U || source >= laneCount ||
        target >= laneCount) {
        return;
    }

    if (source != target) {
        const auto descriptor = drumStepActionDescriptor(
            drumUi,
            seq::SequencerHistoryActionKind::DrumLaneStructure,
            source,
            target
        );
        if (!beginDrumStepActionHistory(descriptor)) return;
        const bool changed = drumUi.drumTrack->moveLane(source, target);
        if (changed) drumUi.publishAuthoredMutation();
        if (!sealDrumStepActionHistory(changed, descriptor)) return;
    }

    selection.moving = false;
    selection.pasteBlocked = false;
    selection.selectedMask = static_cast<uint16_t>(1U << target);
    selection.destinationMask = 0U;
    selection.overwriteMask = 0U;
    drumUi.selectedLane = target;
    drumUi.laneAddSlotSelected = false;
    drumUi.ensureSelectedLaneVisible();
    drumUi.clampOverviewPage();
    const uint8_t laneLength =
        drumUi.drumTrack->pattern.effectiveLength(target);
    const uint8_t pageStart = static_cast<uint8_t>(
        drumUi.page * seq::DrumSequencerState::STEPS_PER_PAGE
    );
    drumUi.focusedStep = pageStart < laneLength
        ? std::clamp<uint8_t>(
              drumUi.focusedStep,
              pageStart,
              std::min<uint8_t>(
                  static_cast<uint8_t>(
                      pageStart + seq::DrumSequencerState::STEPS_PER_PAGE - 1U
                  ),
                  static_cast<uint8_t>(laneLength - 1U)
              )
          )
        : static_cast<uint8_t>(laneLength - 1U);
    drumUi.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::refreshStructureSelectionPastePreview() {
    auto refresh = [](core::state::StructureSelectionState& selection, uint16_t destinationMask,
                      uint16_t overwriteMask, bool blocked, uint32_t clipboardRevision) {
        selection.destinationMask.set(destinationMask);
        selection.overwriteMask.set(overwriteMask);
        selection.pasteBlocked.set(blocked);
        selection.clipboardRevision.set(clipboardRevision);
    };

    auto& trackSelection = track_ui_.selection;
    if (trackSelection.placementActive()) {
        const auto plan = buildTrackPastePlan();
        refresh(trackSelection, plan.targetMask, plan.overwriteMask, !plan.canCommit(),
                structure_clipboard_.revision.get());
    } else {
        refresh(trackSelection, 0U, 0U, false, 0U);
    }

    auto& pageSelection = sequencer_.structureUi.pageSelection;
    if (pageSelection.placementActive()) {
        const auto plan = buildPageSelectionPastePlan(sequencer_, structure_clipboard_,
                                                      pageSelection.cursorIndex.get());
        refresh(pageSelection, plan.destinationMask, plan.overwriteMask, !plan.canCommit(),
                structure_clipboard_.revision.get());
    } else {
        refresh(pageSelection, 0U, 0U, false, 0U);
    }

    auto& drumUi = sequencer_.drumSequencer;
    auto& drumSelection = drumUi.laneSelection;
    const uint16_t previousDestination = drumSelection.destinationMask;
    const uint16_t previousOverwrite = drumSelection.overwriteMask;
    const bool previousBlocked = drumSelection.pasteBlocked;
    if (drumSelection.placementActive() && drumUi.drumTrack != nullptr) {
        const uint8_t laneCount = std::min<uint8_t>(
            drumUi.drumTrack->kit.laneCount,
            seq::DRUM_MAX_LANES
        );
        const bool compatible =
            drumSelection.clipboardRevision ==
                structure_clipboard_.revision.get() &&
            structure_clipboard_.hasSequencerDrumLaneSelection() &&
            drumSelection.clipboardCount ==
                structure_clipboard_.sequencerDrumLaneSelectionCount;
        drumSelection.destinationMask = compatible
            ? drumLaneDestinationMask(
                  drumSelection.cursorLane,
                  drumSelection.clipboardCount,
                  laneCount
              )
            : 0U;
        drumSelection.overwriteMask = drumSelection.destinationMask;
        drumSelection.pasteBlocked = !compatible ||
            drumSelection.cursorLane + drumSelection.clipboardCount >
                laneCount;
    } else if (!drumSelection.active ||
               (!drumSelection.placing && !drumSelection.moving)) {
        drumSelection.destinationMask = 0U;
        drumSelection.overwriteMask = 0U;
        drumSelection.pasteBlocked = false;
    }
    if (previousDestination != drumSelection.destinationMask ||
        previousOverwrite != drumSelection.overwriteMask ||
        previousBlocked != drumSelection.pasteBlocked) {
        drumUi.bump();
    }
}

FLASHMEM void SequencerStructureEditWorkflow::copyStructureSelection() {
    if (sequencer_.drumSequencer.laneSelection.active) {
        copyDrumLaneSelection();
        return;
    }
    if (track_ui_.selection.active.get()) {
        auto& selection = track_ui_.selection;
        if (selection.placing.get()) return;
        auto clipboard = captureTrackSelectionClipboard(tracks_, sequencer_, macro_pages_,
                                                        selection.selectedMask.get());
        if (!clipboard ||
            !structure_clipboard_.storeSequencerTrackSelection(std::move(clipboard))) {
            return;
        }
        selection.placing.set(true);
        selection.clipboardRevision.set(structure_clipboard_.revision.get());
        refreshStructureSelectionPastePreview();
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get() || selection.placing.get()) return;
    core::state::SequencerPageSelectionClipboard clipboard;
    if (!capturePageSelectionClipboard(sequencer_, selection.selectedMask.get(), clipboard) ||
        !structure_clipboard_.storeSequencerPageSelection(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
    selection.placing.set(true);
    selection.clipboardRevision.set(structure_clipboard_.revision.get());
    refreshStructureSelectionPastePreview();
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStructureSelection() {
    if (sequencer_.drumSequencer.laneSelection.active) {
        pasteDrumLaneSelection();
        return;
    }
    const auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.placementActive()) return;
    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PageSelectionPaste;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    const uint16_t settlement = pastePageSelectionAfterBoundary(transaction);
    switch (preparedStructureSettlementOutcome(settlement)) {
        case PreparedStructureSettlement::Committed:
            sequencer_.structureUi.pageHold.clear();
            sequencer_.structureUi.syncPreviewPage(sequencer_.page.get());
            refreshStructureSelectionPastePreview();
            return;
        case PreparedStructureSettlement::NoChange: {
            const uint8_t finalFocus =
                preparedStructureSettlementFocus(settlement);
            sequencer_.page.set(sequencer_.pageForStep(finalFocus));
            sequencer_.focusedStep.set(finalFocus);
            sequencer_.structureUi.pageHold.clear();
            sequencer_.structureUi.syncPreviewPage(sequencer_.page.get());
            refreshStructureSelectionPastePreview();
            return;
        }
        case PreparedStructureSettlement::Failed:
        default:
            return;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM uint16_t SequencerStructureEditWorkflow::pastePageSelectionAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageSelectionPasteMutationPlan(
        sequencer_, structure_clipboard_,
        makeSequencerPreparedPageStructureTarget(
            currentActiveTrack(),
            sequencer_.structureUi.pageSelection.cursorIndex.get()),
        plan)) {
        case Preflight::Rejected:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
        case Preflight::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Preflight::Ready:
            break;
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Committed);
        case Result::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Result::Failed:
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }
}

FLASHMEM contextual::GuardedActionRelease SequencerStructureEditWorkflow::releaseTrackPasteAction(
    uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    // Copy remains the unconditional tap action, even when no compatible
    // clipboard exists and therefore no guarded paste press was acquired.
    if (!paste.buttonOwned) return contextual::GuardedActionRelease::TAP;

    update(nowMs);
    const auto phase = paste.guard.phase;
    auto release = contextual::releaseGuardedAction(paste.guard, nowMs);
    if (phase == contextual::GuardedActionPhase::CANCELLED) {
        release = contextual::GuardedActionRelease::CANCELLED;
    }
    paste.buttonOwned = false;

    if (release == contextual::GuardedActionRelease::TAP) {
        paste.detailVisible = false;
        paste.plan = {};
        contextual::clearOperationFeedback(paste.feedback);
    } else if (release == contextual::GuardedActionRelease::CANCELLED) {
        paste.detailVisible = false;
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::CANCELLED,
                              contextual::ContextActionReason::NONE,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
    }
    paste.bump();
    return release;
}

FLASHMEM bool SequencerStructureEditWorkflow::cancelTrackPasteAction(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    bool changed = false;
    if (paste.detailVisible) {
        paste.detailVisible = false;
        changed = true;
    }
    if (contextual::cancelGuardedAction(paste.guard)) {
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::CANCELLED,
                              contextual::ContextActionReason::NONE,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
        changed = true;
    }
    if (changed) paste.bump();
    return changed;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPasteNavigationBlocked() const {
    const auto& paste = sequencer_.structureUi.trackPaste;
    return paste.buttonOwned || paste.gestureActive() || paste.detailVisible;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackRemoveNavigationBlocked() const {
    return trackRemoveHoldPending();
}

FLASHMEM bool SequencerStructureEditWorkflow::trackRemoveHoldPending() const {
    return track_hold_intent_ != TrackHoldIntent::None;
}

FLASHMEM bool SequencerStructureEditWorkflow::currentTrackRemoveHoldPending() const {
    return track_hold_intent_ == TrackHoldIntent::CurrentRemove;
}

FLASHMEM bool
SequencerStructureEditWorkflow::currentTrackRemoveHoldStillMatches() const {
    return currentTrackRemoveHoldPending() &&
           trackRemoveHoldOwnsSharedState() &&
           currentTrackRemoveIntentMatches(track_hold_target_);
}

FLASHMEM bool SequencerStructureEditWorkflow::currentTrackRemoveIntentMatches(
    uint8_t targetTrack
) const {
    return navigation_focus_.get() ==
               core::state::StructureNavigationFocus::TRACK &&
           !track_ui_.selection.active.get() &&
           !track_ui_.previewAddSlot.get() &&
           track_ui_.previewTrackIndex.get() == targetTrack &&
           currentActiveTrack() == targetTrack;
}

FLASHMEM bool SequencerStructureEditWorkflow::selectionTrackRemoveHoldPending() const {
    return track_hold_intent_ == TrackHoldIntent::SelectionRemove;
}

FLASHMEM bool
SequencerStructureEditWorkflow::selectionTrackRemoveHoldStillMatches() const {
    return selectionTrackRemoveHoldPending() &&
           trackRemoveHoldOwnsSharedState() &&
           selectionTrackRemoveIntentMatches(
               track_selection_hold_token_,
               track_hold_target_
           );
}

FLASHMEM bool SequencerStructureEditWorkflow::selectionTrackRemoveIntentMatches(
    const TrackSelectionHoldToken& token,
    uint8_t targetTrack
) const {
    return navigation_focus_.get() ==
               core::state::StructureNavigationFocus::TRACK &&
           track_ui_.selection.active.get() &&
           track_ui_.selection.clipboardRevision.get() ==
               token.clipboardRevision &&
           track_ui_.selection.selectedMask.get() ==
               token.selectedMask &&
           track_ui_.selection.destinationMask.get() ==
               token.destinationMask &&
           track_ui_.selection.overwriteMask.get() ==
               token.overwriteMask &&
           track_ui_.selection.cursorIndex.get() ==
               token.cursor &&
           track_ui_.previewTrackIndex.get() == token.previewTrack &&
           packTrackSelectionHoldFlags(
               track_ui_.selection.scope.get(),
               track_ui_.selection.placing.get(),
               track_ui_.selection.pasteBlocked.get(),
               track_ui_.previewAddSlot.get()
           ) == token.flags &&
           currentTrackEnabledMask() ==
               token.enabledMask &&
           currentActiveTrack() == targetTrack;
}

FLASHMEM void
SequencerStructureEditWorkflow::settleRejectedSelectionTrackRemoveLongPress() {
    if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPastePlanInspectable() const {
    const auto& paste = sequencer_.structureUi.trackPaste;
    return paste.inspectable() && paste.plan.canCommit() && paste.feedback.active;
}

FLASHMEM void SequencerStructureEditWorkflow::toggleTrackPasteDetails() {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (!trackPastePlanInspectable()) return;
    paste.detailVisible = !paste.detailVisible;
    paste.bump();
}

FLASHMEM void
SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress() {
    if (!currentTrackRemoveHoldStillMatches()) {
        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
        invalidateTrackRemoveHoldIntent();
        return;
    }

    const uint8_t targetTrack = track_hold_target_;
    clearTrackRemoveHoldIntent();
    if (history_.commitCoalescedPatternEditOutcome() ==
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    if (track_ui_.hold.active() ||
        !currentTrackRemoveIntentMatches(targetTrack)) {
        return;
    }
    (void)toggleSequencerStructureTrackMute(
        project_tracks_,
        project_track_domain_,
        targetTrack
    );
}

FLASHMEM void
SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress() {
    if (!selectionTrackRemoveHoldStillMatches()) {
        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
        invalidateTrackRemoveHoldIntent();
        return;
    }

    const auto token = track_selection_hold_token_;
    const uint8_t targetTrack = track_hold_target_;
    clearTrackRemoveHoldIntent();
    if (history_.commitCoalescedPatternEditOutcome() ==
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    if (track_ui_.hold.active() ||
        !selectionTrackRemoveIntentMatches(token, targetTrack)) {
        return;
    }
    applySelectionBottomLeftTap();
}

FLASHMEM void
SequencerStructureEditWorkflow::applyLatchedTrackSelectionLongPress() {
    if (!selectionTrackRemoveHoldStillMatches() ||
        !selectionHoldActionAvailable()) {
        settleRejectedSelectionTrackRemoveLongPress();
        return;
    }

    const auto token = track_selection_hold_token_;
    const uint8_t targetTrack = track_hold_target_;
    if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
    if (track_ui_.hold.active() ||
        !selectionTrackRemoveIntentMatches(token, targetTrack)) {
        return;
    }
    applySelectionBottomLeftHold();
}

FLASHMEM void SequencerStructureEditWorkflow::applyCurrentStructureShortPress() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const uint8_t activeTrack = currentActiveTrack();
        (void)toggleSequencerStructureTrackMute(project_tracks_, project_track_domain_,
                                                activeTrack);
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        if (sequencer_.drumSequencer.active()) {
            resetDrumFocusedStep(false);
            return;
        }
        resetFocusedStep(StepResetDepth::Shallow);
        return;
    }

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PageClear;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    clearCurrentPageAfterBoundary(transaction);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM void SequencerStructureEditWorkflow::clearCurrentPageAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageClearMutationPlan(
        sequencer_, currentActiveTrack(), sequencer_.visiblePage(), plan)) {
        case Preflight::Rejected:
            return;
        case Preflight::NoChange:
            sequencer_.page.set(sequencer_.pageForStep(plan.finalFocus));
            sequencer_.focusedStep.set(plan.finalFocus);
            sequencer_.structureUi.pageHold.clear();
            return;
        case Preflight::Ready:
            break;
        default:
            return;
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            sequencer_.structureUi.pageHold.clear();
            return;
        case Result::NoChange:
            sequencer_.page.set(sequencer_.pageForStep(plan.finalFocus));
            sequencer_.focusedStep.set(plan.finalFocus);
            sequencer_.structureUi.pageHold.clear();
            return;
        case Result::Failed:
        default:
            return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::applyCurrentStructureLongPress() {
    if (trackRemoveHoldPending()) {
        const bool holdStillMatches = currentTrackRemoveHoldStillMatches();
        const uint8_t latchedTarget = track_hold_target_;
        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
        if (!holdStillMatches || track_activations_ == nullptr) {
            return;
        }
        const auto result = executeSequencerRemoveCurrentTrackStructure({
            tracks_,
            sequencer_,
            navigation_focus_,
            track_ui_,
            structure_clipboard_,
            macro_pages_,
            *track_activations_,
            shared_tracks_,
            history_,
        }, latchedTarget);
        if (!result.settled()) return;
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        track_ui_.hold.clear();
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        if (sequencer_.drumSequencer.active()) {
            resetDrumFocusedStep(true);
            return;
        }
        resetFocusedStep(StepResetDepth::Deep);
        return;
    }

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PageDelete;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    deleteCurrentPageAfterBoundary(transaction);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM void SequencerStructureEditWorkflow::deleteCurrentPageAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageDeleteMutationPlan(
        sequencer_, currentActiveTrack(), sequencer_.visiblePage(), plan)) {
        case Preflight::Rejected:
        case Preflight::NoChange:
            return;
        case Preflight::Ready:
            break;
        default:
            return;
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            sequencer_.structureUi.pageHold.clear();
            syncSequencerPagePreviewToVisible(sequencer_, false);
            return;
        case Result::NoChange:
        case Result::Failed:
        default:
            return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::copyCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        core::state::sequencer::SequencerPatternSnapshot snapshot;
        core::state::sequencer::captureSnapshot(sequencer_.pattern, snapshot);
        if (!structure_clipboard_.storeSequencerTrack(
                snapshot, core::state::sequencer::graphView(sequencer_.pattern),
                currentActiveTrack(),
                core::state::sequencer::sequencerCcLaneView(sequencer_.pattern),
                tracks_.isDrumTrack(currentActiveTrack())
                    ? &tracks_.drumTrack(currentActiveTrack())
                    : nullptr)) {
            return;
        }
        return;
    }

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        if (sequencer_.drumSequencer.active()) {
            copyDrumFocusedStep();
            return;
        }
        copyFocusedStep();
        return;
    }

    core::state::SequencerPageClipboard clipboard;
    const uint8_t page = sequencer_.visiblePage();
    if (!capturePageClipboard(sequencer_, page, clipboard)) return;
    if (!structure_clipboard_.storeSequencerPage(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        if (sequencer_.drumSequencer.active()) {
            pasteDrumFocusedStep();
            return;
        }
        pasteFocusedStep();
        return;
    }

    if (navigation_focus_.get() != core::state::StructureNavigationFocus::PAGE) return;

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PagePaste;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    const uint16_t settlement = pasteCurrentPageAfterBoundary(transaction);
    switch (preparedStructureSettlementOutcome(settlement)) {
        case PreparedStructureSettlement::Committed:
            sequencer_.structureUi.pageHold.clear();
            syncSequencerPagePreviewToVisible(sequencer_, false);
            return;
        case PreparedStructureSettlement::NoChange: {
            const uint8_t finalFocus =
                preparedStructureSettlementFocus(settlement);
            sequencer_.page.set(sequencer_.pageForStep(finalFocus));
            sequencer_.focusedStep.set(finalFocus);
            sequencer_.structureUi.pageHold.clear();
            syncSequencerPagePreviewToVisible(sequencer_, false);
            return;
        }
        case PreparedStructureSettlement::Failed:
        default:
            return;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM uint16_t SequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPagePasteMutationPlan(
        sequencer_, structure_clipboard_,
        makeSequencerPreparedPageStructureTarget(
            currentActiveTrack(),
            sequencer_.visiblePage()),
        plan)) {
        case Preflight::Rejected:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
        case Preflight::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Preflight::Ready:
            break;
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Committed);
        case Result::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Result::Failed:
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteFocusedStep() const {
    return structure_clipboard_.hasSequencerSteps() &&
           !structure_clipboard_.sequencerSteps.drumContext &&
           structure_clipboard_.sequencerSteps.rootContext ==
               core::state::sequencer::isRootContentView(sequencer_);
}

FLASHMEM void SequencerStructureEditWorkflow::copyFocusedStep() {
    const uint8_t step = sequencer_.focusedStep.get();

    core::state::SequencerStepsClipboard clipboard;
    if (!captureFocusedStepClipboard(sequencer_, tracks_, step, clipboard)) return;

    if (!structure_clipboard_.storeSequencerSteps(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteFocusedStep() {
    pasteStepClipboardAt(sequencer_.focusedStep.get(), false);
}

FLASHMEM void SequencerStructureEditWorkflow::copyStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get() || selection.placementActive()) return;

    core::state::SequencerStepsClipboard clipboard;
    if (!captureStepSelectionClipboard(sequencer_, tracks_, selection.selectedMask.get(),
                                       clipboard)) {
        return;
    }
    if (!structure_clipboard_.storeSequencerSteps(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
    selection.placing.set(true);
    selection.clipboardRevision.set(structure_clipboard_.revision.get());
    clearStepPastePreview();
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteStepSelection() const {
    const auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() != structure_clipboard_.revision.get() ||
        !structure_clipboard_.hasSequencerSteps() ||
        structure_clipboard_.sequencerSteps.drumContext ||
        structure_clipboard_.sequencerSteps.rootContext !=
            core::state::sequencer::isRootContentView(sequencer_)) {
        return false;
    }

    const auto plan = buildStructureStepPastePlan(sequencer_, structure_clipboard_.sequencerSteps,
                                                  structureStepPasteMode(project_navigation_),
                                                  selection.cursorStep.get());
    return !plan.blocked && plan.hasEntries();
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelectionShallow() {
    resetStepSelection(StepResetDepth::Shallow);
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelectionDeep() {
    resetStepSelection(StepResetDepth::Deep);
}

FLASHMEM void SequencerStructureEditWorkflow::beginStepPastePreview() {
    beginStructureStepPastePreview(sequencer_, structure_clipboard_, project_navigation_);
}

FLASHMEM void SequencerStructureEditWorkflow::clearStepPastePreview() {
    clearStructureStepPastePreview(sequencer_);
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepClipboardAt(uint8_t cursorStep,
                                                                   bool selectionPaste) {
    if (!structure_clipboard_.hasSequencerSteps()) return;
    if (selectionPaste) {
        const auto& selection = sequencer_.structureUi.stepSelection;
        if (!selection.placementActive() ||
            selection.cursorStep.get() != cursorStep) {
            return;
        }
    } else if (sequencer_.focusedStep.get() != cursorStep) {
        return;
    }

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::StepPaste;
    SequencerPreparedPageStructureTransaction transaction(
        sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    const uint16_t settlement = pasteStepClipboardAfterBoundary(transaction);
    const auto outcome = preparedStructureSettlementOutcome(settlement);
    if (outcome == PreparedStructureSettlement::Failed) return;

    const uint8_t finalFocus =
        preparedStructureSettlementFocus(settlement);
    if (outcome == PreparedStructureSettlement::NoChange) {
        sequencer_.page.set(
            core::state::sequencer::activeContentPageForStep(finalFocus));
        sequencer_.focusedStep.set(finalFocus);
    }
    if (selectionPaste) {
        auto& selection = sequencer_.structureUi.stepSelection;
        selection.cursorStep.set(finalFocus);
        clearStepPastePreview();
    }
    navigation_focus_.set(core::state::StructureNavigationFocus::STEP);
    sequencer_.structureUi.pageHold.clear();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM uint16_t
SequencerStructureEditWorkflow::pasteStepClipboardAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerStepPasteMutationPlan(
        sequencer_,
        structure_clipboard_,
        makeSequencerPreparedStepPasteTarget(
            currentActiveTrack(),
            structureStepPasteMode(project_navigation_),
            sequencer_.structureUi.stepSelection.placementActive()
                ? sequencer_.structureUi.stepSelection.cursorStep.get()
                : sequencer_.focusedStep.get()),
        plan)) {
        case Preflight::Rejected:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
        case Preflight::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Preflight::Ready:
            break;
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Committed, plan.finalFocus);
        case Result::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Result::Failed:
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() != structure_clipboard_.revision.get()) {
        return;
    }
    pasteStepClipboardAt(selection.cursorStep.get(), true);
}

FLASHMEM void SequencerStructureEditWorkflow::syncPreviewToFocus(
    core::state::StructureNavigationFocus focus) {
    track_ui_.previewAddSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    syncSequencerPagePreviewToVisible(sequencer_,
                                      focus == core::state::StructureNavigationFocus::PAGE);
}

FLASHMEM void SequencerStructureEditWorkflow::resetFocusedStep(StepResetDepth depth) {
    const uint8_t step = sequencer_.focusedStep.get();
    if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::FocusedStepReset;
    SequencerPreparedPageStructureTransaction transaction(
        sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    if (resetFocusedStepAfterBoundary(transaction, depth) ==
        SequencerPreparedPageStructureResult::Committed) {
        sequencer_.structureUi.pageHold.clear();
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
SequencerStructureEditWorkflow::resetFocusedStepAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction,
    StepResetDepth depth
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerFocusedStepResetMutationPlan(
        sequencer_,
        makeSequencerPreparedFocusedStepResetTarget(
            currentActiveTrack(), sequencer_.focusedStep.get(), depth),
        plan)) {
        case Preflight::Rejected:
            return Result::Failed;
        case Preflight::NoChange:
            return Result::NoChange;
        case Preflight::Ready:
            break;
        default:
            return Result::Failed;
    }
    return executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan);
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelection(
    StepResetDepth depth
) {
    if (!sequencer_.structureUi.stepSelection.active.get()) return;

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::StepSelectionReset;
    SequencerPreparedPageStructureTransaction transaction(
        sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    if (resetStepSelectionAfterBoundary(transaction, depth) ==
        SequencerPreparedPageStructureResult::Committed) {
        sequencer_.structureUi.pageHold.clear();
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
SequencerStructureEditWorkflow::resetStepSelectionAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction,
    StepResetDepth depth
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerStepSelectionResetMutationPlan(
        sequencer_,
        sequencer_.structureUi.stepSelection.selectedMask.get(),
        makeSequencerPreparedStepSelectionResetTarget(
            currentActiveTrack(), depth),
        plan)) {
        case Preflight::Rejected:
            return Result::Failed;
        case Preflight::NoChange:
            return Result::NoChange;
        case Preflight::Ready:
            break;
        default:
            return Result::Failed;
    }
    return executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan);
}

FLASHMEM uint16_t SequencerStructureEditWorkflow::currentTrackEnabledMask() const {
    return shared_tracks_.enabledMask();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::currentActiveTrack() const {
    return shared_tracks_.activeTrack();
}

}  // namespace core::handler

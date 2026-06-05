#include "state/sequencer/SequencerHistory.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using GraphPtr = SequencerHistoryGraphPtr;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerStepNode;

FLASHMEM bool sameScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings lhs,
    oc::note::sequencer::StepSequencerScaleSettings rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

FLASHMEM bool sameVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges lhs,
    oc::note::sequencer::StepSequencerVariationRanges rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM bool sameFlatPatternSnapshot(
    const SequencerPatternSnapshot& lhs,
    const SequencerPatternSnapshot& rhs
) {
    return lhs.length == rhs.length &&
           lhs.stepsPerBeat == rhs.stepsPerBeat &&
           lhs.midiChannel == rhs.midiChannel &&
           lhs.enabledMask == rhs.enabledMask &&
           sameVariationRanges(lhs.variationRanges, rhs.variationRanges) &&
           lhs.scalePolicy == rhs.scalePolicy &&
           sameScaleSettings(lhs.scaleOverride, rhs.scaleOverride) &&
           lhs.pitchEditMode == rhs.pitchEditMode &&
           lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity &&
           lhs.gate == rhs.gate &&
           lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

FLASHMEM bool sameSequence(
    const StepSequencerSequence& lhs,
    const StepSequencerSequence& rhs
) {
    return lhs.kind == rhs.kind &&
           lhs.firstStepNode == rhs.firstStepNode &&
           lhs.length == rhs.length &&
           lhs.offset == rhs.offset;
}

FLASHMEM bool sameCycleSet(
    const StepSequencerCycleStateSet& lhs,
    const StepSequencerCycleStateSet& rhs
) {
    return lhs.firstStateNode == rhs.firstStateNode &&
           lhs.length == rhs.length;
}

FLASHMEM bool sameStepNode(
    const StepSequencerStepNode& lhs,
    const StepSequencerStepNode& rhs
) {
    return lhs.flags == rhs.flags &&
           lhs.noteOffset == rhs.noteOffset &&
           lhs.velocityOffset == rhs.velocityOffset &&
           lhs.gateOffset == rhs.gateOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset &&
           lhs.probabilityOffset == rhs.probabilityOffset &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId;
}

FLASHMEM bool sameGraph(const Graph* lhs, const Graph* rhs) {
    const bool lhsEnabled = lhs != nullptr && lhs->enabled;
    const bool rhsEnabled = rhs != nullptr && rhs->enabled;
    if (!lhsEnabled && !rhsEnabled) return true;
    if (lhsEnabled != rhsEnabled) return false;

    if (lhs->rootSequenceId != rhs->rootSequenceId ||
        lhs->stepNodeCount != rhs->stepNodeCount ||
        lhs->sequenceCount != rhs->sequenceCount ||
        lhs->cycleSetCount != rhs->cycleSetCount) {
        return false;
    }

    for (uint16_t i = 0; i < lhs->stepNodes.size(); ++i) {
        if (!sameStepNode(lhs->stepNodes[i], rhs->stepNodes[i])) return false;
    }

    for (uint8_t i = 0; i < lhs->sequences.size(); ++i) {
        if (!sameSequence(lhs->sequences[i], rhs->sequences[i])) return false;
    }

    for (uint8_t i = 0; i < lhs->cycleSets.size(); ++i) {
        if (!sameCycleSet(lhs->cycleSets[i], rhs->cycleSets[i])) return false;
    }

    return true;
}

FLASHMEM bool cloneGraph(const Graph* source, GraphPtr& out) {
    out.reset();
    if (source == nullptr || !source->enabled) {
        return true;
    }

    auto graph = core::app::makeExtmemUnique<Graph>();
    if (!graph) {
        return false;
    }

    *graph = *source;
    out = std::move(graph);
    return true;
}

FLASHMEM bool cloneGraph(const GraphPtr& source, GraphPtr& out) {
    return cloneGraph(source.get(), out);
}

FLASHMEM void installGraph(
    SequencerPatternState& target,
    GraphPtr graph,
    uint32_t revision
) {
    target.graph = std::move(graph);
    target.graphRevision.set(revision);
}

FLASHMEM uint8_t clampedFocusFor(const SequencerState& active, uint8_t focusedStep) {
    const uint8_t length = active.pattern.length.get();
    if (length == 0) {
        return 0;
    }
    return static_cast<uint8_t>(std::min<uint16_t>(focusedStep, length - 1U));
}

FLASHMEM void restoreFocus(SequencerState& active, uint8_t focusedStep) {
    const uint8_t focus = clampedFocusFor(active, focusedStep);
    active.focusedStep.set(focus);
    active.page.set(active.pageForStep(focus));
}

FLASHMEM bool sameFlatTrackBankSnapshot(
    const SequencerTrackBankSnapshot& lhs,
    const SequencerTrackBankSnapshot& rhs
) {
    if (lhs.activeTrack != rhs.activeTrack ||
        lhs.enabledMask != rhs.enabledMask ||
        !sameScaleSettings(lhs.projectScaleSettings, rhs.projectScaleSettings)) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!sameFlatPatternSnapshot(lhs.tracks[i], rhs.tracks[i])) return false;
    }

    return true;
}

FLASHMEM const Graph* effectiveTrackGraph(
    const SequencerHistoryTrackBankSnapshot& snapshot,
    uint8_t track
) {
    const uint8_t activeTrack =
        SequencerTrackBankState::clampTrackIndex(snapshot.flat.activeTrack);
    return (track == activeTrack) ? snapshot.editorGraph.get() : snapshot.bankGraphs[track].get();
}

FLASHMEM uint8_t scopeLimit(SequencerHistoryScope scope) {
    return scope == SequencerHistoryScope::PatternOnly
               ? SequencerHistoryService::PATTERN_ENTRY_LIMIT
               : SequencerHistoryService::FULL_BANK_ENTRY_LIMIT;
}

FLASHMEM uint8_t countScope(
    const std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t count,
    SequencerHistoryScope scope
) {
    uint8_t result = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].scope == scope && entries[i].valid()) {
            ++result;
        }
    }
    return result;
}

FLASHMEM void removeEntryAt(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count,
    uint8_t index
) {
    if (index >= count) return;

    for (uint8_t i = index; static_cast<uint8_t>(i + 1U) < count; ++i) {
        entries[i] = std::move(entries[i + 1U]);
    }

    --count;
    entries[count] = SequencerHistoryEntry{};
}

FLASHMEM void pruneOldestScope(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count,
    SequencerHistoryScope scope
) {
    if (countScope(entries, count, scope) < scopeLimit(scope)) {
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].scope == scope) {
            removeEntryAt(entries, count, i);
            return;
        }
    }
}

FLASHMEM bool pushEntry(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count,
    SequencerHistoryEntry entry
) {
    if (!entry.valid()) {
        return false;
    }

    pruneOldestScope(entries, count, entry.scope);
    if (count >= SequencerHistoryService::ENTRY_LIMIT) {
        removeEntryAt(entries, count, 0);
    }

    entries[count] = std::move(entry);
    ++count;
    return true;
}

FLASHMEM SequencerHistoryEntry popBack(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count
) {
    SequencerHistoryEntry entry;
    if (count == 0) {
        return entry;
    }

    --count;
    entry = std::move(entries[count]);
    entries[count] = SequencerHistoryEntry{};
    return entry;
}

FLASHMEM bool applyEntrySnapshot(
    SequencerHistoryEntry& entry,
    bool after,
    SequencerTrackBankState& bank,
    SequencerState& active
) {
    if (entry.scope == SequencerHistoryScope::PatternOnly) {
        if (!entry.pattern) return false;
        return applyHistorySnapshotToTrack(
            bank,
            active,
            entry.pattern->trackIndex,
            after ? entry.pattern->after : entry.pattern->before
        );
    }

    if (!entry.fullBank) return false;
    return applyHistorySnapshot(bank, active, after ? entry.fullBank->after : entry.fullBank->before);
}

FLASHMEM SequencerHistoryDescriptor descriptorForEntry(
    const SequencerHistoryEntry& entry
) {
    SequencerHistoryDescriptor descriptor{};
    if (entry.scope == SequencerHistoryScope::PatternOnly && entry.pattern) {
        descriptor = entry.pattern->descriptor;
        descriptor.trackIndex = entry.pattern->trackIndex;
        return descriptor;
    }

    if (entry.scope == SequencerHistoryScope::FullBank && entry.fullBank) {
        descriptor = entry.fullBank->descriptor;
        return descriptor;
    }

    descriptor.kind = SequencerHistoryActionKind::FullBank;
    return descriptor;
}

}  // namespace

FLASHMEM bool captureHistorySnapshot(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
) {
    out = SequencerHistoryPatternSnapshot{};
    captureSnapshot(source.pattern, out.flat);
    out.focusedStep = source.focusedStep.get();
    out.page = source.page.get();
    return cloneGraph(graphView(source.pattern), out.graph);
}

FLASHMEM bool captureHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot& out
) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) {
        return captureHistorySnapshot(active, out);
    }

    out = SequencerHistoryPatternSnapshot{};
    const auto& source = bank.track(targetTrack);
    captureSnapshot(source, out.flat);
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();
    return cloneGraph(graphView(source), out.graph);
}

FLASHMEM bool captureHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackBankSnapshot& out
) {
    out = SequencerHistoryTrackBankSnapshot{};
    captureTrackBankSnapshot(bank, active, out.flat);
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();

    if (!cloneGraph(graphView(active.pattern), out.editorGraph)) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!cloneGraph(graphView(bank.track(i)), out.bankGraphs[i])) {
            return false;
        }
    }

    return true;
}

FLASHMEM bool applyHistorySnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryPatternSnapshot& snapshot
) {
    return applyHistorySnapshotToTrack(bank, active, bank.activeTrackIndex(), snapshot);
}

FLASHMEM bool applyHistorySnapshotToTrack(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t trackIndex,
    const SequencerHistoryPatternSnapshot& snapshot
) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    const uint8_t activeTrack = bank.activeTrackIndex();

    if (targetTrack != activeTrack) {
        GraphPtr bankGraph;
        if (!cloneGraph(snapshot.graph, bankGraph)) {
            return false;
        }

        applySnapshot(bank.track(targetTrack), snapshot.flat);
        installGraph(bank.track(targetTrack), std::move(bankGraph), snapshot.flat.graphRevision);
        return true;
    }

    GraphPtr editorGraph;
    GraphPtr bankGraph;
    if (!cloneGraph(snapshot.graph, editorGraph) ||
        !cloneGraph(snapshot.graph, bankGraph)) {
        return false;
    }

    applySnapshotToEditor(active, snapshot.flat);
    installGraph(active.pattern, std::move(editorGraph), snapshot.flat.graphRevision);
    applySnapshot(bank.track(activeTrack), snapshot.flat);
    installGraph(bank.track(activeTrack), std::move(bankGraph), snapshot.flat.graphRevision);
    restoreFocus(active, snapshot.focusedStep);
    return true;
}

FLASHMEM bool applyHistorySnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackBankSnapshot& snapshot
) {
    std::array<GraphPtr, SequencerTrackBankState::TRACK_COUNT> bankGraphs{};
    GraphPtr editorGraph;

    const uint8_t activeTrack =
        SequencerTrackBankState::clampTrackIndex(snapshot.flat.activeTrack);

    if (!cloneGraph(snapshot.editorGraph, editorGraph)) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        const GraphPtr& source = (i == activeTrack) ? snapshot.editorGraph : snapshot.bankGraphs[i];
        if (!cloneGraph(source, bankGraphs[i])) {
            return false;
        }
    }

    applyTrackBankSnapshot(bank, active, snapshot.flat);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        installGraph(bank.track(i), std::move(bankGraphs[i]), snapshot.flat.tracks[i].graphRevision);
    }

    installGraph(active.pattern, std::move(editorGraph), snapshot.flat.tracks[activeTrack].graphRevision);
    restoreFocus(active, snapshot.focusedStep);
    return true;
}

FLASHMEM bool sameMusicalHistorySnapshot(
    const SequencerHistoryPatternSnapshot& lhs,
    const SequencerHistoryPatternSnapshot& rhs
) {
    return sameFlatPatternSnapshot(lhs.flat, rhs.flat) &&
           sameGraph(lhs.graph.get(), rhs.graph.get());
}

FLASHMEM bool sameMusicalHistorySnapshot(
    const SequencerHistoryTrackBankSnapshot& lhs,
    const SequencerHistoryTrackBankSnapshot& rhs
) {
    if (!sameFlatTrackBankSnapshot(lhs.flat, rhs.flat) ||
        !sameGraph(lhs.editorGraph.get(), rhs.editorGraph.get())) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!sameGraph(effectiveTrackGraph(lhs, i), effectiveTrackGraph(rhs, i))) {
            return false;
        }
    }

    return true;
}

FLASHMEM bool SequencerHistoryService::recordPattern(
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    if (sameMusicalHistorySnapshot(before, after)) {
        return false;
    }

    auto change = core::app::makeExtmemUnique<SequencerHistoryPatternChange>();
    if (!change) {
        return false;
    }

    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (descriptor.trackIndex == SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = targetTrack;
    }

    change->trackIndex = targetTrack;
    change->descriptor = descriptor;
    change->before = std::move(before);
    change->after = std::move(after);

    SequencerHistoryEntry entry;
    entry.scope = SequencerHistoryScope::PatternOnly;
    entry.pattern = std::move(change);

    if (!pushUndo(std::move(entry))) {
        return false;
    }

    redo_count_ = 0;
    for (auto& item : redo_) {
        item = SequencerHistoryEntry{};
    }
    return true;
}

FLASHMEM bool SequencerHistoryService::recordPattern(
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    return recordPattern(0, std::move(before), std::move(after), descriptor);
}

FLASHMEM bool SequencerHistoryService::recordFullBank(
    SequencerHistoryTrackBankSnapshot before,
    SequencerHistoryTrackBankSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    auto change = core::app::makeExtmemUnique<SequencerHistoryFullBankChange>();
    if (!change) {
        return false;
    }

    change->descriptor = descriptor;
    change->before = std::move(before);
    change->after = std::move(after);
    return recordFullBank(std::move(change));
}

FLASHMEM bool SequencerHistoryService::recordFullBank(
    SequencerHistoryFullBankChangePtr change
) {
    if (!change) {
        return false;
    }

    if (sameMusicalHistorySnapshot(change->before, change->after)) {
        return false;
    }

    if (change->descriptor.kind == SequencerHistoryActionKind::PatternEdit) {
        change->descriptor.kind = SequencerHistoryActionKind::FullBank;
    }

    SequencerHistoryEntry entry;
    entry.scope = SequencerHistoryScope::FullBank;
    entry.fullBank = std::move(change);

    if (!pushUndo(std::move(entry))) {
        return false;
    }

    redo_count_ = 0;
    for (auto& item : redo_) {
        item = SequencerHistoryEntry{};
    }
    return true;
}

FLASHMEM bool SequencerHistoryService::undo(
    SequencerTrackBankState& bank,
    SequencerState& active
) {
    return undoWithResult(bank, active).applied;
}

FLASHMEM SequencerHistoryApplyResult SequencerHistoryService::undoWithResult(
    SequencerTrackBankState& bank,
    SequencerState& active
) {
    SequencerHistoryApplyResult result;
    result.direction = SequencerHistoryDirection::Undo;

    if (undo_count_ == 0) {
        return result;
    }

    SequencerHistoryEntry& entry = undo_[undo_count_ - 1U];
    result.descriptor = descriptorForEntry(entry);
    if (!applyEntrySnapshot(entry, false, bank, active)) {
        return result;
    }

    auto moved = popBack(undo_, undo_count_);
    result.applied = pushRedo(std::move(moved));
    return result;
}

FLASHMEM bool SequencerHistoryService::redo(
    SequencerTrackBankState& bank,
    SequencerState& active
) {
    return redoWithResult(bank, active).applied;
}

FLASHMEM SequencerHistoryApplyResult SequencerHistoryService::redoWithResult(
    SequencerTrackBankState& bank,
    SequencerState& active
) {
    SequencerHistoryApplyResult result;
    result.direction = SequencerHistoryDirection::Redo;

    if (redo_count_ == 0) {
        return result;
    }

    SequencerHistoryEntry& entry = redo_[redo_count_ - 1U];
    result.descriptor = descriptorForEntry(entry);
    if (!applyEntrySnapshot(entry, true, bank, active)) {
        return result;
    }

    auto moved = popBack(redo_, redo_count_);
    result.applied = pushUndo(std::move(moved));
    return result;
}

FLASHMEM void SequencerHistoryService::clear() {
    undo_count_ = 0;
    redo_count_ = 0;
    for (auto& item : undo_) {
        item = SequencerHistoryEntry{};
    }
    for (auto& item : redo_) {
        item = SequencerHistoryEntry{};
    }
}

FLASHMEM uint8_t SequencerHistoryService::undoCount(SequencerHistoryScope scope) const {
    return countScope(undo_, undo_count_, scope);
}

FLASHMEM uint8_t SequencerHistoryService::redoCount(SequencerHistoryScope scope) const {
    return countScope(redo_, redo_count_, scope);
}

FLASHMEM bool SequencerHistoryService::pushUndo(SequencerHistoryEntry entry) {
    return pushEntry(undo_, undo_count_, std::move(entry));
}

FLASHMEM bool SequencerHistoryService::pushRedo(SequencerHistoryEntry entry) {
    return pushEntry(redo_, redo_count_, std::move(entry));
}

}  // namespace core::state::sequencer

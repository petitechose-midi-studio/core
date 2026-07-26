#include "state/sequencer/SequencerHistory.hpp"

#include <algorithm>
#include <cassert>
#include <new>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerChordState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

FLASHMEM SequencerHistoryPatternSnapshot::SequencerHistoryPatternSnapshot() = default;
FLASHMEM SequencerHistoryPatternSnapshot::~SequencerHistoryPatternSnapshot() = default;
FLASHMEM SequencerHistoryPatternSnapshot::SequencerHistoryPatternSnapshot(
    SequencerHistoryPatternSnapshot&&
) noexcept = default;
FLASHMEM SequencerHistoryPatternSnapshot& SequencerHistoryPatternSnapshot::operator=(
    SequencerHistoryPatternSnapshot&&
) noexcept = default;
FLASHMEM void SequencerHistoryPatternSnapshot::reset() {
    this->~SequencerHistoryPatternSnapshot();
    ::new (static_cast<void*>(this)) SequencerHistoryPatternSnapshot();
}

FLASHMEM SequencerHistoryTrackBankSnapshot::SequencerHistoryTrackBankSnapshot() = default;
FLASHMEM SequencerHistoryTrackBankSnapshot::~SequencerHistoryTrackBankSnapshot() = default;
FLASHMEM SequencerHistoryTrackBankSnapshot::SequencerHistoryTrackBankSnapshot(
    SequencerHistoryTrackBankSnapshot&&
) noexcept = default;
FLASHMEM SequencerHistoryTrackBankSnapshot& SequencerHistoryTrackBankSnapshot::operator=(
    SequencerHistoryTrackBankSnapshot&&
) noexcept = default;
FLASHMEM void SequencerHistoryTrackBankSnapshot::reset() {
    this->~SequencerHistoryTrackBankSnapshot();
    ::new (static_cast<void*>(this)) SequencerHistoryTrackBankSnapshot();
}

FLASHMEM SequencerHistoryPatternChange::SequencerHistoryPatternChange() = default;
FLASHMEM SequencerHistoryPatternChange::~SequencerHistoryPatternChange() = default;
FLASHMEM SequencerHistoryPatternChange::SequencerHistoryPatternChange(
    SequencerHistoryPatternChange&&
) noexcept = default;
FLASHMEM SequencerHistoryPatternChange& SequencerHistoryPatternChange::operator=(
    SequencerHistoryPatternChange&&
) noexcept = default;

FLASHMEM SequencerHistoryFullBankChange::SequencerHistoryFullBankChange() = default;
FLASHMEM SequencerHistoryFullBankChange::~SequencerHistoryFullBankChange() = default;
FLASHMEM SequencerHistoryFullBankChange::SequencerHistoryFullBankChange(
    SequencerHistoryFullBankChange&&
) noexcept = default;
FLASHMEM SequencerHistoryFullBankChange& SequencerHistoryFullBankChange::operator=(
    SequencerHistoryFullBankChange&&
) noexcept = default;

FLASHMEM SequencerHistoryEntry::SequencerHistoryEntry() = default;
FLASHMEM SequencerHistoryEntry::~SequencerHistoryEntry() = default;
FLASHMEM SequencerHistoryEntry::SequencerHistoryEntry(SequencerHistoryEntry&&) noexcept = default;
FLASHMEM SequencerHistoryEntry& SequencerHistoryEntry::operator=(
    SequencerHistoryEntry&&
) noexcept = default;

FLASHMEM SequencerHistoryService::SequencerHistoryService() = default;
FLASHMEM SequencerHistoryService::~SequencerHistoryService() = default;

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
           lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart &&
           lhs.loopEnd == rhs.loopEnd &&
           lhs.stepsPerBeat == rhs.stepsPerBeat &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.swingOffsetPercent == rhs.swingOffsetPercent &&
           lhs.patternNudgePercent == rhs.patternNudgePercent &&
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
           lhs.length == rhs.length &&
           lhs.offset == rhs.offset;
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
           sameVariationRanges(lhs.localVariation, rhs.localVariation) &&
           lhs.chordMode == rhs.chordMode &&
           chordSpecEqualsSanitized(lhs.chordSpec, rhs.chordSpec) &&
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

    if (lhs->stepNodeCount > lhs->stepNodes.size() ||
        rhs->stepNodeCount > rhs->stepNodes.size() ||
        lhs->sequenceCount > lhs->sequences.size() ||
        rhs->sequenceCount > rhs->sequences.size() ||
        lhs->cycleSetCount > lhs->cycleSets.size() ||
        rhs->cycleSetCount > rhs->cycleSets.size()) {
        return false;
    }

    for (uint16_t i = 0; i < lhs->stepNodeCount; ++i) {
        if (!sameStepNode(lhs->stepNodes[i], rhs->stepNodes[i])) return false;
    }

    for (uint8_t i = 0; i < lhs->sequenceCount; ++i) {
        if (!sameSequence(lhs->sequences[i], rhs->sequences[i])) return false;
    }

    for (uint8_t i = 0; i < lhs->cycleSetCount; ++i) {
        if (!sameCycleSet(lhs->cycleSets[i], rhs->cycleSets[i])) return false;
    }

    return true;
}

FLASHMEM bool cloneGraph(const Graph* source, GraphPtr& out) {
    out.reset();
    if (source == nullptr || !source->enabled) {
        return true;
    }

    auto graph = core::app::makeExtmemUnique<Graph>(*source);
    if (!graph) return false;
    out = std::move(graph);
    return true;
}

FLASHMEM bool cloneGraph(const GraphPtr& source, GraphPtr& out) {
    return cloneGraph(source.get(), out);
}

FLASHMEM bool captureGraphUsingReservedStorage(const Graph* source, GraphPtr& out) {
    if (source == nullptr || !source->enabled) {
        out.reset();
        return true;
    }
    if (!out) {
        out = core::app::makeExtmemUnique<Graph>();
        if (!out) return false;
    }
    *out = *source;
    return true;
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
    switch (scope) {
        case SequencerHistoryScope::PatternOnly:
            return SequencerHistoryService::PATTERN_ENTRY_LIMIT;
        case SequencerHistoryScope::Structure:
            return SequencerHistoryService::STRUCTURE_ENTRY_LIMIT;
        case SequencerHistoryScope::FullBank:
        default:
            return SequencerHistoryService::FULL_BANK_ENTRY_LIMIT;
    }
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

constexpr size_t kExtmemAllocationOverheadEstimate = 16U;

FLASHMEM size_t graphRetainedBytes(const GraphPtr& graph) {
    return graph ? sizeof(Graph) + kExtmemAllocationOverheadEstimate : 0U;
}

FLASHMEM size_t ccLaneRetainedBytes(const SequencerHistoryCcLanePtr& lanes) {
    return lanes
        ? sizeof(SequencerCcLaneBank) + kExtmemAllocationOverheadEstimate
        : 0U;
}

FLASHMEM size_t patternSnapshotRetainedBytes(
    const SequencerHistoryPatternSnapshot& snapshot
) {
    return graphRetainedBytes(snapshot.graph) +
           ccLaneRetainedBytes(snapshot.ccLanes);
}

FLASHMEM size_t trackBankSnapshotRetainedBytes(
    const SequencerHistoryTrackBankSnapshot& snapshot
) {
    size_t bytes = graphRetainedBytes(snapshot.editorGraph);
    for (const auto& graph : snapshot.bankGraphs) {
        bytes += graphRetainedBytes(graph);
    }
    bytes += ccLaneRetainedBytes(snapshot.editorCcLanes);
    for (const auto& lanes : snapshot.bankCcLanes) {
        bytes += ccLaneRetainedBytes(lanes);
    }
    return bytes;
}

FLASHMEM size_t structureSnapshotRetainedBytes(
    const SequencerHistoryTrackStructureSnapshot& snapshot
) {
    size_t bytes = 0;
    for (const auto& track : snapshot.tracks) {
        bytes += patternSnapshotRetainedBytes(track);
    }
    return bytes;
}

FLASHMEM size_t structureChangeRetainedBytes(
    const SequencerHistoryTrackStructureChange& change
) {
    size_t bytes = sizeof(SequencerHistoryTrackStructureChange) +
                   kExtmemAllocationOverheadEstimate +
                   structureSnapshotRetainedBytes(change.before) +
                   structureSnapshotRetainedBytes(change.after);
    if (change.macroStructure != nullptr) {
        bytes += sizeof(SequencerHistoryMacroTrackStructurePayload) +
                 kExtmemAllocationOverheadEstimate;
        if (change.macroStructure->beforeControl != nullptr) {
            bytes += sizeof(core::state::modulation::ProjectControlDomainState) +
                     kExtmemAllocationOverheadEstimate;
        }
        if (change.macroStructure->afterControl != nullptr) {
            bytes += sizeof(core::state::modulation::ProjectControlDomainState) +
                     kExtmemAllocationOverheadEstimate;
        }
    }
    return bytes;
}

FLASHMEM size_t patternChangeRetainedBytes(
    const SequencerHistoryPatternChange& change
) {
    return sizeof(SequencerHistoryPatternChange) +
           kExtmemAllocationOverheadEstimate +
           patternSnapshotRetainedBytes(change.before) +
           patternSnapshotRetainedBytes(change.after);
}

FLASHMEM size_t patternChangeAdmissionBytes(
    const SequencerHistoryPatternChange& change
) {
    // FlatOnly payload owners are discarded before the entry is retained. Its
    // admission cost is therefore the normalized fixed-size change, while
    // retainedBytes() deliberately measures every owner that actually remains
    // so a broken normalization can never under-report the PSRAM budget.
    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        return sizeof(SequencerHistoryPatternChange) +
               kExtmemAllocationOverheadEstimate;
    }
    return patternChangeRetainedBytes(change);
}

FLASHMEM bool incomingEntryFitsRetainedBudget(size_t incomingBytes) {
    // recordEntry may evict every retained entry, so admission depends only on
    // whether the incoming entry itself fits the total retained-byte budget.
    return incomingBytes <= SequencerHistoryService::RETAINED_BYTE_BUDGET;
}

FLASHMEM size_t entryRetainedBytes(const SequencerHistoryEntry& entry) {
    switch (entry.scope) {
        case SequencerHistoryScope::PatternOnly:
            if (!entry.pattern) return 0;
            return patternChangeRetainedBytes(*entry.pattern);
        case SequencerHistoryScope::Structure:
            if (!entry.structure) return 0;
            return structureChangeRetainedBytes(*entry.structure);
        case SequencerHistoryScope::FullBank:
            if (!entry.fullBank) return 0;
            return sizeof(SequencerHistoryFullBankChange) +
                   kExtmemAllocationOverheadEstimate +
                   trackBankSnapshotRetainedBytes(entry.fullBank->before) +
                   trackBankSnapshotRetainedBytes(entry.fullBank->after);
        default:
            return 0;
    }
}

FLASHMEM size_t entriesRetainedBytes(
    const std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t count
) {
    size_t bytes = 0;
    for (uint8_t i = 0; i < count; ++i) {
        bytes += entryRetainedBytes(entries[i]);
    }
    return bytes;
}

FLASHMEM uintptr_t projectHistoryIdentity(const SequencerHistoryEntry& entry) {
    switch (entry.scope) {
        case SequencerHistoryScope::PatternOnly:
            return reinterpret_cast<uintptr_t>(entry.pattern.get());
        case SequencerHistoryScope::Structure:
            return reinterpret_cast<uintptr_t>(entry.structure.get());
        case SequencerHistoryScope::FullBank:
            return reinterpret_cast<uintptr_t>(entry.fullBank.get());
        default:
            return 0U;
    }
}

FLASHMEM void removeEntryAt(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count,
    uint8_t index,
    const core::state::project::ProjectHistoryEventSink* sink
) {
    if (index >= count) return;

    if (sink != nullptr) {
        sink->notifyEvicted(
            core::state::project::ProjectHistoryDomain::Sequencer,
            projectHistoryIdentity(entries[index])
        );
    }

    for (uint8_t i = index; static_cast<uint8_t>(i + 1U) < count; ++i) {
        entries[i] = std::move(entries[i + 1U]);
    }

    --count;
    entries[count] = SequencerHistoryEntry{};
}

FLASHMEM void pruneOldestScope(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count,
    SequencerHistoryScope scope,
    const core::state::project::ProjectHistoryEventSink* sink
) {
    if (countScope(entries, count, scope) < scopeLimit(scope)) {
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].scope == scope) {
            removeEntryAt(entries, count, i, sink);
            return;
        }
    }
}

FLASHMEM bool pushEntry(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count,
    SequencerHistoryEntry entry,
    const core::state::project::ProjectHistoryEventSink* sink
) {
    if (!entry.valid()) {
        return false;
    }

    pruneOldestScope(entries, count, entry.scope, sink);
    if (count >= SequencerHistoryService::ENTRY_LIMIT) {
        removeEntryAt(entries, count, 0, sink);
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

FLASHMEM bool applyFlatHistorySnapshotToTrack(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t trackIndex,
    const SequencerHistoryPatternSnapshot& snapshot
) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    const uint8_t activeTrack = bank.activeTrackIndex();

    if (targetTrack != activeTrack) {
        applySnapshotPreservingGraph(bank.track(targetTrack), snapshot.flat);
        return true;
    }

    applySnapshotToEditorPreservingGraph(active, snapshot.flat);
    applySnapshotPreservingGraph(bank.track(activeTrack), snapshot.flat);
    restoreFocus(active, snapshot.focusedStep);
    return true;
}

FLASHMEM bool applyEntrySnapshot(
    SequencerHistoryEntry& entry,
    bool after,
    SequencerTrackBankState& bank,
    SequencerState& active
) {
    if (entry.scope == SequencerHistoryScope::PatternOnly) {
        if (!entry.pattern) return false;
        const auto& snapshot = after ? entry.pattern->after : entry.pattern->before;
        if (entry.pattern->storage == SequencerHistoryPatternStorage::FlatOnly) {
            return applyFlatHistorySnapshotToTrack(
                bank,
                active,
                entry.pattern->trackIndex,
                snapshot
            );
        }
        return applyHistorySnapshotToTrack(
            bank,
            active,
            entry.pattern->trackIndex,
            snapshot
        );
    }

    if (entry.scope == SequencerHistoryScope::Structure) {
        if (!entry.structure) return false;
        return applyHistoryStructureSnapshot(
            bank,
            active,
            after ? entry.structure->after : entry.structure->before
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

    if (entry.scope == SequencerHistoryScope::Structure && entry.structure) {
        descriptor = entry.structure->descriptor;
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
    out.reset();
    return captureHistorySnapshotUsingReservedGraph(source, out);
}

FLASHMEM bool reserveHistorySnapshotGraphStorage(
    SequencerHistoryPatternSnapshot& snapshot
) {
    if (snapshot.graph) return true;
    snapshot.graph = core::app::makeExtmemUnique<Graph>();
    return static_cast<bool>(snapshot.graph);
}

FLASHMEM bool captureHistorySnapshotUsingReservedGraph(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
) {
    captureSnapshot(source.pattern, out.flat);
    out.focusedStep = source.focusedStep.get();
    if (!captureGraphUsingReservedStorage(graphView(source.pattern), out.graph) ||
        !captureSequencerCcLaneBankUsingReservedStorage(
            source.pattern.ccLanes.get(),
            out.ccLanes
        )) {
        return false;
    }
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM void captureFlatHistorySnapshot(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
) {
    out.reset();
    captureSnapshot(source.pattern, out.flat);
    out.focusedStep = source.focusedStep.get();
    out.ccLanesCaptured = false;
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

    out.reset();
    return captureHistorySnapshotUsingReservedGraph(bank, active, targetTrack, out);
}

FLASHMEM bool captureHistorySnapshotUsingReservedGraph(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot& out
) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) {
        return captureHistorySnapshotUsingReservedGraph(active, out);
    }

    const auto& source = bank.track(targetTrack);
    captureSnapshot(source, out.flat);
    out.focusedStep = active.focusedStep.get();
    if (!captureGraphUsingReservedStorage(graphView(source), out.graph) ||
        !captureSequencerCcLaneBankUsingReservedStorage(
            source.ccLanes.get(),
            out.ccLanes
        )) {
        return false;
    }
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM void captureFlatHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot& out
) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) {
        captureFlatHistorySnapshot(active, out);
        return;
    }

    out.reset();
    captureSnapshot(bank.track(targetTrack), out.flat);
    out.focusedStep = active.focusedStep.get();
    out.ccLanesCaptured = false;
}

FLASHMEM bool captureHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackBankSnapshot& out
) {
    out.reset();
    captureTrackBankSnapshot(bank, active, out.flat);
    out.focusedStep = active.focusedStep.get();
    out.activeStepProperty = active.activeStepProperty.get();

    if (!cloneGraph(graphView(active.pattern), out.editorGraph)) {
        return false;
    }
    if (!cloneSequencerCcLaneBank(
            out.editorCcLanes,
            active.pattern.ccLanes.get()
        )) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!cloneGraph(graphView(bank.track(i)), out.bankGraphs[i])) {
            return false;
        }
        if (!cloneSequencerCcLaneBank(
                out.bankCcLanes[i],
                bank.track(i).ccLanes.get()
            )) {
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

FLASHMEM bool applyHistorySnapshotToEditor(
    SequencerState& active,
    const SequencerHistoryPatternSnapshot& snapshot
) {
    GraphPtr editorGraph;
    SequencerHistoryCcLanePtr editorCcLanes;
    if (!cloneGraph(snapshot.graph, editorGraph)) {
        return false;
    }
    if (snapshot.ccLanesCaptured &&
        !cloneSequencerCcLaneBank(editorCcLanes, snapshot.ccLanes.get())) {
        return false;
    }

    applySnapshotToEditor(active, snapshot.flat);
    installGraph(active.pattern, std::move(editorGraph), snapshot.flat.graphRevision);
    if (snapshot.ccLanesCaptured) {
        installSequencerCcLaneBank(active.pattern, std::move(editorCcLanes));
    }
    restoreFocus(active, snapshot.focusedStep);
    return true;
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
        SequencerHistoryCcLanePtr bankCcLanes;
        if (!cloneGraph(snapshot.graph, bankGraph)) {
            return false;
        }
        if (snapshot.ccLanesCaptured &&
            !cloneSequencerCcLaneBank(bankCcLanes, snapshot.ccLanes.get())) {
            return false;
        }

        applySnapshot(bank.track(targetTrack), snapshot.flat);
        installGraph(bank.track(targetTrack), std::move(bankGraph), snapshot.flat.graphRevision);
        if (snapshot.ccLanesCaptured) {
            installSequencerCcLaneBank(
                bank.track(targetTrack),
                std::move(bankCcLanes)
            );
        }
        return true;
    }

    GraphPtr editorGraph;
    GraphPtr bankGraph;
    SequencerHistoryCcLanePtr editorCcLanes;
    SequencerHistoryCcLanePtr bankCcLanes;
    if (!cloneGraph(snapshot.graph, editorGraph) ||
        !cloneGraph(snapshot.graph, bankGraph)) {
        return false;
    }
    if (snapshot.ccLanesCaptured &&
        (!cloneSequencerCcLaneBank(editorCcLanes, snapshot.ccLanes.get()) ||
         !cloneSequencerCcLaneBank(bankCcLanes, snapshot.ccLanes.get()))) {
        return false;
    }

    applySnapshotToEditor(active, snapshot.flat);
    installGraph(active.pattern, std::move(editorGraph), snapshot.flat.graphRevision);
    if (snapshot.ccLanesCaptured) {
        installSequencerCcLaneBank(active.pattern, std::move(editorCcLanes));
    }
    applySnapshot(bank.track(activeTrack), snapshot.flat);
    installGraph(bank.track(activeTrack), std::move(bankGraph), snapshot.flat.graphRevision);
    if (snapshot.ccLanesCaptured) {
        installSequencerCcLaneBank(bank.track(activeTrack), std::move(bankCcLanes));
    }
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
    std::array<SequencerHistoryCcLanePtr, SequencerTrackBankState::TRACK_COUNT>
        bankCcLanes{};
    SequencerHistoryCcLanePtr editorCcLanes;

    const uint8_t activeTrack =
        SequencerTrackBankState::clampTrackIndex(snapshot.flat.activeTrack);

    if (!cloneGraph(snapshot.editorGraph, editorGraph)) {
        return false;
    }
    if (!cloneSequencerCcLaneBank(
            editorCcLanes,
            snapshot.editorCcLanes.get()
        )) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        const GraphPtr& source = (i == activeTrack) ? snapshot.editorGraph : snapshot.bankGraphs[i];
        if (!cloneGraph(source, bankGraphs[i])) {
            return false;
        }
        const auto* sourceLanes = i == activeTrack
            ? snapshot.editorCcLanes.get()
            : snapshot.bankCcLanes[i].get();
        if (!cloneSequencerCcLaneBank(bankCcLanes[i], sourceLanes)) {
            return false;
        }
    }

    applyTrackBankSnapshot(bank, active, snapshot.flat);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        installGraph(bank.track(i), std::move(bankGraphs[i]), snapshot.flat.tracks[i].graphRevision);
        installSequencerCcLaneBank(bank.track(i), std::move(bankCcLanes[i]));
    }

    installGraph(active.pattern, std::move(editorGraph), snapshot.flat.tracks[activeTrack].graphRevision);
    installSequencerCcLaneBank(active.pattern, std::move(editorCcLanes));
    restoreFocus(active, snapshot.focusedStep);
    active.activeStepProperty.set(snapshot.activeStepProperty);
    return true;
}

FLASHMEM bool sameMusicalHistorySnapshot(
    const SequencerHistoryPatternSnapshot& lhs,
    const SequencerHistoryPatternSnapshot& rhs
) {
    return sameFlatPatternSnapshot(lhs.flat, rhs.flat) &&
           sameGraph(lhs.graph.get(), rhs.graph.get()) &&
           (!lhs.ccLanesCaptured || !rhs.ccLanesCaptured ||
            sameOptionalSequencerCcLaneBank(
                lhs.ccLanes.get(),
                rhs.ccLanes.get()
            ));
}

FLASHMEM bool sameMusicalHistorySnapshot(
    const SequencerHistoryTrackBankSnapshot& lhs,
    const SequencerHistoryTrackBankSnapshot& rhs
) {
    if (!sameFlatTrackBankSnapshot(lhs.flat, rhs.flat) ||
        !sameGraph(lhs.editorGraph.get(), rhs.editorGraph.get()) ||
        !sameOptionalSequencerCcLaneBank(
            lhs.editorCcLanes.get(),
            rhs.editorCcLanes.get()
        )) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!sameGraph(effectiveTrackGraph(lhs, i), effectiveTrackGraph(rhs, i))) {
            return false;
        }
        const auto* lhsLanes = i == lhs.flat.activeTrack
            ? lhs.editorCcLanes.get()
            : lhs.bankCcLanes[i].get();
        const auto* rhsLanes = i == rhs.flat.activeTrack
            ? rhs.editorCcLanes.get()
            : rhs.bankCcLanes[i].get();
        if (!sameOptionalSequencerCcLaneBank(lhsLanes, rhsLanes)) return false;
    }

    return true;
}

FLASHMEM bool SequencerHistoryService::recordPattern(
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    return recordPatternWithStorage(
        trackIndex,
        std::move(before),
        std::move(after),
        descriptor,
        SequencerHistoryPatternStorage::FullGraph
    );
}

FLASHMEM bool SequencerHistoryService::recordFlatPattern(
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    return recordPatternWithStorage(
        trackIndex,
        std::move(before),
        std::move(after),
        descriptor,
        SequencerHistoryPatternStorage::FlatOnly
    );
}

FLASHMEM bool SequencerHistoryService::recordPatternWithStorage(
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor,
    SequencerHistoryPatternStorage storage
) {
    auto change = core::app::makeExtmemUnique<SequencerHistoryPatternChange>();
    if (!change) {
        return false;
    }

    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (descriptor.trackIndex == SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = targetTrack;
    }

    change->trackIndex = targetTrack;
    change->storage = storage;
    change->descriptor = descriptor;
    change->before = std::move(before);
    change->after = std::move(after);

    return recordPattern(std::move(change));
}

FLASHMEM bool SequencerHistoryService::recordPattern(
    SequencerHistoryPatternChangePtr change
) {
    if (!change || !canRecordPattern(*change)) return false;
    recordPreparedPattern(std::move(change));
    return true;
}

FLASHMEM bool SequencerHistoryService::canRecordPattern(
    const SequencerHistoryPatternChange& change
) const {
    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        if (change.before.flat.graphRevision != change.after.flat.graphRevision ||
            sameFlatPatternSnapshot(change.before.flat, change.after.flat)) {
            return false;
        }
    } else if (sameMusicalHistorySnapshot(change.before, change.after)) {
        return false;
    }

    return incomingEntryFitsRetainedBudget(patternChangeAdmissionBytes(change));
}

FLASHMEM void SequencerHistoryService::recordPreparedPattern(
    SequencerHistoryPatternChangePtr change
) {
    assert(change && canRecordPattern(*change));
    if (!change) return;

    if (change->storage == SequencerHistoryPatternStorage::FlatOnly) {
        change->before.graph.reset();
        change->after.graph.reset();
        change->before.ccLanes.reset();
        change->after.ccLanes.reset();
        change->before.ccLanesCaptured = false;
        change->after.ccLanesCaptured = false;
    }
    change->trackIndex = SequencerTrackBankState::clampTrackIndex(change->trackIndex);
    if (change->descriptor.trackIndex == SequencerHistoryDescriptor::INVALID_INDEX) {
        change->descriptor.trackIndex = change->trackIndex;
    }

    SequencerHistoryEntry entry;
    entry.scope = SequencerHistoryScope::PatternOnly;
    entry.pattern = std::move(change);
    commitPreparedEntry(std::move(entry));
}

FLASHMEM bool SequencerHistoryService::recordPattern(
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    return recordPattern(0, std::move(before), std::move(after), descriptor);
}

FLASHMEM bool SequencerHistoryService::recordFlatPattern(
    SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after,
    SequencerHistoryDescriptor descriptor
) {
    return recordFlatPattern(0, std::move(before), std::move(after), descriptor);
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

    return recordEntry(std::move(entry));
}

FLASHMEM bool SequencerHistoryService::recordStructure(
    SequencerHistoryTrackStructureChangePtr change
) {
    if (!change || !canRecordStructure(*change)) {
        return false;
    }

    recordPreparedStructure(std::move(change));
    return true;
}

FLASHMEM void SequencerHistoryService::recordPreparedStructure(
    SequencerHistoryTrackStructureChangePtr change
) {
    assert(change && canRecordStructure(*change));
    if (!change) return;

    if (change->descriptor.kind == SequencerHistoryActionKind::PatternEdit) {
        change->descriptor.kind = SequencerHistoryActionKind::TrackStructure;
    }

    SequencerHistoryEntry entry;
    entry.scope = SequencerHistoryScope::Structure;
    entry.structure = std::move(change);

    commitPreparedEntry(std::move(entry));
}

FLASHMEM bool SequencerHistoryService::canRecordStructure(
    const SequencerHistoryTrackStructureChange& change
) const {
    if (sameMusicalHistoryStructureSnapshot(change.before, change.after) &&
        !macroTrackStructureHistoryChanged(change)) {
        return false;
    }

    return incomingEntryFitsRetainedBudget(structureChangeRetainedBytes(change));
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
    const uintptr_t projectHistoryEntryIdentity = projectHistoryIdentity(entry);
    result.descriptor = descriptorForEntry(entry);
    if (!applyEntrySnapshot(entry, false, bank, active)) {
        return result;
    }

    auto moved = popBack(undo_, undo_count_);
    result.applied = pushRedo(std::move(moved));
    if (result.applied && project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            core::state::project::ProjectHistoryDomain::Sequencer,
            projectHistoryEntryIdentity,
            core::state::project::ProjectHistoryDirection::Undo
        );
    }
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
    const uintptr_t projectHistoryEntryIdentity = projectHistoryIdentity(entry);
    result.descriptor = descriptorForEntry(entry);
    if (!applyEntrySnapshot(entry, true, bank, active)) {
        return result;
    }

    auto moved = popBack(redo_, redo_count_);
    result.applied = pushUndo(std::move(moved));
    if (result.applied && project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            core::state::project::ProjectHistoryDomain::Sequencer,
            projectHistoryEntryIdentity,
            core::state::project::ProjectHistoryDirection::Redo
        );
    }
    return result;
}

FLASHMEM bool SequencerHistoryService::peekUndoTrackActivation(
    SequencerTrackActivationHistoryPlan& out
) const {
    out = {};
    if (undo_count_ == 0) return false;
    const auto& entry = undo_[undo_count_ - 1U];
    if (entry.scope == SequencerHistoryScope::PatternOnly && entry.pattern &&
        entry.pattern->activation.valid()) {
        out.reference = entry.pattern->activation;
        out.targetAudibleMask = entry.pattern->activationTargetAudibleMask;
        return true;
    }
    if (entry.scope != SequencerHistoryScope::Structure || !entry.structure ||
        !entry.structure->activation.valid()) return false;
    out.reference = entry.structure->activation;
    out.targetAudibleMask = entry.structure->activationBeforeAudibleMask;
    return true;
}

FLASHMEM bool SequencerHistoryService::peekRedoTrackActivation(
    SequencerTrackActivationHistoryPlan& out
) const {
    out = {};
    if (redo_count_ == 0) return false;
    const auto& entry = redo_[redo_count_ - 1U];
    if (entry.scope == SequencerHistoryScope::PatternOnly && entry.pattern &&
        entry.pattern->activation.valid()) {
        out.reference = entry.pattern->activation;
        out.targetAudibleMask = entry.pattern->activationTargetAudibleMask;
        return true;
    }
    if (entry.scope != SequencerHistoryScope::Structure || !entry.structure ||
        !entry.structure->activation.valid()) return false;
    out.reference = entry.structure->activation;
    out.targetAudibleMask = entry.structure->activationAfterAudibleMask;
    return true;
}

FLASHMEM const SequencerHistoryMacroTrackStructurePayload*
SequencerHistoryService::peekUndoMacroTrackStructure() const {
    if (undo_count_ == 0U) return nullptr;
    const auto& entry = undo_[undo_count_ - 1U];
    if (entry.scope != SequencerHistoryScope::Structure || !entry.structure) {
        return nullptr;
    }
    return entry.structure->macroStructure.get();
}

FLASHMEM const SequencerHistoryMacroTrackStructurePayload*
SequencerHistoryService::peekRedoMacroTrackStructure() const {
    if (redo_count_ == 0U) return nullptr;
    const auto& entry = redo_[redo_count_ - 1U];
    if (entry.scope != SequencerHistoryScope::Structure || !entry.structure) {
        return nullptr;
    }
    return entry.structure->macroStructure.get();
}

FLASHMEM void SequencerHistoryService::clear() {
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCleared(
            core::state::project::ProjectHistoryDomain::Sequencer
        );
    }
    undo_count_ = 0;
    redo_count_ = 0;
    for (auto& item : undo_) {
        item = SequencerHistoryEntry{};
    }
    for (auto& item : redo_) {
        item = SequencerHistoryEntry{};
    }
}

FLASHMEM void SequencerHistoryService::discardRedoBranch() {
    for (uint8_t index = 0U; index < redo_count_; ++index) {
        if (project_history_sink_ != nullptr) {
            project_history_sink_->notifyEvicted(
                core::state::project::ProjectHistoryDomain::Sequencer,
                projectHistoryIdentity(redo_[index])
            );
        }
        redo_[index] = SequencerHistoryEntry{};
    }
    redo_count_ = 0U;
}

FLASHMEM uint8_t SequencerHistoryService::undoCount(SequencerHistoryScope scope) const {
    return countScope(undo_, undo_count_, scope);
}

FLASHMEM uint8_t SequencerHistoryService::redoCount(SequencerHistoryScope scope) const {
    return countScope(redo_, redo_count_, scope);
}

FLASHMEM uintptr_t SequencerHistoryService::projectHistoryUndoIdentity() const {
    return undo_count_ == 0U
        ? 0U
        : projectHistoryIdentity(undo_[undo_count_ - 1U]);
}

FLASHMEM uintptr_t SequencerHistoryService::projectHistoryRedoIdentity() const {
    return redo_count_ == 0U
        ? 0U
        : projectHistoryIdentity(redo_[redo_count_ - 1U]);
}

FLASHMEM size_t SequencerHistoryService::retainedBytes() const {
    return entriesRetainedBytes(undo_, undo_count_) +
           entriesRetainedBytes(redo_, redo_count_);
}

FLASHMEM bool SequencerHistoryService::pushUndo(SequencerHistoryEntry entry) {
    return pushEntry(undo_, undo_count_, std::move(entry), project_history_sink_);
}

FLASHMEM bool SequencerHistoryService::pushRedo(SequencerHistoryEntry entry) {
    return pushEntry(redo_, redo_count_, std::move(entry), project_history_sink_);
}

FLASHMEM void SequencerHistoryService::commitPreparedEntry(SequencerHistoryEntry entry) {
    assert(entry.valid());
    const size_t incomingBytes = entryRetainedBytes(entry);
    assert(incomingEntryFitsRetainedBudget(incomingBytes));
    const uintptr_t identity = projectHistoryIdentity(entry);
    const uint8_t actionKind = static_cast<uint8_t>(
        descriptorForEntry(entry).kind
    );

    discardRedoBranch();

    pruneOldestScope(undo_, undo_count_, entry.scope, project_history_sink_);
    while (undo_count_ > 0 && retainedBytes() + incomingBytes > RETAINED_BYTE_BUDGET) {
        removeEntryAt(undo_, undo_count_, 0, project_history_sink_);
    }
    assert(retainedBytes() + incomingBytes <= RETAINED_BYTE_BUDGET);

    const bool pushed = pushUndo(std::move(entry));
    assert(pushed);
    (void)pushed;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCommitted(
            core::state::project::ProjectHistoryDomain::Sequencer,
            identity,
            actionKind
        );
    }
}

FLASHMEM bool SequencerHistoryService::recordEntry(SequencerHistoryEntry entry) {
    if (!entry.valid()) return false;

    const size_t incomingBytes = entryRetainedBytes(entry);
    if (!incomingEntryFitsRetainedBudget(incomingBytes)) return false;

    commitPreparedEntry(std::move(entry));
    return true;
}

}  // namespace core::state::sequencer

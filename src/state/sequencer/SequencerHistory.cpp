#include "state/sequencer/SequencerHistory.hpp"

#include <cassert>

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <new>
#include <utility>

#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerChordState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

FLASHMEM SequencerHistoryPatternPayloadStorage::SequencerHistoryPatternPayloadStorage() = default;
FLASHMEM SequencerHistoryPatternPayloadStorage::~SequencerHistoryPatternPayloadStorage() = default;
FLASHMEM SequencerHistoryPatternPayloadStorage::SequencerHistoryPatternPayloadStorage(
    SequencerHistoryPatternPayloadStorage&&) noexcept = default;
FLASHMEM SequencerHistoryPatternPayloadStorage& SequencerHistoryPatternPayloadStorage::operator=(
    SequencerHistoryPatternPayloadStorage&&) noexcept = default;
FLASHMEM void SequencerHistoryPatternPayloadStorage::reset() {
    graph.reset();
    ccLanes.reset();
}

FLASHMEM
SequencerPreparedActiveTrackSynchronization::SequencerPreparedActiveTrackSynchronization() =
    default;
FLASHMEM
    SequencerPreparedActiveTrackSynchronization::~SequencerPreparedActiveTrackSynchronization() =
        default;
FLASHMEM SequencerPreparedActiveTrackSynchronization::SequencerPreparedActiveTrackSynchronization(
    SequencerPreparedActiveTrackSynchronization&&) noexcept = default;
FLASHMEM SequencerPreparedActiveTrackSynchronization&
SequencerPreparedActiveTrackSynchronization::operator=(
    SequencerPreparedActiveTrackSynchronization&&) noexcept = default;
FLASHMEM void SequencerPreparedActiveTrackSynchronization::reset() {
    trackIndex = SequencerTrackBankState::TRACK_COUNT;
    storage = SequencerHistoryPatternStorage::FullGraph;
    reserved = false;
    captured = false;
    payload.reset();
}

FLASHMEM SequencerHistoryPatternSnapshot::SequencerHistoryPatternSnapshot() = default;
FLASHMEM SequencerHistoryPatternSnapshot::~SequencerHistoryPatternSnapshot() = default;
FLASHMEM SequencerHistoryPatternSnapshot::SequencerHistoryPatternSnapshot(
    SequencerHistoryPatternSnapshot&&) noexcept = default;
FLASHMEM SequencerHistoryPatternSnapshot& SequencerHistoryPatternSnapshot::operator=(
    SequencerHistoryPatternSnapshot&&) noexcept = default;
FLASHMEM void SequencerHistoryPatternSnapshot::reset() {
    this->~SequencerHistoryPatternSnapshot();
    ::new (static_cast<void*>(this)) SequencerHistoryPatternSnapshot();
}

FLASHMEM SequencerHistoryTrackBankSnapshot::SequencerHistoryTrackBankSnapshot() = default;
FLASHMEM SequencerHistoryTrackBankSnapshot::~SequencerHistoryTrackBankSnapshot() = default;
FLASHMEM SequencerHistoryTrackBankSnapshot::SequencerHistoryTrackBankSnapshot(
    SequencerHistoryTrackBankSnapshot&&) noexcept = default;
FLASHMEM SequencerHistoryTrackBankSnapshot& SequencerHistoryTrackBankSnapshot::operator=(
    SequencerHistoryTrackBankSnapshot&&) noexcept = default;
FLASHMEM void SequencerHistoryTrackBankSnapshot::reset() {
    this->~SequencerHistoryTrackBankSnapshot();
    ::new (static_cast<void*>(this)) SequencerHistoryTrackBankSnapshot();
}

FLASHMEM SequencerHistoryPatternChange::SequencerHistoryPatternChange() = default;
FLASHMEM SequencerHistoryPatternChange::~SequencerHistoryPatternChange() = default;
FLASHMEM SequencerHistoryPatternChange::SequencerHistoryPatternChange(
    SequencerHistoryPatternChange&&) noexcept = default;
FLASHMEM SequencerHistoryPatternChange& SequencerHistoryPatternChange::operator=(
    SequencerHistoryPatternChange&&) noexcept = default;
FLASHMEM void SequencerHistoryPatternChange::setPreparedPayloadOwnerProof(
    const SequencerPatternState& pattern) {
    ::new (static_cast<void*>(&auxiliary.preparedOwners)) SequencerPreparedPatternPayloadOwnerProof{
        .graphOwner = reinterpret_cast<uintptr_t>(pattern.graph.get()),
        .ccLaneOwner = reinterpret_cast<uintptr_t>(pattern.ccLanes.get()),
    };
}
FLASHMEM bool SequencerHistoryPatternChange::preparedPayloadOwnerProofMatches(
    const SequencerPatternState& pattern) const {
    return preparedGraphOwnerProofMatches(pattern) &&
           preparedCcLaneOwnerProofMatches(pattern);
}
FLASHMEM bool SequencerHistoryPatternChange::preparedGraphOwnerProofMatches(
    const SequencerPatternState& pattern) const {
    return auxiliary.preparedOwners.graphOwner ==
           reinterpret_cast<uintptr_t>(pattern.graph.get());
}
FLASHMEM bool SequencerHistoryPatternChange::preparedCcLaneOwnerProofMatches(
    const SequencerPatternState& pattern) const {
    return auxiliary.preparedOwners.ccLaneOwner ==
           reinterpret_cast<uintptr_t>(pattern.ccLanes.get());
}
FLASHMEM bool SequencerHistoryPatternChange::preparedGraphOwnerProofPresent() const {
    return auxiliary.preparedOwners.graphOwner != 0U;
}
FLASHMEM bool SequencerHistoryPatternChange::preparedCcLaneOwnerProofPresent() const {
    return auxiliary.preparedOwners.ccLaneOwner != 0U;
}
FLASHMEM void SequencerHistoryPatternChange::clearPreparedPayloadOwnerProof() {
    ::new (static_cast<void*>(&auxiliary.activation)) SequencerHistoryPatternActivationMetadata{};
}

FLASHMEM SequencerHistoryFullBankChange::SequencerHistoryFullBankChange() = default;
FLASHMEM SequencerHistoryFullBankChange::~SequencerHistoryFullBankChange() = default;
FLASHMEM SequencerHistoryFullBankChange::SequencerHistoryFullBankChange(
    SequencerHistoryFullBankChange&&) noexcept = default;
FLASHMEM SequencerHistoryFullBankChange& SequencerHistoryFullBankChange::operator=(
    SequencerHistoryFullBankChange&&) noexcept = default;

FLASHMEM SequencerHistoryEntry::SequencerHistoryEntry() = default;
FLASHMEM SequencerHistoryEntry::~SequencerHistoryEntry() = default;
FLASHMEM SequencerHistoryEntry::SequencerHistoryEntry(SequencerHistoryEntry&&) noexcept = default;
FLASHMEM SequencerHistoryEntry& SequencerHistoryEntry::operator=(SequencerHistoryEntry&&) noexcept =
    default;

FLASHMEM SequencerHistoryService::SequencerHistoryService() = default;
FLASHMEM SequencerHistoryService::~SequencerHistoryService() = default;

namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using GraphPtr = SequencerHistoryGraphPtr;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerStepNode;

[[noreturn]] FLASHMEM void failSequencerHistoryInvariant() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    for (;;) {}
#endif
}

FLASHMEM bool sameScaleSettings(oc::note::sequencer::StepSequencerScaleSettings lhs,
                                oc::note::sequencer::StepSequencerScaleSettings rhs) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

FLASHMEM bool sameVariationRanges(oc::note::sequencer::StepSequencerVariationRanges lhs,
                                  oc::note::sequencer::StepSequencerVariationRanges rhs) {
    lhs.clamp();
    rhs.clamp();
    return lhs.pitchSemitones == rhs.pitchSemitones && lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent && lhs.nudge == rhs.nudge;
}

FLASHMEM bool sameFlatPatternSnapshot(const SequencerPatternSnapshot& lhs,
                                      const SequencerPatternSnapshot& rhs) {
    return lhs.length == rhs.length && lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart && lhs.loopEnd == rhs.loopEnd &&
           lhs.stepsPerBeat == rhs.stepsPerBeat && lhs.enabledMask == rhs.enabledMask &&
           lhs.swingOffsetPercent == rhs.swingOffsetPercent &&
           lhs.patternNudgePercent == rhs.patternNudgePercent &&
           sameVariationRanges(lhs.variationRanges, rhs.variationRanges) &&
           lhs.scalePolicy == rhs.scalePolicy &&
           sameScaleSettings(lhs.scaleOverride, rhs.scaleOverride) &&
           lhs.pitchEditMode == rhs.pitchEditMode && lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity && lhs.gate == rhs.gate && lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

FLASHMEM bool sameSequence(const StepSequencerSequence& lhs, const StepSequencerSequence& rhs) {
    return lhs.kind == rhs.kind && lhs.firstStepNode == rhs.firstStepNode &&
           lhs.length == rhs.length && lhs.offset == rhs.offset;
}

FLASHMEM bool sameCycleSet(const StepSequencerCycleStateSet& lhs,
                           const StepSequencerCycleStateSet& rhs) {
    return lhs.firstStateNode == rhs.firstStateNode && lhs.length == rhs.length &&
           lhs.offset == rhs.offset;
}

FLASHMEM bool sameStepNode(const StepSequencerStepNode& lhs, const StepSequencerStepNode& rhs) {
    return lhs.flags == rhs.flags && lhs.noteOffset == rhs.noteOffset &&
           lhs.velocityOffset == rhs.velocityOffset && lhs.gateOffset == rhs.gateOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset && lhs.probabilityOffset == rhs.probabilityOffset &&
           sameVariationRanges(lhs.localVariation, rhs.localVariation) &&
           lhs.chordMode == rhs.chordMode &&
           chordSpecEqualsSanitized(lhs.chordSpec, rhs.chordSpec) &&
           lhs.childSequenceId == rhs.childSequenceId && lhs.cycleSetId == rhs.cycleSetId;
}

FLASHMEM bool sameGraph(const Graph* lhs, const Graph* rhs) {
    const bool lhsEnabled = lhs != nullptr && lhs->enabled;
    const bool rhsEnabled = rhs != nullptr && rhs->enabled;
    if (!lhsEnabled && !rhsEnabled) return true;
    if (lhsEnabled != rhsEnabled) return false;

    if (lhs->rootSequenceId != rhs->rootSequenceId || lhs->stepNodeCount != rhs->stepNodeCount ||
        lhs->sequenceCount != rhs->sequenceCount || lhs->cycleSetCount != rhs->cycleSetCount) {
        return false;
    }

    if (lhs->stepNodeCount > lhs->stepNodes.size() || rhs->stepNodeCount > rhs->stepNodes.size() ||
        lhs->sequenceCount > lhs->sequences.size() || rhs->sequenceCount > rhs->sequences.size() ||
        lhs->cycleSetCount > lhs->cycleSets.size() || rhs->cycleSetCount > rhs->cycleSets.size()) {
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

FLASHMEM bool sameScaleSettingsExact(
    const oc::note::sequencer::StepSequencerScaleSettings& lhs,
    const oc::note::sequencer::StepSequencerScaleSettings& rhs
) {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

FLASHMEM bool sameVariationRangesExact(
    const oc::note::sequencer::StepSequencerVariationRanges& lhs,
    const oc::note::sequencer::StepSequencerVariationRanges& rhs
) {
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM bool sameChordSpecExact(
    const oc::note::sequencer::StepSequencerChordSpec& lhs,
    const oc::note::sequencer::StepSequencerChordSpec& rhs
) {
    return lhs.voiceCount == rhs.voiceCount &&
           lhs.harmonyData == rhs.harmonyData &&
           lhs.voicingData == rhs.voicingData &&
           lhs.inversionData == rhs.inversionData &&
           lhs.strum == rhs.strum &&
           lhs.velocityCurve == rhs.velocityCurve &&
           lhs.customIntervalExtension == rhs.customIntervalExtension;
}

FLASHMEM bool sameStepNodeExact(
    const StepSequencerStepNode& lhs,
    const StepSequencerStepNode& rhs
) {
    return lhs.flags == rhs.flags &&
           lhs.velocityOffset == rhs.velocityOffset &&
           lhs.gateOffset == rhs.gateOffset &&
           lhs.probabilityOffset == rhs.probabilityOffset &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId &&
           sameVariationRangesExact(lhs.localVariation, rhs.localVariation) &&
           sameChordSpecExact(lhs.chordSpec, rhs.chordSpec) &&
           lhs.chordMode == rhs.chordMode &&
           lhs.noteOffset == rhs.noteOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset;
}

// Prepared Structure revalidation is stricter than musical History equality:
// every declared Graph field, including unused capacity, must still match the
// detached checkpoint when owner identity and revision alone are unchanged.
FLASHMEM bool sameGraphExact(const Graph* lhs, const Graph* rhs) {
    const bool lhsEnabled = lhs != nullptr && lhs->enabled;
    const bool rhsEnabled = rhs != nullptr && rhs->enabled;
    if (!lhsEnabled || !rhsEnabled) return lhsEnabled == rhsEnabled;
    if (lhs->rootSequenceId != rhs->rootSequenceId ||
        lhs->stepNodeCount != rhs->stepNodeCount ||
        lhs->sequenceCount != rhs->sequenceCount ||
        lhs->cycleSetCount != rhs->cycleSetCount ||
        lhs->stepNodeCount > lhs->stepNodes.size() ||
        rhs->stepNodeCount > rhs->stepNodes.size() ||
        lhs->sequenceCount > lhs->sequences.size() ||
        rhs->sequenceCount > rhs->sequences.size() ||
        lhs->cycleSetCount > lhs->cycleSets.size() ||
        rhs->cycleSetCount > rhs->cycleSets.size()) {
        return false;
    }
    for (uint16_t index = 0U; index < lhs->stepNodes.size(); ++index) {
        if (!sameStepNodeExact(lhs->stepNodes[index], rhs->stepNodes[index])) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < lhs->sequences.size(); ++index) {
        if (!sameSequence(lhs->sequences[index], rhs->sequences[index])) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < lhs->cycleSets.size(); ++index) {
        if (!sameCycleSet(lhs->cycleSets[index], rhs->cycleSets[index])) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameCcLaneDestinationExact(
    const SequencerCcLaneDestination& lhs,
    const SequencerCcLaneDestination& rhs
) {
    return lhs.controller == rhs.controller &&
           lhs.minimum == rhs.minimum &&
           lhs.maximum == rhs.maximum &&
           lhs.routePolicy == rhs.routePolicy &&
           lhs.pinnedPort == rhs.pinnedPort &&
           lhs.pinnedChannel == rhs.pinnedChannel;
}

FLASHMEM bool sameCcLaneExact(
    const SequencerCcLane& lhs,
    const SequencerCcLane& rhs
) {
    return lhs.occupied == rhs.occupied &&
           lhs.acceptedMacroConflict == rhs.acceptedMacroConflict &&
           lhs.conflictPolicy == rhs.conflictPolicy &&
           lhs.initialValue == rhs.initialValue &&
           lhs.lifecycleGeneration == rhs.lifecycleGeneration &&
           sameCcLaneDestinationExact(lhs.destination, rhs.destination) &&
           lhs.activeMask == rhs.activeMask &&
           lhs.values == rhs.values &&
           lhs.transitions == rhs.transitions;
}

FLASHMEM bool sameOptionalCcLaneBankExact(
    const SequencerCcLaneBank* lhs,
    const SequencerCcLaneBank* rhs
) {
    if ((lhs != nullptr && !validSequencerCcLaneBank(*lhs)) ||
        (rhs != nullptr && !validSequencerCcLaneBank(*rhs))) {
        return false;
    }
    const bool lhsEmpty = lhs == nullptr || sequencerCcLaneCount(*lhs) == 0U;
    const bool rhsEmpty = rhs == nullptr || sequencerCcLaneCount(*rhs) == 0U;
    if (lhsEmpty || rhsEmpty) return lhsEmpty == rhsEmpty;
    if (lhs->formatVersion != rhs->formatVersion ||
        lhs->revision != rhs->revision) {
        return false;
    }
    for (uint8_t lane = 0U; lane < lhs->lanes.size(); ++lane) {
        if (!sameCcLaneExact(lhs->lanes[lane], rhs->lanes[lane])) return false;
    }
    return true;
}

FLASHMEM bool cloneGraph(const Graph* source, GraphPtr& out) {
    out.reset();
    if (source == nullptr || !source->enabled) { return true; }

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

FLASHMEM bool reservePatternPayloadStorageForExpectedGraph(const SequencerPatternState& source,
                                                           bool graphMustBePresent, GraphPtr& graph,
                                                           SequencerHistoryCcLanePtr& ccLanes) {
    const auto* sourceGraph = graphView(source);
    const bool needsGraph = graphMustBePresent || sourceGraph != nullptr;
    if (!needsGraph) {
        graph.reset();
    } else if (!graph) {
        graph = core::app::makeExtmemUnique<Graph>();
        if (!graph) return false;
    }

    const auto* sourceCcLanes = source.ccLanes.get();
    const bool needsCcLanes =
        sourceCcLanes != nullptr && sequencerCcLaneCount(*sourceCcLanes) != 0U;
    if (!needsCcLanes) {
        ccLanes.reset();
    } else if (!ccLanes) {
        ccLanes = core::app::makeExtmemUnique<SequencerCcLaneBank>();
        if (!ccLanes) return false;
    }
    return true;
}

FLASHMEM bool reservePatternPayloadStorage(const SequencerPatternState& source, GraphPtr& graph,
                                           SequencerHistoryCcLanePtr& ccLanes) {
    return reservePatternPayloadStorageForExpectedGraph(source, false, graph, ccLanes);
}

FLASHMEM bool capturePatternPayloadUsingReservedStorage(const SequencerPatternState& source,
                                                         GraphPtr& graph,
                                                         SequencerHistoryCcLanePtr& ccLanes) {
    const auto* sourceGraph = graphView(source);
    const auto* sourceCcLanes = source.ccLanes.get();
    if (sourceGraph != nullptr && !graph) return false;
    if (sourceCcLanes != nullptr && !validSequencerCcLaneBank(*sourceCcLanes)) {
        return false;
    }
    const bool sourceCcLanesEmpty = sourceCcLanes == nullptr ||
        sequencerCcLaneCount(*sourceCcLanes) == 0U;
    if (!sourceCcLanesEmpty && !ccLanes) return false;

    // All fallible validation is complete. The reserved-owner publication
    // below is a direct, allocation-free write with a strong failure guarantee.
    if (sourceGraph == nullptr) graph.reset();
    else *graph = *sourceGraph;

    if (sourceCcLanesEmpty) {
        ccLanes.reset();
        return true;
    }
    *ccLanes = *sourceCcLanes;
    return true;
}

FLASHMEM bool patternStorageForCoalescedPlan(SequencerCoalescedPatternPayloadPlan plan,
                                             SequencerHistoryPatternStorage& storage) {
    switch (plan) {
        case SequencerCoalescedPatternPayloadPlan::FlatOnly:
            storage = SequencerHistoryPatternStorage::FlatOnly;
            return true;
        case SequencerCoalescedPatternPayloadPlan::FullCurrentPayload:
        case SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph:
            storage = SequencerHistoryPatternStorage::FullGraph;
            return true;
        default: return false;
    }
}

FLASHMEM bool planMatchesPatternStorage(SequencerCoalescedPatternPayloadPlan plan,
                                        SequencerHistoryPatternStorage storage) {
    SequencerHistoryPatternStorage expected{};
    return patternStorageForCoalescedPlan(plan, expected) && expected == storage;
}

FLASHMEM bool planRequiresPresentGraph(SequencerCoalescedPatternPayloadPlan plan) {
    return plan == SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph;
}

FLASHMEM const SequencerPatternState& patternSourceForTrack(const SequencerTrackBankState& bank,
                                                             const SequencerState& active,
                                                             uint8_t trackIndex) {
    return trackIndex == bank.activeTrackIndex() ? active.pattern : bank.track(trackIndex);
}

FLASHMEM bool captureCoalescedPatternBefore(const SequencerTrackBankState& bank,
                                            const SequencerState& active, uint8_t trackIndex,
                                            SequencerCoalescedPatternPayloadPlan plan,
                                            SequencerHistoryPatternSnapshot& out,
                                            SequencerHistoryGraphPtr& prospectiveGraph) {
    out.reset();
    prospectiveGraph.reset();
    const auto& source = patternSourceForTrack(bank, active, trackIndex);
    const auto* sourceGraph = graphView(source);

    // The prospective live Graph deliberately occupies the otherwise-absent
    // before-Graph allocation slot. A disabled but already-owned live Graph is
    // reusable by ensureGraphRoot(), so it must not trigger another owner.
    // CC capture follows it, preserving the frozen Change, Graph, CC order.
    if (sourceGraph != nullptr) {
        if (!cloneGraph(sourceGraph, out.graph)) return false;
    } else if (planRequiresPresentGraph(plan) && !source.graph) {
        prospectiveGraph = core::app::makeExtmemUnique<Graph>();
        if (!prospectiveGraph) return false;
    }

    if (!cloneSequencerCcLaneBank(out.ccLanes, source.ccLanes.get())) { return false; }
    captureSnapshot(source, out.flat);
    out.ccLaneRevision = source.ccLaneRevision.get();
    out.focusedStep = active.focusedStep.get();
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM bool capturePreparedSynchronizationPayloadUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& after,
    SequencerPreparedActiveTrackSynchronization& synchronization) {
    if (synchronization.storage == SequencerHistoryPatternStorage::FlatOnly) {
        const auto& target = bank.track(synchronization.trackIndex);
        if (!sameGraph(graphView(target), graphView(after.pattern)) ||
            !sameOptionalSequencerCcLaneBank(sequencerCcLaneView(target),
                                             sequencerCcLaneView(after.pattern))) {
            return false;
        }
        synchronization.payload.reset();
        return true;
    }
    return captureHistoryPatternPayloadUsingReservedStorage(after.pattern, synchronization.payload);
}

FLASHMEM void installGraph(SequencerPatternState& target, GraphPtr graph, uint32_t revision) {
    target.graph = std::move(graph);
    target.graphRevision.set(revision);
}

FLASHMEM uint8_t clampedFocusFor(const SequencerState& active, uint8_t focusedStep) {
    const uint8_t length = active.pattern.length.get();
    if (length == 0) { return 0; }
    return static_cast<uint8_t>(std::min<uint16_t>(focusedStep, length - 1U));
}

FLASHMEM void restoreFocus(SequencerState& active, uint8_t focusedStep) {
    const uint8_t focus = clampedFocusFor(active, focusedStep);
    active.focusedStep.set(focus);
    active.page.set(active.pageForStep(focus));
}

FLASHMEM void restoreActiveContentFocus(
    SequencerState& active,
    uint8_t focusedStep
) {
    const uint8_t length = activeContentLength(active);
    const uint8_t focus = length == 0U
        ? 0U
        : static_cast<uint8_t>(std::min<uint16_t>(focusedStep, length - 1U));
    active.focusedStep.set(focus);
    active.page.set(activeContentPageForStep(focus));
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM void restoreActiveStepProperty(SequencerState& active, StepProperty property) {
    active.activeStepProperty.set(property);
}

FLASHMEM bool sameFlatTrackBankSnapshot(const SequencerTrackBankSnapshot& lhs,
                                        const SequencerTrackBankSnapshot& rhs) {
    if (lhs.activeTrack != rhs.activeTrack || lhs.enabledMask != rhs.enabledMask ||
        !sameScaleSettings(lhs.projectScaleSettings, rhs.projectScaleSettings)) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!sameFlatPatternSnapshot(lhs.tracks[i], rhs.tracks[i])) return false;
    }

    return true;
}

FLASHMEM const Graph* effectiveTrackGraph(const SequencerHistoryTrackBankSnapshot& snapshot,
                                          uint8_t track) {
    const uint8_t activeTrack = SequencerTrackBankState::clampTrackIndex(snapshot.flat.activeTrack);
    return (track == activeTrack) ? snapshot.editorGraph.get() : snapshot.bankGraphs[track].get();
}

FLASHMEM uint8_t scopeLimit(SequencerHistoryScope scope) {
    switch (scope) {
        case SequencerHistoryScope::PatternOnly:
            return SequencerHistoryService::PATTERN_ENTRY_LIMIT;
        case SequencerHistoryScope::Structure:
            return SequencerHistoryService::STRUCTURE_ENTRY_LIMIT;
        case SequencerHistoryScope::FullBank:
        default: return SequencerHistoryService::FULL_BANK_ENTRY_LIMIT;
    }
}

FLASHMEM uint8_t
countScope(const std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
           uint8_t count, SequencerHistoryScope scope) {
    uint8_t result = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].scope == scope && entries[i].valid()) { ++result; }
    }
    return result;
}

constexpr size_t kExtmemAllocationOverheadEstimate = 16U;

FLASHMEM size_t graphRetainedBytes(const GraphPtr& graph) {
    return graph ? sizeof(Graph) + kExtmemAllocationOverheadEstimate : 0U;
}

FLASHMEM size_t ccLaneRetainedBytes(const SequencerHistoryCcLanePtr& lanes) {
    return lanes ? sizeof(SequencerCcLaneBank) + kExtmemAllocationOverheadEstimate : 0U;
}

FLASHMEM size_t patternSnapshotRetainedBytes(const SequencerHistoryPatternSnapshot& snapshot) {
    return graphRetainedBytes(snapshot.graph) + ccLaneRetainedBytes(snapshot.ccLanes);
}

FLASHMEM size_t trackBankSnapshotRetainedBytes(const SequencerHistoryTrackBankSnapshot& snapshot) {
    size_t bytes = graphRetainedBytes(snapshot.editorGraph);
    for (const auto& graph : snapshot.bankGraphs) { bytes += graphRetainedBytes(graph); }
    bytes += ccLaneRetainedBytes(snapshot.editorCcLanes);
    for (const auto& lanes : snapshot.bankCcLanes) { bytes += ccLaneRetainedBytes(lanes); }
    return bytes;
}

FLASHMEM size_t fullBankChangeRetainedBytes(const SequencerHistoryFullBankChange& change) {
    return sizeof(SequencerHistoryFullBankChange) + kExtmemAllocationOverheadEstimate +
           trackBankSnapshotRetainedBytes(change.before) +
           trackBankSnapshotRetainedBytes(change.after);
}

FLASHMEM size_t
structureSnapshotRetainedBytes(const SequencerHistoryTrackStructureSnapshot& snapshot) {
    size_t bytes = 0;
    for (const auto& track : snapshot.tracks) { bytes += patternSnapshotRetainedBytes(track); }
    return bytes;
}

FLASHMEM size_t structureChangeRetainedBytes(const SequencerHistoryTrackStructureChange& change) {
    size_t bytes = sizeof(SequencerHistoryTrackStructureChange) +
                   kExtmemAllocationOverheadEstimate +
                   structureSnapshotRetainedBytes(change.before) +
                   structureSnapshotRetainedBytes(change.after);
    if (change.macroStructure != nullptr) {
        bytes +=
            sizeof(SequencerHistoryMacroTrackStructurePayload) + kExtmemAllocationOverheadEstimate;
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

FLASHMEM size_t patternChangeRetainedBytes(const SequencerHistoryPatternChange& change) {
    return sizeof(SequencerHistoryPatternChange) + kExtmemAllocationOverheadEstimate +
           patternSnapshotRetainedBytes(change.before) + patternSnapshotRetainedBytes(change.after);
}

FLASHMEM size_t patternChangeAdmissionBytes(const SequencerHistoryPatternChange& change) {
    // FlatOnly payload owners are discarded before the entry is retained. Its
    // admission cost is therefore the normalized fixed-size change, while
    // retainedBytes() deliberately measures every owner that actually remains
    // so a broken normalization can never under-report the PSRAM budget.
    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        return sizeof(SequencerHistoryPatternChange) + kExtmemAllocationOverheadEstimate;
    }
    return patternChangeRetainedBytes(change);
}

FLASHMEM bool incomingEntryFitsRetainedBudget(size_t incomingBytes) {
    // commitPreparedEntry may evict every retained entry, so admission depends
    // only on whether the incoming entry fits the total retained-byte budget.
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
            return fullBankChangeRetainedBytes(*entry.fullBank);
        default: return 0;
    }
}

FLASHMEM size_t entriesRetainedBytes(
    const std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t count) {
    size_t bytes = 0;
    for (uint8_t i = 0; i < count; ++i) { bytes += entryRetainedBytes(entries[i]); }
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
        default: return 0U;
    }
}

FLASHMEM void removeEntryAt(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count, uint8_t index, const core::state::project::ProjectHistoryEventSink* sink) {
    if (index >= count) return;

    if (sink != nullptr) {
        sink->notifyEvicted(core::state::project::ProjectHistoryDomain::Sequencer,
                            projectHistoryIdentity(entries[index]));
    }

    for (uint8_t i = index; static_cast<uint8_t>(i + 1U) < count; ++i) {
        entries[i] = std::move(entries[i + 1U]);
    }

    --count;
    entries[count] = SequencerHistoryEntry{};
}

FLASHMEM void pruneOldestScope(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count, SequencerHistoryScope scope,
    const core::state::project::ProjectHistoryEventSink* sink) {
    if (countScope(entries, count, scope) < scopeLimit(scope)) { return; }

    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].scope == scope) {
            removeEntryAt(entries, count, i, sink);
            return;
        }
    }
}

FLASHMEM bool pushEntry(
    std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
    uint8_t& count, SequencerHistoryEntry entry,
    const core::state::project::ProjectHistoryEventSink* sink) {
    if (!entry.valid()) { return false; }

    pruneOldestScope(entries, count, entry.scope, sink);
    if (count >= SequencerHistoryService::ENTRY_LIMIT) { removeEntryAt(entries, count, 0, sink); }

    entries[count] = std::move(entry);
    ++count;
    return true;
}

FLASHMEM SequencerHistoryEntry
popBack(std::array<SequencerHistoryEntry, SequencerHistoryService::ENTRY_LIMIT>& entries,
        uint8_t& count) {
    SequencerHistoryEntry entry;
    if (count == 0) { return entry; }

    --count;
    entry = std::move(entries[count]);
    entries[count] = SequencerHistoryEntry{};
    return entry;
}

FLASHMEM void applyFlatSnapshotPreservingColdPayload(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot
) noexcept {
    auto ccLanes = std::move(target.ccLanes);
    const uint32_t ccLaneRevision = target.ccLaneRevision.get();
    applySnapshotPreservingGraph(target, snapshot);
    target.ccLanes = std::move(ccLanes);
    target.ccLaneRevision.set(ccLaneRevision);
}

FLASHMEM void applyFlatSnapshotToEditorPreservingColdPayload(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot
) noexcept {
    auto ccLanes = std::move(target.pattern.ccLanes);
    const uint32_t ccLaneRevision = target.pattern.ccLaneRevision.get();
    applySnapshotToEditorPreservingGraph(target, snapshot);
    target.pattern.ccLanes = std::move(ccLanes);
    target.pattern.ccLaneRevision.set(ccLaneRevision);
}

FLASHMEM bool applyFlatHistorySnapshotToTrack(SequencerTrackBankState& bank, SequencerState& active,
                                              uint8_t trackIndex,
                                              const SequencerHistoryPatternSnapshot& snapshot) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    const uint8_t activeTrack = bank.activeTrackIndex();

    if (targetTrack != activeTrack) {
        auto& target = bank.track(targetTrack);
        applyFlatSnapshotPreservingColdPayload(target, snapshot.flat);
        synchronizeHistoryPatternRevisionSignals(
            target, snapshot.flat, snapshot.ccLaneRevision);
        return true;
    }

    applyFlatSnapshotToEditorPreservingColdPayload(active, snapshot.flat);
    synchronizeHistoryPatternRevisionSignals(
        active.pattern, snapshot.flat, snapshot.ccLaneRevision);
    auto& bankTarget = bank.track(activeTrack);
    applyFlatSnapshotPreservingColdPayload(bankTarget, snapshot.flat);
    synchronizeHistoryPatternRevisionSignals(
        bankTarget, snapshot.flat, snapshot.ccLaneRevision);
    restoreFocus(active, snapshot.focusedStep);
    return true;
}

FLASHMEM bool applyEntrySnapshot(SequencerHistoryEntry& entry, bool after,
                                 SequencerTrackBankState& bank, SequencerState& active) {
    if (active.stepContentDraft.rejectTransitionIfActive(
            SequencerStepContentDraftBlockedTransition::HISTORY)) {
        return false;
    }

    if (entry.scope == SequencerHistoryScope::PatternOnly) {
        if (!entry.pattern) return false;
        const auto& snapshot = after ? entry.pattern->after : entry.pattern->before;
        if (entry.pattern->storage == SequencerHistoryPatternStorage::FlatOnly) {
            return applyFlatHistorySnapshotToTrack(bank, active, entry.pattern->trackIndex,
                                                   snapshot);
        }
        return applyHistorySnapshotToTrack(bank, active, entry.pattern->trackIndex, snapshot);
    }

    // Structure owns a Macro-aware prepared replay lifecycle and may never
    // escape through this generic allocating path.
    if (entry.scope == SequencerHistoryScope::Structure) return false;

    if (!entry.fullBank) return false;
    return applyHistorySnapshot(bank, active,
                                after ? entry.fullBank->after : entry.fullBank->before);
}

FLASHMEM SequencerHistoryDescriptor descriptorForEntry(const SequencerHistoryEntry& entry) {
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

FLASHMEM bool captureHistorySnapshot(const SequencerState& source,
                                     SequencerHistoryPatternSnapshot& out) {
    out.reset();
    return reserveHistorySnapshotStorage(source, out) &&
           captureHistorySnapshotUsingReservedStorage(source, out);
}

FLASHMEM void synchronizeHistoryPatternRevisionSignals(SequencerPatternState& target,
                                                       const SequencerPatternSnapshot& snapshot,
                                                       uint32_t ccLaneRevision) {
    target.stepDataRevision.set(snapshot.stepDataRevision);
    target.patternVariationRevision.set(snapshot.patternVariationRevision);
    target.patternScaleRevision.set(snapshot.patternScaleRevision);
    target.patternTimingRevision.set(snapshot.patternTimingRevision);
    target.graphRevision.set(snapshot.graphRevision);
    target.ccLaneRevision.set(ccLaneRevision);
}

FLASHMEM bool reserveHistoryPatternPayloadStorage(const SequencerPatternState& source,
                                                  SequencerHistoryPatternPayloadStorage& storage) {
    return reservePatternPayloadStorage(source, storage.graph, storage.ccLanes);
}

FLASHMEM bool captureHistoryPatternPayloadUsingReservedStorage(
    const SequencerPatternState& source, SequencerHistoryPatternPayloadStorage& storage) {
    return capturePatternPayloadUsingReservedStorage(source, storage.graph, storage.ccLanes);
}

FLASHMEM bool reserveHistorySnapshotStorage(const SequencerState& source,
                                            SequencerHistoryPatternSnapshot& snapshot) {
    return reservePatternPayloadStorage(source.pattern, snapshot.graph, snapshot.ccLanes);
}

FLASHMEM bool captureHistorySnapshotUsingReservedStorage(const SequencerState& source,
                                                         SequencerHistoryPatternSnapshot& out) {
    if (!capturePatternPayloadUsingReservedStorage(source.pattern, out.graph, out.ccLanes)) {
        return false;
    }
    captureSnapshot(source.pattern, out.flat);
    out.ccLaneRevision = source.pattern.ccLaneRevision.get();
    out.focusedStep = source.focusedStep.get();
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM bool reserveHistorySnapshotGraphStorage(SequencerHistoryPatternSnapshot& snapshot) {
    if (snapshot.graph) return true;
    snapshot.graph = core::app::makeExtmemUnique<Graph>();
    return static_cast<bool>(snapshot.graph);
}

FLASHMEM bool captureHistorySnapshotUsingReservedGraph(const SequencerState& source,
                                                       SequencerHistoryPatternSnapshot& out) {
    captureSnapshot(source.pattern, out.flat);
    out.ccLaneRevision = source.pattern.ccLaneRevision.get();
    out.focusedStep = source.focusedStep.get();
    if (!captureGraphUsingReservedStorage(graphView(source.pattern), out.graph) ||
        !captureSequencerCcLaneBankUsingReservedStorage(source.pattern.ccLanes.get(),
                                                        out.ccLanes)) {
        return false;
    }
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM void captureFlatHistorySnapshot(const SequencerState& source,
                                         SequencerHistoryPatternSnapshot& out) {
    out.reset();
    captureSnapshot(source.pattern, out.flat);
    out.ccLaneRevision = source.pattern.ccLaneRevision.get();
    out.focusedStep = source.focusedStep.get();
    out.ccLanesCaptured = false;
}

FLASHMEM bool captureHistorySnapshot(const SequencerTrackBankState& bank,
                                     const SequencerState& active, uint8_t trackIndex,
                                     SequencerHistoryPatternSnapshot& out) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) { return captureHistorySnapshot(active, out); }

    out.reset();
    return reserveHistorySnapshotStorage(bank, active, targetTrack, out) &&
           captureHistorySnapshotUsingReservedStorage(bank, active, targetTrack, out);
}

FLASHMEM bool reserveHistorySnapshotStorage(const SequencerTrackBankState& bank,
                                            const SequencerState& active, uint8_t trackIndex,
                                            SequencerHistoryPatternSnapshot& snapshot) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    const auto& source =
        targetTrack == bank.activeTrackIndex() ? active.pattern : bank.track(targetTrack);
    return reservePatternPayloadStorage(source, snapshot.graph, snapshot.ccLanes);
}

FLASHMEM bool captureHistorySnapshotUsingReservedStorage(const SequencerTrackBankState& bank,
                                                         const SequencerState& active,
                                                         uint8_t trackIndex,
                                                         SequencerHistoryPatternSnapshot& out) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) {
        return captureHistorySnapshotUsingReservedStorage(active, out);
    }

    const auto& source = bank.track(targetTrack);
    if (!capturePatternPayloadUsingReservedStorage(source, out.graph, out.ccLanes)) {
        return false;
    }
    captureSnapshot(source, out.flat);
    out.ccLaneRevision = source.ccLaneRevision.get();
    out.focusedStep = active.focusedStep.get();
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM bool captureHistorySnapshotUsingReservedGraph(const SequencerTrackBankState& bank,
                                                       const SequencerState& active,
                                                       uint8_t trackIndex,
                                                       SequencerHistoryPatternSnapshot& out) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) {
        return captureHistorySnapshotUsingReservedGraph(active, out);
    }

    const auto& source = bank.track(targetTrack);
    captureSnapshot(source, out.flat);
    out.ccLaneRevision = source.ccLaneRevision.get();
    out.focusedStep = active.focusedStep.get();
    if (!captureGraphUsingReservedStorage(graphView(source), out.graph) ||
        !captureSequencerCcLaneBankUsingReservedStorage(source.ccLanes.get(), out.ccLanes)) {
        return false;
    }
    out.ccLanesCaptured = true;
    return true;
}

FLASHMEM void captureFlatHistorySnapshot(const SequencerTrackBankState& bank,
                                         const SequencerState& active, uint8_t trackIndex,
                                         SequencerHistoryPatternSnapshot& out) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    if (targetTrack == bank.activeTrackIndex()) {
        captureFlatHistorySnapshot(active, out);
        return;
    }

    out.reset();
    captureSnapshot(bank.track(targetTrack), out.flat);
    out.ccLaneRevision = bank.track(targetTrack).ccLaneRevision.get();
    out.focusedStep = active.focusedStep.get();
    out.ccLanesCaptured = false;
}

FLASHMEM bool captureHistorySnapshot(const SequencerTrackBankState& bank,
                                     const SequencerState& active,
                                     SequencerHistoryTrackBankSnapshot& out) {
    out.reset();
    return reserveHistoryTrackBankSnapshotStorage(bank, active, out) &&
           captureHistoryTrackBankSnapshotUsingReservedStorage(bank, active, out);
}

FLASHMEM bool reserveHistoryTrackBankSnapshotStorage(const SequencerTrackBankState& bank,
                                                     const SequencerState& active,
                                                     SequencerHistoryTrackBankSnapshot& snapshot) {
    const uint8_t activeTrack = bank.activeTrackIndex();
    snapshot.flat.activeTrack = activeTrack;
    if (!reservePatternPayloadStorage(active.pattern, snapshot.editorGraph,
                                      snapshot.editorCcLanes)) {
        return false;
    }
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (i == activeTrack) {
            snapshot.bankGraphs[i].reset();
            snapshot.bankCcLanes[i].reset();
            continue;
        }
        if (!reservePatternPayloadStorage(bank.track(i), snapshot.bankGraphs[i],
                                          snapshot.bankCcLanes[i])) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool captureHistoryTrackBankSnapshotUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& active,
    SequencerHistoryTrackBankSnapshot& out) {
    const uint8_t activeTrack = bank.activeTrackIndex();
    if (out.flat.activeTrack != activeTrack) return false;
    if (!capturePatternPayloadUsingReservedStorage(active.pattern, out.editorGraph,
                                                   out.editorCcLanes)) {
        return false;
    }
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (i == activeTrack) {
            out.bankGraphs[i].reset();
            out.bankCcLanes[i].reset();
            continue;
        }
        if (!capturePatternPayloadUsingReservedStorage(bank.track(i), out.bankGraphs[i],
                                                       out.bankCcLanes[i])) {
            return false;
        }
    }

    captureTrackBankSnapshot(bank, active, out.flat);
    out.focusedStep = active.focusedStep.get();
    out.activeStepProperty = active.activeStepProperty.get();
    return true;
}

FLASHMEM bool reservePreparedActiveTrackSynchronization(
    const SequencerTrackBankState& bank, const SequencerState& after, uint8_t trackIndex,
    SequencerHistoryPatternStorage storage,
    SequencerPreparedActiveTrackSynchronization& synchronization) {
    synchronization.reset();
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    synchronization.trackIndex = targetTrack;
    synchronization.storage = storage;
    if (targetTrack != bank.activeTrackIndex()) return false;
    if (storage == SequencerHistoryPatternStorage::FlatOnly) {
        const auto& target = bank.track(targetTrack);
        synchronization.reserved =
            sameGraph(graphView(target), graphView(after.pattern)) &&
            sameOptionalSequencerCcLaneBank(sequencerCcLaneView(target),
                                            sequencerCcLaneView(after.pattern));
        return synchronization.reserved;
    }
    synchronization.reserved =
        reserveHistoryPatternPayloadStorage(after.pattern, synchronization.payload);
    return synchronization.reserved;
}

FLASHMEM bool reservePreparedActiveTrackSynchronization(
    const SequencerTrackBankState& bank, const SequencerState& after, uint8_t trackIndex,
    SequencerCoalescedPatternPayloadPlan plan,
    SequencerPreparedActiveTrackSynchronization& synchronization) {
    SequencerHistoryPatternStorage storage{};
    if (!patternStorageForCoalescedPlan(plan, storage)) {
        synchronization.reset();
        return false;
    }
    if (!planRequiresPresentGraph(plan)) {
        return reservePreparedActiveTrackSynchronization(bank, after, trackIndex, storage,
                                                         synchronization);
    }

    synchronization.reset();
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    synchronization.trackIndex = targetTrack;
    synchronization.storage = storage;
    if (targetTrack != bank.activeTrackIndex()) return false;
    synchronization.reserved = reservePatternPayloadStorageForExpectedGraph(
        after.pattern, true, synchronization.payload.graph, synchronization.payload.ccLanes);
    return synchronization.reserved;
}

FLASHMEM bool preparedActiveTrackSynchronizationMatches(
    const SequencerTrackBankState& bank,
    const SequencerPreparedActiveTrackSynchronization& synchronization) {
    return synchronization.reserved &&
           synchronization.trackIndex < SequencerTrackBankState::TRACK_COUNT &&
           synchronization.trackIndex == bank.activeTrackIndex();
}

FLASHMEM bool capturePreparedActiveTrackSynchronizationUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& after,
    SequencerPreparedActiveTrackSynchronization& synchronization) {
    if (!preparedActiveTrackSynchronizationMatches(bank, synchronization)) { return false; }
    if (synchronization.captured) return false;
    synchronization.captured =
        capturePreparedSynchronizationPayloadUsingReservedStorage(bank, after, synchronization);
    return synchronization.captured;
}

FLASHMEM bool refreshPreparedActiveTrackSynchronizationUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& after,
    SequencerPreparedActiveTrackSynchronization& synchronization) {
    if (!preparedActiveTrackSynchronizationMatches(bank, synchronization)) {
        // A stale previously-captured payload must never remain publishable
        // after a failed continuation refresh.
        synchronization.captured = false;
        return false;
    }
    synchronization.captured = false;
    synchronization.captured =
        capturePreparedSynchronizationPayloadUsingReservedStorage(bank, after, synchronization);
    return synchronization.captured;
}

namespace {

FLASHMEM void publishPreparedActiveTrackSynchronizationUsingFlat(
    SequencerTrackBankState& bank, const SequencerState& active,
    const SequencerPatternSnapshot& flat, uint32_t ccLaneRevision,
    bool ccLanesCaptured, bool validateFlatPayload,
    SequencerPreparedActiveTrackSynchronization synchronization) {
    if (!synchronization.captured ||
        !preparedActiveTrackSynchronizationMatches(bank, synchronization)) {
        return;
    }
    auto& target = bank.track(synchronization.trackIndex);
    if (synchronization.storage == SequencerHistoryPatternStorage::FlatOnly) {
        if (validateFlatPayload &&
            (!sameGraph(graphView(target), graphView(active.pattern)) ||
             !sameOptionalSequencerCcLaneBank(sequencerCcLaneView(target),
                                              sequencerCcLaneView(active.pattern)))) {
            return;
        }
        applyFlatSnapshotPreservingColdPayload(target, flat);
        synchronizeHistoryPatternRevisionSignals(target, flat, ccLaneRevision);
        return;
    }

    installTrackContentSnapshotWithOwnedGraph(
        target, flat, std::move(synchronization.payload.graph));
    if (ccLanesCaptured) {
        installSequencerCcLaneBank(
            target, std::move(synchronization.payload.ccLanes));
    }
    synchronizeHistoryPatternRevisionSignals(target, flat, ccLaneRevision);
}

}  // namespace

FLASHMEM void publishPreparedActiveTrackSynchronization(
    SequencerTrackBankState& bank, const SequencerState& active,
    SequencerPreparedActiveTrackSynchronization synchronization) {
    SequencerPatternSnapshot flat{};
    captureSnapshot(active.pattern, flat);
    publishPreparedActiveTrackSynchronizationUsingFlat(
        bank,
        active,
        flat,
        active.pattern.ccLaneRevision.get(),
        true,
        true,
        std::move(synchronization));
}

FLASHMEM void publishPreparedActiveTrackSynchronization(
    SequencerTrackBankState& bank, const SequencerState& active,
    const SequencerHistoryPatternSnapshot& sealedAfter,
    SequencerPreparedActiveTrackSynchronization synchronization) {
    publishPreparedActiveTrackSynchronizationUsingFlat(
        bank,
        active,
        sealedAfter.flat,
        sealedAfter.ccLaneRevision,
        sealedAfter.ccLanesCaptured,
        false,
        std::move(synchronization));
}

FLASHMEM bool applyHistorySnapshot(SequencerTrackBankState& bank, SequencerState& active,
                                   const SequencerHistoryPatternSnapshot& snapshot) {
    return applyHistorySnapshotToTrack(bank, active, bank.activeTrackIndex(), snapshot);
}

FLASHMEM bool applyHistorySnapshotToEditor(SequencerState& active,
                                           const SequencerHistoryPatternSnapshot& snapshot) {
    GraphPtr editorGraph;
    SequencerHistoryCcLanePtr editorCcLanes;
    if (!cloneGraph(snapshot.graph, editorGraph)) { return false; }
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

FLASHMEM bool applyHistorySnapshotToTrack(SequencerTrackBankState& bank, SequencerState& active,
                                          uint8_t trackIndex,
                                          const SequencerHistoryPatternSnapshot& snapshot) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    const uint8_t activeTrack = bank.activeTrackIndex();

    if (targetTrack != activeTrack) {
        GraphPtr bankGraph;
        SequencerHistoryCcLanePtr bankCcLanes;
        if (!cloneGraph(snapshot.graph, bankGraph)) { return false; }
        if (snapshot.ccLanesCaptured &&
            !cloneSequencerCcLaneBank(bankCcLanes, snapshot.ccLanes.get())) {
            return false;
        }

        applySnapshot(bank.track(targetTrack), snapshot.flat);
        installGraph(bank.track(targetTrack), std::move(bankGraph), snapshot.flat.graphRevision);
        if (snapshot.ccLanesCaptured) {
            installSequencerCcLaneBank(bank.track(targetTrack), std::move(bankCcLanes));
        }
        return true;
    }

    GraphPtr editorGraph;
    GraphPtr bankGraph;
    SequencerHistoryCcLanePtr editorCcLanes;
    SequencerHistoryCcLanePtr bankCcLanes;
    if (!cloneGraph(snapshot.graph, editorGraph) || !cloneGraph(snapshot.graph, bankGraph)) {
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

FLASHMEM bool applyHistorySnapshot(SequencerTrackBankState& bank, SequencerState& active,
                                   const SequencerHistoryTrackBankSnapshot& snapshot) {
    std::array<GraphPtr, SequencerTrackBankState::TRACK_COUNT> bankGraphs{};
    GraphPtr editorGraph;
    std::array<SequencerHistoryCcLanePtr, SequencerTrackBankState::TRACK_COUNT> bankCcLanes{};
    SequencerHistoryCcLanePtr editorCcLanes;

    const uint8_t activeTrack = SequencerTrackBankState::clampTrackIndex(snapshot.flat.activeTrack);

    if (!cloneGraph(snapshot.editorGraph, editorGraph)) { return false; }
    if (!cloneSequencerCcLaneBank(editorCcLanes, snapshot.editorCcLanes.get())) { return false; }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (i == activeTrack) continue;
        if (!cloneGraph(snapshot.bankGraphs[i], bankGraphs[i])) { return false; }
        if (!cloneSequencerCcLaneBank(bankCcLanes[i], snapshot.bankCcLanes[i].get())) {
            return false;
        }
    }

    applyTrackBankSnapshot(bank, active, snapshot.flat);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (i == activeTrack) continue;
        installGraph(bank.track(i), std::move(bankGraphs[i]),
                     snapshot.flat.tracks[i].graphRevision);
        installSequencerCcLaneBank(bank.track(i), std::move(bankCcLanes[i]));
        synchronizeHistoryPatternRevisionSignals(
            bank.track(i), snapshot.flat.tracks[i], bank.track(i).ccLaneRevision.get());
    }

    bank.track(activeTrack).graph.reset();
    bank.track(activeTrack).ccLanes.reset();
    installGraph(active.pattern, std::move(editorGraph),
                 snapshot.flat.tracks[activeTrack].graphRevision);
    installSequencerCcLaneBank(active.pattern, std::move(editorCcLanes));
    synchronizeHistoryPatternRevisionSignals(
        active.pattern, snapshot.flat.tracks[activeTrack], active.pattern.ccLaneRevision.get());
    restoreFocus(active, snapshot.focusedStep);
    restoreActiveStepProperty(active, snapshot.activeStepProperty);
    return true;
}

FLASHMEM bool sameMusicalHistorySnapshot(const SequencerHistoryPatternSnapshot& lhs,
                                          const SequencerHistoryPatternSnapshot& rhs) {
    return sameFlatPatternSnapshot(lhs.flat, rhs.flat) &&
           sameGraph(lhs.graph.get(), rhs.graph.get()) &&
           (!lhs.ccLanesCaptured || !rhs.ccLanesCaptured ||
            sameOptionalSequencerCcLaneBank(lhs.ccLanes.get(), rhs.ccLanes.get()));
}

FLASHMEM bool liveHistoryPatternSnapshotMatches(
    const SequencerPatternState& live,
    const SequencerHistoryPatternSnapshot& snapshot
) {
    const auto& flat = snapshot.flat;
    if (live.length.get() != flat.length ||
        live.playStart != flat.playStart ||
        live.loopStart != flat.loopStart ||
        live.loopEnd != flat.loopEnd ||
        live.stepsPerBeat.get() != flat.stepsPerBeat ||
        live.enabledMask.get() != flat.enabledMask ||
        live.stepDataRevision.get() != flat.stepDataRevision ||
        live.patternVariationRevision.get() != flat.patternVariationRevision ||
        live.patternScaleRevision.get() != flat.patternScaleRevision ||
        live.patternTimingRevision.get() != flat.patternTimingRevision ||
        live.graphRevision.get() != flat.graphRevision ||
        live.ccLaneRevision.get() != snapshot.ccLaneRevision ||
        live.swingOffsetPercent.get() != flat.swingOffsetPercent ||
        live.patternNudgePercent.get() != flat.patternNudgePercent ||
        live.effectiveSwingPercent(0U) != flat.effectiveSwingPercent ||
        !sameVariationRangesExact(live.variationRanges, flat.variationRanges) ||
        live.scalePolicy != flat.scalePolicy ||
        !sameScaleSettingsExact(live.scaleOverride, flat.scaleOverride) ||
        live.pitchEditMode != flat.pitchEditMode ||
        live.note != flat.note ||
        live.velocity != flat.velocity ||
        live.gate != flat.gate ||
        live.nudge != flat.nudge ||
        live.probability != flat.probability) {
        return false;
    }

    const auto effectiveScale = resolveEffectiveScaleSettings(
        {},
        live.scalePolicy,
        live.scaleOverride
    );
    if (!sameScaleSettingsExact(effectiveScale, flat.effectiveScaleSettings) ||
        !sameGraphExact(graphView(live), snapshot.graph.get())) {
        return false;
    }
    if (!snapshot.ccLanesCaptured) return false;
    return sameOptionalCcLaneBankExact(
        sequencerCcLaneView(live),
        snapshot.ccLanes.get()
    );
}

FLASHMEM bool preparedHistoryPatternAfterMatchesTrack(const SequencerTrackBankState& bank,
                                                      const SequencerState& active,
                                                      uint8_t trackIndex,
                                                      const SequencerHistoryPatternSnapshot& after,
                                                      SequencerHistoryPatternStorage storage) {
    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    const auto& target = patternSourceForTrack(bank, active, targetTrack);
    SequencerPatternSnapshot flat{};
    captureSnapshot(target, flat);
    if (!sameFlatPatternSnapshot(flat, after.flat) ||
        target.graphRevision.get() != after.flat.graphRevision ||
        target.ccLaneRevision.get() != after.ccLaneRevision) {
        return false;
    }
    // Track projection setters deliberately advance the four flat revision
    // signals. Exact musical bytes, cold-payload revisions and the Core-owned
    // payload-owner proof establish an equivalent sealed state even after an
    // A->B->A Track round trip. Core normalizes all revisions to `after`
    // immediately before publication.
    if (storage == SequencerHistoryPatternStorage::FlatOnly) {
        if (targetTrack == bank.activeTrackIndex()) {
            const auto& bankTarget = bank.track(targetTrack);
            if (!sameGraph(graphView(bankTarget), graphView(active.pattern)) ||
                !sameOptionalSequencerCcLaneBank(sequencerCcLaneView(bankTarget),
                                                 sequencerCcLaneView(active.pattern))) {
                return false;
            }
        }
        return true;
    }
    if (!sameGraph(graphView(target), after.graph.get())) return false;
    if (!after.ccLanesCaptured) {
        const auto* targetCcLanes = sequencerCcLaneView(target);
        return after.ccLanes == nullptr &&
               (targetCcLanes == nullptr ||
                sequencerCcLaneCount(*targetCcLanes) == 0U);
    }
    return sameOptionalSequencerCcLaneBank(
        sequencerCcLaneView(target), after.ccLanes.get());
}

FLASHMEM bool sameMusicalHistorySnapshot(const SequencerHistoryTrackBankSnapshot& lhs,
                                         const SequencerHistoryTrackBankSnapshot& rhs) {
    if (!sameFlatTrackBankSnapshot(lhs.flat, rhs.flat) ||
        !sameGraph(lhs.editorGraph.get(), rhs.editorGraph.get()) ||
        !sameOptionalSequencerCcLaneBank(lhs.editorCcLanes.get(), rhs.editorCcLanes.get())) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (!sameGraph(effectiveTrackGraph(lhs, i), effectiveTrackGraph(rhs, i))) { return false; }
        const auto* lhsLanes =
            i == lhs.flat.activeTrack ? lhs.editorCcLanes.get() : lhs.bankCcLanes[i].get();
        const auto* rhsLanes =
            i == rhs.flat.activeTrack ? rhs.editorCcLanes.get() : rhs.bankCcLanes[i].get();
        if (!sameOptionalSequencerCcLaneBank(lhsLanes, rhsLanes)) return false;
    }

    return true;
}

FLASHMEM SequencerHistoryPatternChangePtr prepareHistoryPatternChangeBefore(
    const SequencerTrackBankState& bank, const SequencerState& active, uint8_t trackIndex,
    SequencerHistoryPatternStorage storage, SequencerHistoryDescriptor descriptor) {
    auto change = core::app::makeExtmemUnique<SequencerHistoryPatternChange>();
    if (!change) return nullptr;

    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    change->trackIndex = targetTrack;
    change->storage = storage;
    descriptor.trackIndex = targetTrack;
    change->descriptor = descriptor;

    bool captured = true;
    if (storage == SequencerHistoryPatternStorage::FlatOnly) {
        captureFlatHistorySnapshot(bank, active, targetTrack, change->before);
    } else {
        captured = captureHistorySnapshot(bank, active, targetTrack, change->before);
    }
    return captured ? std::move(change) : SequencerHistoryPatternChangePtr{};
}

FLASHMEM SequencerHistoryPatternChangePtr prepareHistoryPatternChangeBefore(
    const SequencerTrackBankState& bank, const SequencerState& active, uint8_t trackIndex,
    SequencerCoalescedPatternPayloadPlan plan, SequencerHistoryGraphPtr& prospectiveGraph,
    SequencerHistoryDescriptor descriptor) {
    SequencerHistoryPatternStorage storage{};
    if (!patternStorageForCoalescedPlan(plan, storage)) {
        prospectiveGraph.reset();
        return nullptr;
    }
    if (!planRequiresPresentGraph(plan)) {
        prospectiveGraph.reset();
        return prepareHistoryPatternChangeBefore(bank, active, trackIndex, storage, descriptor);
    }

    auto change = core::app::makeExtmemUnique<SequencerHistoryPatternChange>();
    if (!change) {
        prospectiveGraph.reset();
        return nullptr;
    }

    const uint8_t targetTrack = SequencerTrackBankState::clampTrackIndex(trackIndex);
    change->trackIndex = targetTrack;
    change->storage = storage;
    descriptor.trackIndex = targetTrack;
    change->descriptor = descriptor;
    if (!captureCoalescedPatternBefore(bank, active, targetTrack, plan, change->before,
                                       prospectiveGraph)) {
        return nullptr;
    }
    return change;
}

FLASHMEM bool reservePreparedHistoryPatternAfter(const SequencerTrackBankState& bank,
                                                 const SequencerState& active,
                                                 SequencerHistoryPatternChange& change) {
    if (change.trackIndex >= SequencerTrackBankState::TRACK_COUNT ||
        change.descriptor.trackIndex != change.trackIndex) {
        return false;
    }
    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        change.after.reset();
        return true;
    }
    return reserveHistorySnapshotStorage(bank, active, change.trackIndex, change.after);
}

FLASHMEM bool reservePreparedHistoryPatternAfter(const SequencerTrackBankState& bank,
                                                 const SequencerState& active,
                                                 SequencerHistoryPatternChange& change,
                                                 SequencerCoalescedPatternPayloadPlan plan) {
    if (!planMatchesPatternStorage(plan, change.storage)) return false;
    if (!planRequiresPresentGraph(plan)) {
        return reservePreparedHistoryPatternAfter(bank, active, change);
    }
    if (change.trackIndex >= SequencerTrackBankState::TRACK_COUNT ||
        change.descriptor.trackIndex != change.trackIndex) {
        return false;
    }

    change.after.reset();
    const auto& source = patternSourceForTrack(bank, active, change.trackIndex);
    return reservePatternPayloadStorageForExpectedGraph(source, true, change.after.graph,
                                                        change.after.ccLanes);
}

FLASHMEM bool capturePreparedHistoryPatternAfterUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& active,
    SequencerHistoryPatternChange& change) {
    if (change.trackIndex >= SequencerTrackBankState::TRACK_COUNT ||
        change.descriptor.trackIndex != change.trackIndex) {
        return false;
    }
    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        captureFlatHistorySnapshot(bank, active, change.trackIndex, change.after);
        return true;
    }
    return captureHistorySnapshotUsingReservedStorage(bank, active, change.trackIndex,
                                                      change.after);
}

FLASHMEM bool restorePreparedHistoryPatternBefore(
    SequencerTrackBankState& bank, SequencerState& active,
    SequencerHistoryPatternChange& change,
    bool prospectiveGraphInstalled) {
    if (change.trackIndex >= SequencerTrackBankState::TRACK_COUNT) return false;

    auto& before = change.before;
    if (change.trackIndex != bank.activeTrackIndex()) {
        auto& target = bank.track(change.trackIndex);
        if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
            const bool graphOwnerPresent = change.preparedGraphOwnerProofPresent();
            const bool ccOwnerPresent = change.preparedCcLaneOwnerProofPresent();
            if ((graphOwnerPresent &&
                 (!target.graph || !change.preparedGraphOwnerProofMatches(target))) ||
                (ccOwnerPresent &&
                 (!target.ccLanes || !change.preparedCcLaneOwnerProofMatches(target)))) {
                return false;
            }

            // Validate every owner before the first rollback write. A failed
            // public abort must leave the live state untouched and retryable.
            if (!graphOwnerPresent) target.graph.reset();
            if (!ccOwnerPresent) target.ccLanes.reset();
            applyFlatSnapshotPreservingColdPayload(target, before.flat);
            synchronizeHistoryPatternRevisionSignals(
                target, before.flat, before.ccLaneRevision);
            return true;
        }

        if (!before.ccLanesCaptured &&
            (before.ccLanes != nullptr ||
             (target.ccLanes != nullptr &&
              sequencerCcLaneCount(*target.ccLanes) != 0U))) {
            return false;
        }
        const bool graphOwnerRequired =
            before.graph != nullptr ||
            (!prospectiveGraphInstalled && change.preparedGraphOwnerProofPresent());
        const bool ccOwnerRequired =
            before.ccLanes != nullptr || change.preparedCcLaneOwnerProofPresent();
        if ((graphOwnerRequired &&
             (!target.graph || !change.preparedGraphOwnerProofMatches(target))) ||
            (ccOwnerRequired &&
             (!target.ccLanes || !change.preparedCcLaneOwnerProofMatches(target)))) {
            return false;
        }

        // From this point every remaining operation is allocation-free and
        // non-fallible: no partial rollback can escape as AbortOutcome::Failed.
        applySnapshotPreservingGraph(target, before.flat);
        if (before.graph) {
            *target.graph = *before.graph;
            before.graph.reset();
        } else if (prospectiveGraphInstalled) {
            target.graph.reset();
        } else if (change.preparedGraphOwnerProofPresent()) {
            target.graph->reset();
        } else {
            target.graph.reset();
        }
        if (before.ccLanes) {
            *target.ccLanes = *before.ccLanes;
            before.ccLanes.reset();
        } else if (change.preparedCcLaneOwnerProofPresent()) {
            // Empty CC owners are not mutated by Page operations. Preserve
            // both their exact bytes and address rather than canonicalizing.
        } else {
            target.ccLanes.reset();
        }
        synchronizeHistoryPatternRevisionSignals(
            target, before.flat, before.ccLaneRevision);
        return true;
    }

    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        const bool graphOwnerPresent =
            change.preparedGraphOwnerProofPresent();
        const bool ccOwnerPresent =
            change.preparedCcLaneOwnerProofPresent();
        if ((graphOwnerPresent &&
             (!active.pattern.graph ||
              !change.preparedGraphOwnerProofMatches(active.pattern))) ||
            (ccOwnerPresent &&
             (!active.pattern.ccLanes ||
              !change.preparedCcLaneOwnerProofMatches(active.pattern)))) {
            return false;
        }

        // Flat prepared mutations do not own cold Graph/CC payload bytes.
        // Preserve every owner proven at begin byte-for-byte, including a
        // canonical disabled Graph or empty CC bank whose active-track scratch
        // slot is intentionally null. Only discard an owner that appeared
        // after a proof of absence.
        if (!graphOwnerPresent) active.pattern.graph.reset();
        if (!ccOwnerPresent) active.pattern.ccLanes.reset();
        applyFlatSnapshotToEditorPreservingColdPayload(active, before.flat);
        synchronizeHistoryPatternRevisionSignals(active.pattern, before.flat,
                                                 before.ccLaneRevision);
        restoreActiveContentFocus(active, before.focusedStep);
        return true;
    }

    if (!before.ccLanesCaptured &&
        (before.ccLanes != nullptr ||
         (active.pattern.ccLanes != nullptr &&
          sequencerCcLaneCount(*active.pattern.ccLanes) != 0U))) {
        return false;
    }
    const bool graphOwnerRequired =
        before.graph != nullptr ||
        (!prospectiveGraphInstalled && change.preparedGraphOwnerProofPresent());
    const bool ccOwnerRequired =
        before.ccLanes != nullptr || change.preparedCcLaneOwnerProofPresent();
    if ((graphOwnerRequired &&
         (!active.pattern.graph ||
          !change.preparedGraphOwnerProofMatches(active.pattern))) ||
        (ccOwnerRequired &&
         (!active.pattern.ccLanes ||
          !change.preparedCcLaneOwnerProofMatches(active.pattern)))) {
        return false;
    }

    // Validate the complete Graph+CC owner set before restoring any flat or
    // cold payload byte. The public abort is therefore failure-atomic.
    applySnapshotToEditorPreservingGraph(active, before.flat);
    if (before.graph) {
        *active.pattern.graph = *before.graph;
        before.graph.reset();
    } else if (prospectiveGraphInstalled) {
        active.pattern.graph.reset();
    } else if (change.preparedGraphOwnerProofPresent()) {
        active.pattern.graph->reset();
    } else {
        active.pattern.graph.reset();
    }
    if (before.ccLanes) {
        *active.pattern.ccLanes = *before.ccLanes;
        before.ccLanes.reset();
    } else if (!change.preparedCcLaneOwnerProofPresent()) {
        active.pattern.ccLanes.reset();
    }
    synchronizeHistoryPatternRevisionSignals(active.pattern, before.flat, before.ccLaneRevision);
    restoreActiveContentFocus(active, before.focusedStep);
    return true;
}

FLASHMEM SequencerHistoryFullBankChangePtr prepareHistoryFullBankChangeBefore(
    const SequencerTrackBankState& bank, const SequencerState& active,
    SequencerHistoryDescriptor descriptor) {
    auto change = core::app::makeExtmemUnique<SequencerHistoryFullBankChange>();
    if (!change || !captureHistorySnapshot(bank, active, change->before)) { return nullptr; }
    change->descriptor = descriptor;
    return change;
}

FLASHMEM bool reservePreparedHistoryFullBankAfter(const SequencerTrackBankState& bank,
                                                  const SequencerState& active,
                                                  SequencerHistoryFullBankChange& change) {
    return reserveHistoryTrackBankSnapshotStorage(bank, active, change.after);
}

FLASHMEM bool capturePreparedHistoryFullBankAfterUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& active,
    SequencerHistoryFullBankChange& change) {
    return captureHistoryTrackBankSnapshotUsingReservedStorage(bank, active, change.after);
}

FLASHMEM bool populatePreparedHistoryFullBankStaging(
    const SequencerTrackBankState& liveBank,
    const SequencerState& liveActive,
    const SequencerHistoryTrackBankSnapshot& before,
    SequencerTrackBankState& stagedBank,
    SequencerState& stagedActive
) {
    const uint8_t activeTrack = before.flat.activeTrack;
    if (activeTrack >= SequencerTrackBankState::TRACK_COUNT ||
        liveBank.activeTrackIndex() != activeTrack) {
        return false;
    }

    applyTrackBankSnapshot(stagedBank, stagedActive, before.flat);
    restoreFocus(stagedActive, before.focusedStep);
    restoreActiveStepProperty(stagedActive, before.activeStepProperty);

    if (!copyGraph(stagedActive.pattern, before.editorGraph.get(),
                   before.flat.tracks[activeTrack].graphRevision)) {
        return false;
    }
    SequencerCcLaneBankPtr editorCcLanes;
    if (!cloneSequencerCcLaneBank(editorCcLanes, before.editorCcLanes.get())) {
        return false;
    }
    installSequencerCcLaneBank(stagedActive.pattern, std::move(editorCcLanes));
    synchronizeHistoryPatternRevisionSignals(
        stagedActive.pattern,
        before.flat.tracks[activeTrack],
        liveActive.pattern.ccLaneRevision.get()
    );

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (i == activeTrack) continue;
        auto& target = stagedBank.track(i);
        if (!copyGraph(target, before.bankGraphs[i].get(),
                       before.flat.tracks[i].graphRevision)) {
            return false;
        }
        SequencerCcLaneBankPtr ccLanes;
        if (!cloneSequencerCcLaneBank(ccLanes, before.bankCcLanes[i].get())) {
            return false;
        }
        installSequencerCcLaneBank(target, std::move(ccLanes));
        synchronizeHistoryPatternRevisionSignals(
            target,
            before.flat.tracks[i],
            liveBank.track(i).ccLaneRevision.get()
        );
    }

    auto& activeScratch = stagedBank.track(activeTrack);
    activeScratch.graph.reset();
    activeScratch.ccLanes.reset();
    return true;
}

FLASHMEM bool SequencerHistoryService::recordPattern(uint8_t trackIndex,
                                                     SequencerHistoryPatternSnapshot before,
                                                     SequencerHistoryPatternSnapshot after,
                                                     SequencerHistoryDescriptor descriptor) {
    return recordPatternWithStorage(trackIndex, std::move(before), std::move(after), descriptor,
                                    SequencerHistoryPatternStorage::FullGraph);
}

FLASHMEM bool SequencerHistoryService::recordFlatPattern(uint8_t trackIndex,
                                                         SequencerHistoryPatternSnapshot before,
                                                         SequencerHistoryPatternSnapshot after,
                                                         SequencerHistoryDescriptor descriptor) {
    return recordPatternWithStorage(trackIndex, std::move(before), std::move(after), descriptor,
                                    SequencerHistoryPatternStorage::FlatOnly);
}

FLASHMEM bool SequencerHistoryService::recordPatternWithStorage(
    uint8_t trackIndex, SequencerHistoryPatternSnapshot before,
    SequencerHistoryPatternSnapshot after, SequencerHistoryDescriptor descriptor,
    SequencerHistoryPatternStorage storage) {
    auto change = core::app::makeExtmemUnique<SequencerHistoryPatternChange>();
    if (!change) { return false; }

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

FLASHMEM bool SequencerHistoryService::recordPattern(SequencerHistoryPatternChangePtr change) {
    if (!change || !canRecordPattern(*change)) return false;
    recordPreparedPattern(std::move(change));
    return true;
}

FLASHMEM bool SequencerHistoryService::canRecordPattern(
    const SequencerHistoryPatternChange& change) const {
    if (change.storage == SequencerHistoryPatternStorage::FlatOnly) {
        if (change.before.flat.graphRevision != change.after.flat.graphRevision ||
            change.before.ccLaneRevision != change.after.ccLaneRevision ||
            sameFlatPatternSnapshot(change.before.flat, change.after.flat)) {
            return false;
        }
    } else if (sameMusicalHistorySnapshot(change.before, change.after)) {
        return false;
    }

    return incomingEntryFitsRetainedBudget(patternChangeAdmissionBytes(change));
}

FLASHMEM void SequencerHistoryService::recordPreparedPattern(
    SequencerHistoryPatternChangePtr change) {
    // Admission belongs to the prepare/seal boundary. Every caller of this
    // ownership-transfer API has already proven canRecordPattern(change), and
    // delayed publication must not repeat a fallible policy decision.
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

FLASHMEM bool SequencerHistoryService::recordPattern(SequencerHistoryPatternSnapshot before,
                                                     SequencerHistoryPatternSnapshot after,
                                                     SequencerHistoryDescriptor descriptor) {
    return recordPattern(0, std::move(before), std::move(after), descriptor);
}

FLASHMEM bool SequencerHistoryService::recordFlatPattern(SequencerHistoryPatternSnapshot before,
                                                         SequencerHistoryPatternSnapshot after,
                                                         SequencerHistoryDescriptor descriptor) {
    return recordFlatPattern(0, std::move(before), std::move(after), descriptor);
}

FLASHMEM bool SequencerHistoryService::recordFullBank(SequencerHistoryTrackBankSnapshot before,
                                                      SequencerHistoryTrackBankSnapshot after,
                                                      SequencerHistoryDescriptor descriptor) {
    auto change = core::app::makeExtmemUnique<SequencerHistoryFullBankChange>();
    if (!change) { return false; }

    change->descriptor = descriptor;
    change->before = std::move(before);
    change->after = std::move(after);
    return recordFullBank(std::move(change));
}

FLASHMEM bool SequencerHistoryService::recordFullBank(SequencerHistoryFullBankChangePtr change) {
    if (!change || !canRecordFullBank(*change)) return false;
    recordPreparedFullBank(std::move(change));
    return true;
}

FLASHMEM bool SequencerHistoryService::canRecordFullBank(
    const SequencerHistoryFullBankChange& change) const {
    return !sameMusicalHistorySnapshot(change.before, change.after) &&
           incomingEntryFitsRetainedBudget(fullBankChangeRetainedBytes(change));
}

FLASHMEM void SequencerHistoryService::recordPreparedFullBank(
    SequencerHistoryFullBankChangePtr change) {
    if (!change || !canRecordFullBank(*change)) return;

    commitAdmittedFullBank(std::move(change));
}

FLASHMEM void SequencerHistoryService::commitAdmittedFullBank(
    SequencerHistoryFullBankChangePtr change) {
    assert(change);
    if (!change) return;

    if (change->descriptor.kind == SequencerHistoryActionKind::PatternEdit) {
        change->descriptor.kind = SequencerHistoryActionKind::FullBank;
    }

    SequencerHistoryEntry entry;
    entry.scope = SequencerHistoryScope::FullBank;
    entry.fullBank = std::move(change);
    commitPreparedEntry(std::move(entry));
}

FLASHMEM bool SequencerHistoryService::recordStructure(
    SequencerHistoryTrackStructureChangePtr change) {
    if (!change || !canRecordStructure(*change)) { return false; }

    recordPreparedStructure(std::move(change));
    return true;
}

FLASHMEM void SequencerHistoryService::recordPreparedStructure(
    SequencerHistoryTrackStructureChangePtr change) {
    if (!change || !canRecordStructure(*change)) return;

    commitAdmittedStructure(std::move(change));
}

FLASHMEM void SequencerHistoryService::commitAdmittedStructure(
    SequencerHistoryTrackStructureChangePtr change
) noexcept {
    if (!change) failSequencerHistoryInvariant();

    if (change->descriptor.kind == SequencerHistoryActionKind::PatternEdit) {
        change->descriptor.kind = SequencerHistoryActionKind::TrackStructure;
    }

    SequencerHistoryEntry entry;
    entry.scope = SequencerHistoryScope::Structure;
    entry.structure = std::move(change);

    commitPreparedEntry(std::move(entry));
}

FLASHMEM bool SequencerHistoryService::canRecordStructure(
    const SequencerHistoryTrackStructureChange& change) const {
    if (sameMusicalHistoryStructureSnapshot(change.before, change.after) &&
        !macroTrackStructureHistoryChanged(change)) {
        return false;
    }

    return incomingEntryFitsRetainedBudget(structureChangeRetainedBytes(change));
}

FLASHMEM bool SequencerHistoryService::undo(SequencerTrackBankState& bank, SequencerState& active) {
    return undoWithResult(bank, active).applied;
}

FLASHMEM SequencerHistoryApplyResult
SequencerHistoryService::undoWithResult(SequencerTrackBankState& bank, SequencerState& active) {
    SequencerHistoryApplyResult result;
    result.direction = SequencerHistoryDirection::Undo;

    if (undo_count_ == 0) { return result; }

    SequencerHistoryEntry& entry = undo_[undo_count_ - 1U];
    const uintptr_t projectHistoryEntryIdentity = projectHistoryIdentity(entry);
    result.descriptor = descriptorForEntry(entry);
    if (!applyEntrySnapshot(entry, false, bank, active)) { return result; }

    auto moved = popBack(undo_, undo_count_);
    result.applied = pushRedo(std::move(moved));
    if (result.applied && project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(core::state::project::ProjectHistoryDomain::Sequencer,
                                             projectHistoryEntryIdentity,
                                             core::state::project::ProjectHistoryDirection::Undo);
    }
    return result;
}

FLASHMEM bool SequencerHistoryService::redo(SequencerTrackBankState& bank, SequencerState& active) {
    return redoWithResult(bank, active).applied;
}

FLASHMEM SequencerHistoryApplyResult
SequencerHistoryService::redoWithResult(SequencerTrackBankState& bank, SequencerState& active) {
    SequencerHistoryApplyResult result;
    result.direction = SequencerHistoryDirection::Redo;

    if (redo_count_ == 0) { return result; }

    SequencerHistoryEntry& entry = redo_[redo_count_ - 1U];
    const uintptr_t projectHistoryEntryIdentity = projectHistoryIdentity(entry);
    result.descriptor = descriptorForEntry(entry);
    if (!applyEntrySnapshot(entry, true, bank, active)) { return result; }

    auto moved = popBack(redo_, redo_count_);
    result.applied = pushUndo(std::move(moved));
    if (result.applied && project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(core::state::project::ProjectHistoryDomain::Sequencer,
                                             projectHistoryEntryIdentity,
                                             core::state::project::ProjectHistoryDirection::Redo);
    }
    return result;
}

FLASHMEM SequencerStructureHistoryReplayPrepareOutcome
SequencerHistoryService::prepareStructureHistoryReplay(
    SequencerHistoryDirection direction,
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const core::state::macro::MacroPagesState& pages,
    SequencerPreparedStructureHistoryReplay& out
) const {
    out.reset();
    if (active.stepContentDraft.active.get()) {
        return SequencerStructureHistoryReplayPrepareOutcome::Rejected;
    }

    const bool redo = direction == SequencerHistoryDirection::Redo;
    const auto& entries = redo ? redo_ : undo_;
    const uint8_t count = redo ? redo_count_ : undo_count_;
    if (count == 0U) {
        return SequencerStructureHistoryReplayPrepareOutcome::Unavailable;
    }

    const auto& entry = entries[count - 1U];
    if (entry.scope != SequencerHistoryScope::Structure) {
        return SequencerStructureHistoryReplayPrepareOutcome::Unavailable;
    }
    if (!entry.structure) {
        return SequencerStructureHistoryReplayPrepareOutcome::Rejected;
    }

    const bool after = redo;
    const auto* macroStructure = entry.structure->macroStructure.get();
    if (macroStructure != nullptr &&
        !validateMacroTrackStructureHistoryReplay(pages, *macroStructure, after)) {
        return SequencerStructureHistoryReplayPrepareOutcome::Rejected;
    }

    const auto& target = after ? entry.structure->after : entry.structure->before;
    if (!prepareHistoryStructureReplayOwners(
            target, bank.activeTrackIndex(), out)) {
        return SequencerStructureHistoryReplayPrepareOutcome::Rejected;
    }

    out.direction = direction;
    out.entryIdentity = projectHistoryIdentity(entry);
    out.entry = entry.structure.get();
    out.macroStructure = macroStructure;
    if (entry.structure->activation.valid()) {
        out.activation.reference = entry.structure->activation;
        out.activation.targetAudibleMask = after
            ? entry.structure->activationAfterAudibleMask
            : entry.structure->activationBeforeAudibleMask;
    }
    return SequencerStructureHistoryReplayPrepareOutcome::Prepared;
}

FLASHMEM SequencerHistoryApplyResult
SequencerHistoryService::commitPreparedStructureHistoryReplay(
    SequencerTrackBankState& bank,
    SequencerState& active,
    core::state::macro::MacroPagesState& pages,
    SequencerPreparedStructureHistoryReplay&& replay
) noexcept {
    SequencerHistoryApplyResult result;
    result.direction = replay.direction;

    const bool redo = replay.direction == SequencerHistoryDirection::Redo;
    auto& entries = redo ? redo_ : undo_;
    uint8_t& count = redo ? redo_count_ : undo_count_;
    if (count == 0U) failSequencerHistoryInvariant();

    auto& entry = entries[count - 1U];
    const auto* target = entry.structure == nullptr
        ? nullptr
        : (redo ? &entry.structure->after : &entry.structure->before);
    if (!replay.valid() || entry.scope != SequencerHistoryScope::Structure ||
        !entry.structure || replay.entry != entry.structure.get() ||
        replay.entryIdentity != projectHistoryIdentity(entry) ||
        replay.targetSnapshot != target ||
        replay.macroStructure != entry.structure->macroStructure.get()) {
        failSequencerHistoryInvariant();
    }

    result.descriptor = descriptorForEntry(entry);
    commitPreparedHistoryStructureReplayState(bank, active, replay);
    if (replay.macroStructure != nullptr) {
        commitMacroTrackStructureHistoryReplay(
            pages, *replay.macroStructure, redo);
    }

    const uintptr_t identity = replay.entryIdentity;
    auto moved = popBack(entries, count);
    const bool pushed = redo
        ? pushUndo(std::move(moved))
        : pushRedo(std::move(moved));
    if (!pushed) failSequencerHistoryInvariant();

    result.applied = true;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            core::state::project::ProjectHistoryDomain::Sequencer,
            identity,
            redo ? core::state::project::ProjectHistoryDirection::Redo
                 : core::state::project::ProjectHistoryDirection::Undo);
    }
    return result;
}

FLASHMEM bool SequencerHistoryService::peekUndoTrackActivation(
    SequencerTrackActivationHistoryPlan& out) const {
    out = {};
    if (undo_count_ == 0) return false;
    const auto& entry = undo_[undo_count_ - 1U];
    if (entry.scope == SequencerHistoryScope::PatternOnly && entry.pattern &&
        entry.pattern->auxiliary.activation.reference.valid()) {
        out.reference = entry.pattern->auxiliary.activation.reference;
        out.targetAudibleMask = entry.pattern->auxiliary.activation.targetAudibleMask;
        return true;
    }
    if (entry.scope != SequencerHistoryScope::Structure || !entry.structure ||
        !entry.structure->activation.valid())
        return false;
    out.reference = entry.structure->activation;
    out.targetAudibleMask = entry.structure->activationBeforeAudibleMask;
    return true;
}

FLASHMEM bool SequencerHistoryService::peekRedoTrackActivation(
    SequencerTrackActivationHistoryPlan& out) const {
    out = {};
    if (redo_count_ == 0) return false;
    const auto& entry = redo_[redo_count_ - 1U];
    if (entry.scope == SequencerHistoryScope::PatternOnly && entry.pattern &&
        entry.pattern->auxiliary.activation.reference.valid()) {
        out.reference = entry.pattern->auxiliary.activation.reference;
        out.targetAudibleMask = entry.pattern->auxiliary.activation.targetAudibleMask;
        return true;
    }
    if (entry.scope != SequencerHistoryScope::Structure || !entry.structure ||
        !entry.structure->activation.valid())
        return false;
    out.reference = entry.structure->activation;
    out.targetAudibleMask = entry.structure->activationAfterAudibleMask;
    return true;
}

FLASHMEM void SequencerHistoryService::clear() {
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCleared(core::state::project::ProjectHistoryDomain::Sequencer);
    }
    undo_count_ = 0;
    redo_count_ = 0;
    for (auto& item : undo_) { item = SequencerHistoryEntry{}; }
    for (auto& item : redo_) { item = SequencerHistoryEntry{}; }
}

FLASHMEM void SequencerHistoryService::discardRedoBranch() {
    for (uint8_t index = 0U; index < redo_count_; ++index) {
        if (project_history_sink_ != nullptr) {
            project_history_sink_->notifyEvicted(
                core::state::project::ProjectHistoryDomain::Sequencer,
                projectHistoryIdentity(redo_[index]));
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
    return undo_count_ == 0U ? 0U : projectHistoryIdentity(undo_[undo_count_ - 1U]);
}

FLASHMEM uintptr_t SequencerHistoryService::projectHistoryRedoIdentity() const {
    return redo_count_ == 0U ? 0U : projectHistoryIdentity(redo_[redo_count_ - 1U]);
}

FLASHMEM size_t SequencerHistoryService::retainedBytes() const {
    return entriesRetainedBytes(undo_, undo_count_) + entriesRetainedBytes(redo_, redo_count_);
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
    const uint8_t actionKind = static_cast<uint8_t>(descriptorForEntry(entry).kind);

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
            core::state::project::ProjectHistoryDomain::Sequencer, identity, actionKind);
    }
}

}  // namespace core::state::sequencer

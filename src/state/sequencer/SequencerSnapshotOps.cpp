#include "state/sequencer/SequencerSnapshotOps.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerChordState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerClipRegionOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM uint8_t sanitizeSequencerLength(uint8_t length) {
    if (length == 0 || length > SequencerPatternState::MAX_STEPS) {
        return SequencerPatternState::DEFAULT_LENGTH;
    }
    return length;
}

FLASHMEM uint8_t sanitizeStepsPerBeat(uint8_t spb) {
    if (spb == 0) {
        return SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    }
    return spb;
}

FLASHMEM uint8_t sanitizeMidi7(uint8_t value) {
    return (value > 127U) ? 127U : value;
}

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings sanitizeScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    return settings;
}

struct StepPayload {
    uint8_t note = SequencerPatternState::DEFAULT_NOTE;
    uint8_t velocity = SequencerPatternState::DEFAULT_VELOCITY;
    uint16_t gate = SequencerPatternState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = SequencerPatternState::DEFAULT_PROBABILITY;
};

FLASHMEM StepPayload defaultStep() {
    return {};
}

FLASHMEM StepPayload readStep(const SequencerPatternState& source, uint8_t step) {
    return {
        source.note[step],
        source.velocity[step],
        source.gate[step],
        source.nudge[step],
        source.probability[step],
    };
}

FLASHMEM StepPayload readSanitizedStep(const oc::note::sequencer::StepSequencerStepData& source, uint8_t step) {
    return {
        sanitizeMidi7(source.note[step]),
        sanitizeMidi7(source.velocity[step]),
        SequencerPatternState::clampGatePercent(source.gate[step]),
        source.nudge[step],
        SequencerPatternState::clampProbability(source.probability[step]),
    };
}

FLASHMEM bool sameStep(const StepPayload& lhs, const StepPayload& rhs) {
    return lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity &&
           lhs.gate == rhs.gate &&
           lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

using StepNode = oc::note::sequencer::StepSequencerStepNode;
using StepGraph = oc::note::sequencer::StepSequencerGraph;
using StepSequenceKind = oc::note::sequencer::StepSequencerSequenceKind;

FLASHMEM bool sameRootNode(const StepNode& lhs, const StepNode& rhs) {
    return lhs.flags == rhs.flags &&
           lhs.noteOffset == rhs.noteOffset &&
           lhs.velocityOffset == rhs.velocityOffset &&
           lhs.gateOffset == rhs.gateOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset &&
           lhs.probabilityOffset == rhs.probabilityOffset &&
           lhs.localVariation.pitchSemitones == rhs.localVariation.pitchSemitones &&
           lhs.localVariation.velocity == rhs.localVariation.velocity &&
           lhs.localVariation.gatePercent == rhs.localVariation.gatePercent &&
           lhs.localVariation.nudge == rhs.localVariation.nudge &&
           lhs.chordMode == rhs.chordMode &&
           chordSpecEqualsSanitized(lhs.chordSpec, rhs.chordSpec) &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId;
}

FLASHMEM bool canEditRootNodes(const SequencerPatternState& pattern) {
    const auto* graph = graphView(pattern);
    return graph != nullptr &&
           graph->stepNodeCount >= SequencerPatternState::MAX_STEPS;
}

FLASHMEM bool assignRootNode(SequencerPatternState& pattern,
                             uint8_t step,
                             const StepNode& node) {
    if (!canEditRootNodes(pattern) || step >= SequencerPatternState::MAX_STEPS) {
        return false;
    }

    auto& target = pattern.graph->stepNodes[step];
    if (sameRootNode(target, node)) {
        return false;
    }

    target = node;
    return true;
}

FLASHMEM bool clearRootNode(SequencerPatternState& pattern, uint8_t step) {
    return assignRootNode(pattern, step, StepNode{});
}

FLASHMEM bool validBatchPatternState(const SequencerState& sequencer) {
    const auto& pattern = sequencer.pattern();
    const uint8_t length = pattern.length;
    if (length == 0U || length > SequencerPatternState::MAX_STEPS ||
        !validClipRegion(pattern, sequencer.clip())) {
        return false;
    }
    return (pattern.enabledMask & ~lengthMask(length)) ==
           oc::note::sequencer::StepBitMask128{};
}

FLASHMEM bool validBatchRootGraph(const SequencerPatternState& pattern) {
    const StepGraph* graph = graphView(pattern);
    return graph == nullptr || validInitializedSequencerGraph(*graph);
}

FLASHMEM void applyBatchPlaybackRegion(
    SequencerState& sequencer,
    const SequencerClipPlaybackRegion& region,
    const oc::note::sequencer::StepBitMask128& enabledMask
) {
    // Internal callers validate the complete region before the first live
    // write; keep this leaf free of firmware assert strings in scarce DTCM.
    const uint16_t ticksPerStep = sequencerTicksPerStep(
        sequencer.pattern().stepsPerBeat
    );
    sequencer.clip() = {
        static_cast<uint16_t>(region.playStart * ticksPerStep),
        static_cast<uint16_t>(region.loopStart * ticksPerStep),
        static_cast<uint16_t>(region.loopEnd * ticksPerStep),
    };
    sequencer.pattern().setEnabledMask(
        enabledMask & lengthMask(region.contentLength)
    );
    // Length is observable and therefore published after its dependent bytes.
    sequencer.pattern().setLength(region.contentLength);
}

FLASHMEM SequencerSnapshotBatchMutationResult batchResult(
    SequencerSnapshotBatchMutationStatus status,
    uint8_t previousLength,
    uint8_t resultingLength,
    SequencerSnapshotBatchDomains domains = {}
) {
    return {
        .status = status,
        .domains = domains,
        .previousLength = previousLength,
        .resultingLength = resultingLength,
    };
}

FLASHMEM void writeStep(oc::note::sequencer::StepSequencerStepData& target, uint8_t step, const StepPayload& payload) {
    target.note[step] = payload.note;
    target.velocity[step] = payload.velocity;
    target.gate[step] = payload.gate;
    target.nudge[step] = payload.nudge;
    target.probability[step] = payload.probability;
}

}  // namespace

FLASHMEM oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length) {
    return oc::note::sequencer::StepBitMask128::prefixMask(length);
}

namespace {

FLASHMEM void applySnapshotImpl(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot
) {
    const uint8_t length = sanitizeSequencerLength(snapshot.length);
    target.setContentLength(length);
    target.setStepsPerBeat(sanitizeStepsPerBeat(snapshot.stepsPerBeat));
    target.setEnabledMask(snapshot.enabledMask & lengthMask(length));
    target.setPatternVariationRanges(snapshot.variationRanges);
    target.setPatternScalePolicy(snapshot.scalePolicy);
    target.setPatternScaleOverride(snapshot.scaleOverride);
    target.setPitchEditMode(snapshot.pitchEditMode);
    target.setPatternSwingOffsetPercent(snapshot.swingOffsetPercent);
    target.setPatternNudgePercent(snapshot.patternNudgePercent);
    target.setPatternTimingRevision(snapshot.patternTimingRevision);
    target.graph.reset();
    target.setGraphRevision(snapshot.graphRevision);

    for (uint16_t i = 0; i < SequencerPatternState::MAX_STEPS; ++i) {
        const auto step = static_cast<uint8_t>(i);
        writeStep(target, step, readSanitizedStep(snapshot, step));
    }

    target.bumpStepDataRevision();
}

FLASHMEM void applySnapshotPreservingGraphImpl(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot
) {
    auto graph = std::move(target.graph);
    applySnapshotImpl(target, snapshot);
    target.graph = std::move(graph);
}

FLASHMEM void applySnapshotToEditorImpl(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot
) {
    const uint8_t length = sanitizeSequencerLength(snapshot.length);
    const uint8_t focusedBefore = target.focusedStep.get();

    applySnapshotImpl(target.pattern(), snapshot);

    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
}

FLASHMEM void applySnapshotToEditorPreservingGraphImpl(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot
) {
    auto graph = std::move(target.pattern().graph);
    applySnapshotToEditorImpl(target, snapshot);
    target.pattern().graph = std::move(graph);
}

}  // namespace

FLASHMEM void captureSnapshot(const SequencerPatternState& source, SequencerPatternSnapshot& out) {
    out.length = sanitizeSequencerLength(source.length);
    out.stepsPerBeat = sanitizeStepsPerBeat(source.stepsPerBeat);
    out.enabledMask = source.enabledMask;
    out.stepDataRevision = source.stepDataRevision;
    out.patternVariationRevision = source.patternVariationRevision;
    out.patternScaleRevision = source.patternScaleRevision;
    out.patternTimingRevision = source.patternTimingRevision;
    out.graphRevision = source.graphRevision;
    out.swingOffsetPercent =
        SequencerPatternState::clampPatternSwingOffsetPercent(source.swingOffsetPercent);
    out.patternNudgePercent =
        SequencerPatternState::clampPatternNudgePercent(source.patternNudgePercent);
    out.effectiveSwingPercent = source.effectiveSwingPercent(0);
    out.variationRanges = source.variationRanges;
    out.variationRanges.clamp();
    out.scalePolicy = source.scalePolicy;
    out.scaleOverride = sanitizeScaleSettings(source.scaleOverride);
    out.pitchEditMode = source.pitchEditMode;
    out.effectiveScaleSettings = resolveEffectiveScaleSettings(
        {},
        out.scalePolicy,
        out.scaleOverride
    );

    for (uint16_t i = 0; i < SequencerPatternState::MAX_STEPS; ++i) {
        const auto step = static_cast<uint8_t>(i);
        writeStep(out, step, readSanitizedStep(source, step));
    }
}

FLASHMEM void applySnapshot(SequencerPatternState& target, const SequencerPatternSnapshot& snapshot) {
    applySnapshotImpl(target, snapshot);
}

FLASHMEM void applySnapshotPreservingGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot
) {
    applySnapshotPreservingGraphImpl(target, snapshot);
}

FLASHMEM void copySequencerCcLaneRevision(
    SequencerPatternState& target,
    const SequencerPatternState& source
) {
    target.setCcLaneRevision(source.ccLaneRevision);
}

FLASHMEM bool copyPatternState(
    SequencerPatternState& target,
    const SequencerPatternState& source
) {
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source, snapshot);
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph;
    const auto* sourceGraph = graphView(source);
    if (sourceGraph != nullptr) {
        graph = core::app::makeExtmemUnique<
            oc::note::sequencer::StepSequencerGraph
        >(*sourceGraph);
        if (!graph) return false;
    }
    SequencerCcLaneBankPtr ccLanes;
    if (!cloneSequencerCcLaneBank(ccLanes, source.ccLanes.get())) return false;

    applySnapshot(target, snapshot);
    target.graph = std::move(graph);
    target.setGraphRevision(snapshot.graphRevision);
    installSequencerCcLaneBank(target, std::move(ccLanes));
    copySequencerCcLaneRevision(target, source);
    return true;
}

FLASHMEM bool applySnapshotWithGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    // Prepare or update graph ownership before touching scalar state. If PSRAM
    // allocation fails, the target remains completely unchanged.
    if (!copyGraph(target, graph, snapshot.graphRevision)) return false;
    applySnapshotPreservingGraph(target, snapshot);
    return true;
}

FLASHMEM void installTrackContentSnapshotWithOwnedGraph(
    SequencerPatternState& target,
    SequencerClipState& targetClip,
    const SequencerPatternSnapshot& snapshot,
    const SequencerClipState& clipSnapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph
) {
    targetClip = clipSnapshot;
    applySnapshotImpl(target, snapshot);
    target.graph = std::move(graph);
    target.setGraphRevision(snapshot.graphRevision);
}

FLASHMEM void installTrackContentSnapshotWithOwnedPayload(
    SequencerPatternState& target,
    SequencerClipState& targetClip,
    const SequencerPatternSnapshot& snapshot,
    const SequencerClipState& clipSnapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph,
    SequencerCcLaneBankPtr ccLanes
) {
    installTrackContentSnapshotWithOwnedGraph(
        target,
        targetClip,
        snapshot,
        clipSnapshot,
        std::move(graph)
    );
    installSequencerCcLaneBank(target, std::move(ccLanes));
}

FLASHMEM void applySnapshotToEditor(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
    applySnapshotToEditorImpl(target, snapshot);
}

FLASHMEM void applySnapshotToEditorPreservingGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot
) {
    applySnapshotToEditorPreservingGraphImpl(target, snapshot);
}

FLASHMEM void installTrackContentSnapshotToEditorWithOwnedGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    const SequencerClipState& clipSnapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph
) {
    target.clip() = clipSnapshot;
    applySnapshotToEditorImpl(target, snapshot);
    target.pattern().graph = std::move(graph);
    target.pattern().setGraphRevision(snapshot.graphRevision);
    target.bumpClipRevision();
}

FLASHMEM void installTrackContentSnapshotToEditorWithOwnedPayload(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    const SequencerClipState& clipSnapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph,
    SequencerCcLaneBankPtr ccLanes
) {
    installTrackContentSnapshotToEditorWithOwnedGraph(
        target,
        snapshot,
        clipSnapshot,
        std::move(graph)
    );
    installSequencerCcLaneBank(target.pattern(), std::move(ccLanes));
}

FLASHMEM bool rotatePatternState(SequencerPatternState& target, int offsetSteps) {
    const uint8_t len = target.length;
    if (len <= 1) return false;

    int normalizedOffset = offsetSteps % static_cast<int>(len);
    if (normalizedOffset < 0) {
        normalizedOffset += len;
    }
    if (normalizedOffset == 0) return false;

    std::array<StepPayload, SequencerState::MAX_STEPS> nextSteps{};
    const auto activeMask = lengthMask(len);
    const auto sourceMask = target.enabledMask;
    auto nextMask = sourceMask & ~activeMask;

    for (uint16_t i = 0; i < len; ++i) {
        const auto sourceStep = static_cast<uint8_t>(i);
        const uint8_t dst = static_cast<uint8_t>((i + normalizedOffset) % len);
        nextSteps[dst] = readStep(target, sourceStep);

        if (sourceMask.test(sourceStep)) {
            nextMask.setBit(dst, true);
        }
    }

    for (uint16_t i = 0; i < len; ++i) {
        const auto step = static_cast<uint8_t>(i);
        writeStep(target, step, nextSteps[i]);
    }

    target.setEnabledMask(nextMask);
    rotateRootStepNodes(target, normalizedOffset);
    if (target.ccLanes && rotateSequencerCcLaneBank(
            *target.ccLanes,
            len,
            normalizedOffset
        )) {
        target.bumpCcLaneRevision();
    }
    target.bumpStepDataRevision();
    return true;
}

FLASHMEM SequencerSnapshotBatchMutationResult
resizeSequencerRootContentUnversioned(
    SequencerState& target,
    uint8_t requiredLength
) noexcept {
    const uint8_t oldLength = target.pattern().length;
    if (requiredLength == 0U ||
        requiredLength > SequencerState::MAX_STEPS ||
        requiredLength < oldLength) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            oldLength,
            oldLength
        );
    }
    if (!validBatchPatternState(target)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            oldLength,
            oldLength
        );
    }
    if (!validBatchRootGraph(target.pattern())) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_GRAPH,
            oldLength,
            oldLength
        );
    }
    if (requiredLength == oldLength) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::NO_CHANGE,
            oldLength,
            oldLength
        );
    }

    const auto nextRegion = resizedClipPlaybackRegion(
        clipPlaybackRegion(target.pattern(), target.clip()),
        requiredLength
    );
    if (!nextRegion.isValid()) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            oldLength,
            oldLength
        );
    }

    bool graphChanged = false;
    auto enabledMask = target.pattern().enabledMask;
    for (uint16_t step = oldLength; step < requiredLength; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        writeStep(target.pattern(), stepIndex, defaultStep());
        enabledMask.setBit(stepIndex, false);
        graphChanged = clearRootNode(target.pattern(), stepIndex) || graphChanged;
    }
    applyBatchPlaybackRegion(target, nextRegion, enabledMask);

    const SequencerSnapshotBatchDomains domains{
        .stepData = true,
        .graph = graphChanged,
        // Page creation and Step-paste extension never reinterpret or erase
        // Pattern-owned CC events, including cold events beyond the former
        // Content Length. Only Page deletion owns a CC shift/removal.
        .ccLanes = false,
        .clip = true,
    };
    return batchResult(
        SequencerSnapshotBatchMutationStatus::APPLIED,
        oldLength,
        requiredLength,
        domains
    );
}

FLASHMEM SequencerSnapshotBatchMutationResult
extendSequencerPageRootUnversioned(
    SequencerState& target,
    uint8_t pageIndex
) noexcept {
    if (pageIndex >= SequencerState::PAGE_COUNT) {
        const uint8_t length = target.pattern().length;
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            length,
            length
        );
    }
    const uint8_t requiredLength = static_cast<uint8_t>(
        static_cast<uint16_t>(pageIndex + 1U) * SequencerState::STEPS_PER_PAGE
    );
    return resizeSequencerRootContentUnversioned(
        target,
        std::max(requiredLength, target.pattern().length)
    );
}

FLASHMEM SequencerSnapshotBatchMutationResult
clearSequencerRootStepSpanUnversioned(
    SequencerState& target,
    uint8_t startStep,
    uint8_t stepCount
) noexcept {
    const uint8_t length = target.pattern().length;
    const uint16_t end = static_cast<uint16_t>(startStep) + stepCount;
    if (stepCount == 0U || end > length || end > SequencerState::MAX_STEPS) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            length,
            length
        );
    }
    if (!validBatchPatternState(target)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            length,
            length
        );
    }
    if (!validBatchRootGraph(target.pattern())) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_GRAPH,
            length,
            length
        );
    }

    auto enabledMask = target.pattern().enabledMask;
    bool stepChanged = false;
    bool graphChanged = false;
    for (uint16_t step = startStep; step < end; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (enabledMask.test(stepIndex)) {
            enabledMask.setBit(stepIndex, false);
            stepChanged = true;
        }
        if (!sameStep(readStep(target.pattern(), stepIndex), defaultStep())) {
            writeStep(target.pattern(), stepIndex, defaultStep());
            stepChanged = true;
        }
        graphChanged = clearRootNode(target.pattern(), stepIndex) || graphChanged;
    }
    if (stepChanged) {
        target.pattern().setEnabledMask(enabledMask);
    }

    const SequencerSnapshotBatchDomains domains{
        .stepData = stepChanged,
        .graph = graphChanged,
        .ccLanes = false,
        .clip = false,
    };
    return batchResult(
        domains.any()
            ? SequencerSnapshotBatchMutationStatus::APPLIED
            : SequencerSnapshotBatchMutationStatus::NO_CHANGE,
        length,
        length,
        domains
    );
}

FLASHMEM SequencerSnapshotBatchMutationResult
deleteSequencerRootPagesUnversioned(
    SequencerState& target,
    uint16_t pageMask
) noexcept {
    const uint8_t oldLength = target.pattern().length;
    if (!validBatchPatternState(target)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            oldLength,
            oldLength
        );
    }
    if (!validBatchRootGraph(target.pattern())) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_GRAPH,
            oldLength,
            oldLength
        );
    }

    const uint8_t pageCount = target.pattern().activePageCount();
    const uint16_t activePageMask = pageCount >= 16U
        ? UINT16_MAX
        : static_cast<uint16_t>((uint16_t{1} << pageCount) - 1U);
    if (pageMask == 0U) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::NO_CHANGE,
            oldLength,
            oldLength
        );
    }
    if ((pageMask & static_cast<uint16_t>(~activePageMask)) != 0U) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            oldLength,
            oldLength
        );
    }

    auto nextRegion = clipPlaybackRegion(target.pattern(), target.clip());
    oc::note::sequencer::StepBitMask128 removalMask{};
    uint8_t removedSteps = 0U;
    for (int page = static_cast<int>(pageCount) - 1; page >= 0; --page) {
        const uint16_t pageBit = static_cast<uint16_t>(uint16_t{1} << page);
        if ((pageMask & pageBit) == 0U) continue;

        const uint8_t removeAt = static_cast<uint8_t>(
            static_cast<uint16_t>(page) * SequencerState::STEPS_PER_PAGE
        );
        const uint8_t removeLength = static_cast<uint8_t>(std::min<uint16_t>(
            SequencerState::STEPS_PER_PAGE,
            static_cast<uint16_t>(oldLength - removeAt)
        ));
        for (uint16_t step = removeAt;
             step < static_cast<uint16_t>(removeAt) + removeLength;
             ++step) {
            removalMask.setBit(static_cast<uint8_t>(step));
        }
        removedSteps = static_cast<uint8_t>(removedSteps + removeLength);
        nextRegion = removedClipPlaybackRegion(
            nextRegion,
            removeAt,
            removeLength
        );
        if (!nextRegion.isValid()) {
            return batchResult(
                SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
                oldLength,
                oldLength
            );
        }
    }

    if (removedSteps == 0U || removedSteps >= oldLength) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            oldLength,
            oldLength
        );
    }
    const uint8_t newLength = static_cast<uint8_t>(oldLength - removedSteps);
    if (nextRegion.contentLength != newLength) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            oldLength,
            oldLength
        );
    }

    bool ccChanged = false;
    if (target.pattern().ccLanes) {
        const auto ccResult = removeSequencerCcLaneBankStepsUnversioned(
            *target.pattern().ccLanes,
            oldLength,
            removalMask
        );
        if (!ccResult.accepted()) {
            return batchResult(
                ccResult.status == SequencerCcLaneBatchMutationStatus::INVALID_BANK
                    ? SequencerSnapshotBatchMutationStatus::INVALID_CC_LANE_BANK
                    : SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
                oldLength,
                oldLength
            );
        }
        ccChanged = ccResult.changed();
    }

    const auto sourceEnabledMask = target.pattern().enabledMask;
    oc::note::sequencer::StepBitMask128 nextEnabledMask{};
    bool graphChanged = false;
    uint8_t destination = 0U;
    for (uint16_t source = 0; source < oldLength; ++source) {
        const auto sourceStep = static_cast<uint8_t>(source);
        if (removalMask.test(sourceStep)) continue;

        if (destination != sourceStep) {
            writeStep(target.pattern(), destination, readStep(target.pattern(), sourceStep));
            if (canEditRootNodes(target.pattern())) {
                graphChanged = assignRootNode(
                    target.pattern(),
                    destination,
                    target.pattern().graph->stepNodes[sourceStep]
                ) || graphChanged;
            }
        }
        nextEnabledMask.setBit(destination, sourceEnabledMask.test(sourceStep));
        ++destination;
    }
    // The validated, non-overlapping Page mask makes this identity exact:
    // every unmasked source contributes one destination.

    for (uint16_t step = newLength; step < SequencerState::MAX_STEPS; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        writeStep(target.pattern(), stepIndex, defaultStep());
        graphChanged = clearRootNode(target.pattern(), stepIndex) || graphChanged;
    }
    applyBatchPlaybackRegion(target, nextRegion, nextEnabledMask);

    const SequencerSnapshotBatchDomains domains{
        .stepData = true,
        .graph = graphChanged,
        .ccLanes = ccChanged,
        .clip = true,
    };
    return batchResult(
        SequencerSnapshotBatchMutationStatus::APPLIED,
        oldLength,
        newLength,
        domains
    );
}

FLASHMEM void publishSequencerSnapshotBatchRevisions(
    SequencerState& sequencer,
    const SequencerSnapshotBatchDomains& domains
) noexcept {
    auto& pattern = sequencer.pattern();
    if (domains.stepData) pattern.bumpStepDataRevision();
    if (domains.graph) pattern.bumpGraphRevision();
    if (domains.ccLanes && pattern.ccLanes) {
        ++pattern.ccLanes->revision;
        pattern.bumpCcLaneRevision();
    }
    if (domains.clip) sequencer.bumpClipRevision();
}

}  // namespace core::state::sequencer

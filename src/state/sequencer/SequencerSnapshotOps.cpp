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
#include "state/sequencer/SequencerPatternRegionOps.hpp"

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

FLASHMEM SequencerPatternPlaybackRegion snapshotPlaybackRegion(
    const SequencerPatternSnapshot& snapshot,
    uint8_t contentLength
) {
    SequencerPatternPlaybackRegion region{
        contentLength,
        snapshot.playStart,
        snapshot.loopStart,
        snapshot.loopEnd,
    };
    return region.isValid()
        ? region
        : SequencerPatternPlaybackRegion::fullLength(contentLength);
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

FLASHMEM StepPayload readSanitizedStep(const SequencerPatternState& source, uint8_t step) {
    return {
        sanitizeMidi7(source.note[step]),
        sanitizeMidi7(source.velocity[step]),
        SequencerPatternState::clampGatePercent(source.gate[step]),
        source.nudge[step],
        SequencerPatternState::clampProbability(source.probability[step]),
    };
}

FLASHMEM StepPayload readSanitizedStep(const SequencerPatternSnapshot& source, uint8_t step) {
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

FLASHMEM bool validBatchPatternState(const SequencerPatternState& pattern) {
    const uint8_t length = pattern.length.get();
    if (length == 0U || length > SequencerPatternState::MAX_STEPS ||
        !patternPlaybackRegion(pattern).isValid()) {
        return false;
    }
    return (pattern.enabledMask.get() & ~lengthMask(length)) ==
           oc::note::sequencer::StepBitMask128{};
}

FLASHMEM bool validBatchRootGraph(const SequencerPatternState& pattern) {
    const StepGraph* graph = graphView(pattern);
    return graph == nullptr || validInitializedSequencerGraph(*graph);
}

FLASHMEM void applyBatchPlaybackRegion(
    SequencerPatternState& pattern,
    const SequencerPatternPlaybackRegion& region,
    const oc::note::sequencer::StepBitMask128& enabledMask
) {
    // Internal callers validate the complete region before the first live
    // write; keep this leaf free of firmware assert strings in scarce DTCM.
    pattern.playStart = region.playStart;
    pattern.loopStart = region.loopStart;
    pattern.loopEnd = region.loopEnd;
    pattern.enabledMask.set(enabledMask & lengthMask(region.contentLength));
    // Length is observable and therefore published after its dependent bytes.
    pattern.length.set(region.contentLength);
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

FLASHMEM void writeStep(SequencerPatternState& target, uint8_t step, const StepPayload& payload) {
    target.note[step] = payload.note;
    target.velocity[step] = payload.velocity;
    target.gate[step] = payload.gate;
    target.nudge[step] = payload.nudge;
    target.probability[step] = payload.probability;
}

FLASHMEM void writeStep(SequencerPatternSnapshot& target, uint8_t step, const StepPayload& payload) {
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

    const auto region = snapshotPlaybackRegion(snapshot, length);
    if (!setPatternPlaybackRegion(target, region)) {
        // A no-op is valid; malformed input was normalized above.
        target.playStart = region.playStart;
        target.loopStart = region.loopStart;
        target.loopEnd = region.loopEnd;
    }
    target.stepsPerBeat.set(sanitizeStepsPerBeat(snapshot.stepsPerBeat));
    target.enabledMask.set(snapshot.enabledMask & lengthMask(length));
    target.setPatternVariationRanges(snapshot.variationRanges);
    target.setPatternScalePolicy(snapshot.scalePolicy);
    target.setPatternScaleOverride(snapshot.scaleOverride);
    target.setPitchEditMode(snapshot.pitchEditMode);
    target.setPatternSwingOffsetPercent(snapshot.swingOffsetPercent);
    target.setPatternNudgePercent(snapshot.patternNudgePercent);
    target.patternTimingRevision.set(snapshot.patternTimingRevision);
    target.graph.reset();
    target.graphRevision.set(snapshot.graphRevision);

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

    applySnapshotImpl(target.pattern, snapshot);

    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
}

FLASHMEM void applySnapshotToEditorPreservingGraphImpl(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot
) {
    auto graph = std::move(target.pattern.graph);
    applySnapshotToEditorImpl(target, snapshot);
    target.pattern.graph = std::move(graph);
}

}  // namespace

FLASHMEM void captureSnapshot(const SequencerPatternState& source, SequencerPatternSnapshot& out) {
    out.length = sanitizeSequencerLength(source.length.get());
    const auto region = patternPlaybackRegion(source);
    if (region.isValid()) {
        out.playStart = region.playStart;
        out.loopStart = region.loopStart;
        out.loopEnd = region.loopEnd;
    } else {
        const auto fallback = SequencerPatternPlaybackRegion::fullLength(out.length);
        out.playStart = fallback.playStart;
        out.loopStart = fallback.loopStart;
        out.loopEnd = fallback.loopEnd;
    }
    out.stepsPerBeat = sanitizeStepsPerBeat(source.stepsPerBeat.get());
    out.enabledMask = source.enabledMask.get();
    out.stepDataRevision = source.stepDataRevision.get();
    out.patternVariationRevision = source.patternVariationRevision.get();
    out.patternScaleRevision = source.patternScaleRevision.get();
    out.patternTimingRevision = source.patternTimingRevision.get();
    out.graphRevision = source.graphRevision.get();
    out.swingOffsetPercent =
        SequencerPatternState::clampPatternSwingOffsetPercent(source.swingOffsetPercent.get());
    out.patternNudgePercent =
        SequencerPatternState::clampPatternNudgePercent(source.patternNudgePercent.get());
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
    target.ccLaneRevision.set(source.ccLaneRevision.get());
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
    target.graphRevision.set(snapshot.graphRevision);
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

FLASHMEM bool applyTrackContentSnapshotWithGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!copyGraph(target, graph, snapshot.graphRevision)) return false;
    applySnapshotPreservingGraphImpl(target, snapshot);
    return true;
}

FLASHMEM void installTrackContentSnapshotWithOwnedGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph
) {
    applySnapshotImpl(target, snapshot);
    target.graph = std::move(graph);
    target.graphRevision.set(snapshot.graphRevision);
}

FLASHMEM void installTrackContentSnapshotWithOwnedPayload(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph,
    SequencerCcLaneBankPtr ccLanes
) {
    installTrackContentSnapshotWithOwnedGraph(
        target,
        snapshot,
        std::move(graph)
    );
    installSequencerCcLaneBank(target, std::move(ccLanes));
}

FLASHMEM bool copyPatternStatePreservingGraph(
    SequencerPatternState& target,
    const SequencerPatternState& source
) {
    SequencerCcLaneBankPtr ccLanes;
    if (!cloneSequencerCcLaneBank(ccLanes, source.ccLanes.get())) return false;
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source, snapshot);
    applySnapshotPreservingGraph(target, snapshot);
    installSequencerCcLaneBank(target, std::move(ccLanes));
    copySequencerCcLaneRevision(target, source);
    return true;
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

FLASHMEM bool applySnapshotToEditorWithGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!copyGraph(target.pattern, graph, snapshot.graphRevision)) return false;
    applySnapshotToEditorPreservingGraph(target, snapshot);
    return true;
}

FLASHMEM bool applyTrackContentSnapshotToEditorWithGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!copyGraph(target.pattern, graph, snapshot.graphRevision)) return false;
    applySnapshotToEditorPreservingGraphImpl(target, snapshot);
    return true;
}

FLASHMEM void installTrackContentSnapshotToEditorWithOwnedGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph
) {
    applySnapshotToEditorImpl(target, snapshot);
    target.pattern.graph = std::move(graph);
    target.pattern.graphRevision.set(snapshot.graphRevision);
}

FLASHMEM void installTrackContentSnapshotToEditorWithOwnedPayload(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph,
    SequencerCcLaneBankPtr ccLanes
) {
    installTrackContentSnapshotToEditorWithOwnedGraph(
        target,
        snapshot,
        std::move(graph)
    );
    installSequencerCcLaneBank(target.pattern, std::move(ccLanes));
}

FLASHMEM void installPatternStateToEditor(
    SequencerState& target,
    SequencerPatternState& staged
) {
    SequencerPatternSnapshot snapshot;
    captureSnapshot(staged, snapshot);
    auto graph = std::move(staged.graph);
    auto ccLanes = std::move(staged.ccLanes);
    applySnapshotToEditor(target, snapshot);
    target.pattern.graph = std::move(graph);
    target.pattern.graphRevision.set(snapshot.graphRevision);
    installSequencerCcLaneBank(target.pattern, std::move(ccLanes));
    target.pattern.ccLaneRevision.set(staged.ccLaneRevision.get());
}

FLASHMEM void mergePatternStateIntoCurrent(
    SequencerState& target,
    SequencerPatternState& staged
) {
    SequencerPatternSnapshot snapshot;
    captureSnapshot(staged, snapshot);
    auto graph = std::move(staged.graph);
    auto ccLanes = std::move(staged.ccLanes);
    mergeSnapshotIntoCurrent(target, snapshot);
    target.pattern.graph = std::move(graph);
    target.pattern.graphRevision.set(snapshot.graphRevision);
    installSequencerCcLaneBank(target.pattern, std::move(ccLanes));
    target.pattern.ccLaneRevision.set(staged.ccLaneRevision.get());
}

FLASHMEM void mergeSnapshotIntoCurrent(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
    const uint8_t focusedBefore = target.focusedStep.get();

    const uint8_t currentLength = sanitizeSequencerLength(target.pattern.length.get());
    const uint8_t incomingLength = sanitizeSequencerLength(snapshot.length);
    const uint8_t mergedLength = std::max(currentLength, incomingLength);

    auto mergedRegion = snapshotPlaybackRegion(snapshot, incomingLength);
    mergedRegion.contentLength = mergedLength;
    if (!setPatternPlaybackRegion(target.pattern, mergedRegion)) {
        // No-op is valid and keeps the existing canonical region.
    }

    auto mergedMask = target.pattern.enabledMask.get() & lengthMask(mergedLength);
    const auto incomingMask = snapshot.enabledMask & lengthMask(incomingLength);

    for (uint16_t i = 0; i < incomingLength; ++i) {
        const auto step = static_cast<uint8_t>(i);
        if (!incomingMask.test(step)) continue;

        writeStep(target.pattern, step, readSanitizedStep(snapshot, step));
        mergedMask.setBit(step, true);
    }

    target.pattern.enabledMask.set(mergedMask);
    target.setPatternVariationRanges(snapshot.variationRanges);
    target.setPatternScalePolicy(snapshot.scalePolicy);
    target.setPatternScaleOverride(snapshot.scaleOverride);
    target.setPitchEditMode(snapshot.pitchEditMode);
    target.setPatternSwingOffsetPercent(snapshot.swingOffsetPercent);
    target.setPatternNudgePercent(snapshot.patternNudgePercent);
    target.pattern.patternTimingRevision.set(snapshot.patternTimingRevision);
    target.pattern.graph.reset();
    target.pattern.graphRevision.set(snapshot.graphRevision);

    const uint8_t focused =
        (focusedBefore >= mergedLength) ? static_cast<uint8_t>(mergedLength - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.pattern.bumpStepDataRevision();
}

FLASHMEM bool duplicatePatternForward(SequencerState& target) {
    const uint8_t len = target.pattern.length.get();
    if (len == 0 || len >= SequencerState::MAX_STEPS) return false;

    const uint8_t targetStart = len;
    const uint8_t copyCount = static_cast<uint8_t>(
        std::min<uint16_t>(len, static_cast<uint16_t>(SequencerState::MAX_STEPS - targetStart))
    );
    if (copyCount == 0) return false;

    auto mask = target.pattern.enabledMask.get();
    bool dataChanged = false;

    for (uint8_t i = 0; i < copyCount; ++i) {
        const uint8_t src = i;
        const uint8_t dst = static_cast<uint8_t>(targetStart + i);
        const StepPayload sourceStep = readStep(target.pattern, src);

        if (!sameStep(readStep(target.pattern, dst), sourceStep)) {
            dataChanged = true;
        }

        writeStep(target.pattern, dst, sourceStep);

        const bool srcEnabled = mask.test(src);
        const bool dstEnabledBefore = mask.test(dst);
        if (srcEnabled != dstEnabledBefore) {
            dataChanged = true;
        }

        mask.setBit(dst, srcEnabled);
    }

    target.pattern.enabledMask.set(mask);

    const uint8_t requiredLength = static_cast<uint8_t>(targetStart + copyCount);
    if (target.pattern.ccLanes && duplicateSequencerCcLaneBankRange(
            *target.pattern.ccLanes,
            0,
            targetStart,
            copyCount
        )) {
        target.pattern.bumpCcLaneRevision();
    }
    if (requiredLength > len) {
        target.pattern.setContentLength(requiredLength);
    }

    target.page.set(target.pageForStep(targetStart));
    target.focusedStep.set(targetStart);

    if (dataChanged) {
        target.pattern.bumpStepDataRevision();
    }

    return true;
}

FLASHMEM bool rotatePatternState(SequencerPatternState& target, int offsetSteps) {
    const uint8_t len = target.length.get();
    if (len <= 1) return false;

    int normalizedOffset = offsetSteps % static_cast<int>(len);
    if (normalizedOffset < 0) {
        normalizedOffset += len;
    }
    if (normalizedOffset == 0) return false;

    std::array<StepPayload, SequencerState::MAX_STEPS> nextSteps{};
    const auto activeMask = lengthMask(len);
    const auto sourceMask = target.enabledMask.get();
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

    target.enabledMask.set(nextMask);
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

FLASHMEM bool rotatePattern(SequencerState& target, int offsetSteps) {
    return rotatePatternState(target.pattern, offsetSteps);
}

FLASHMEM SequencerSnapshotBatchMutationResult
resizeSequencerRootContentUnversioned(
    SequencerState& target,
    uint8_t requiredLength
) noexcept {
    const uint8_t oldLength = target.pattern.length.get();
    if (requiredLength == 0U ||
        requiredLength > SequencerState::MAX_STEPS ||
        requiredLength < oldLength) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            oldLength,
            oldLength
        );
    }
    if (!validBatchPatternState(target.pattern)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            oldLength,
            oldLength
        );
    }
    if (!validBatchRootGraph(target.pattern)) {
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

    const auto nextRegion = resizedPatternPlaybackRegion(
        patternPlaybackRegion(target.pattern),
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
    auto enabledMask = target.pattern.enabledMask.get();
    for (uint16_t step = oldLength; step < requiredLength; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        writeStep(target.pattern, stepIndex, defaultStep());
        enabledMask.setBit(stepIndex, false);
        graphChanged = clearRootNode(target.pattern, stepIndex) || graphChanged;
    }
    applyBatchPlaybackRegion(target.pattern, nextRegion, enabledMask);

    const SequencerSnapshotBatchDomains domains{
        .stepData = true,
        .graph = graphChanged,
        // Page creation and Step-paste extension never reinterpret or erase
        // Pattern-owned CC events, including cold events beyond the former
        // Content Length. Only Page deletion owns a CC shift/removal.
        .ccLanes = false,
        .timing = true,
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
        const uint8_t length = target.pattern.length.get();
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
        std::max(requiredLength, target.pattern.length.get())
    );
}

FLASHMEM SequencerSnapshotBatchMutationResult
clearSequencerRootStepSpanUnversioned(
    SequencerState& target,
    uint8_t startStep,
    uint8_t stepCount
) noexcept {
    const uint8_t length = target.pattern.length.get();
    const uint16_t end = static_cast<uint16_t>(startStep) + stepCount;
    if (stepCount == 0U || end > length || end > SequencerState::MAX_STEPS) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT,
            length,
            length
        );
    }
    if (!validBatchPatternState(target.pattern)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            length,
            length
        );
    }
    if (!validBatchRootGraph(target.pattern)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_GRAPH,
            length,
            length
        );
    }

    auto enabledMask = target.pattern.enabledMask.get();
    bool stepChanged = false;
    bool graphChanged = false;
    for (uint16_t step = startStep; step < end; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (enabledMask.test(stepIndex)) {
            enabledMask.setBit(stepIndex, false);
            stepChanged = true;
        }
        if (!sameStep(readStep(target.pattern, stepIndex), defaultStep())) {
            writeStep(target.pattern, stepIndex, defaultStep());
            stepChanged = true;
        }
        graphChanged = clearRootNode(target.pattern, stepIndex) || graphChanged;
    }
    if (stepChanged) {
        target.pattern.enabledMask.set(enabledMask);
    }

    const SequencerSnapshotBatchDomains domains{
        .stepData = stepChanged,
        .graph = graphChanged,
        .ccLanes = false,
        .timing = false,
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
    const uint8_t oldLength = target.pattern.length.get();
    if (!validBatchPatternState(target.pattern)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_PATTERN_STATE,
            oldLength,
            oldLength
        );
    }
    if (!validBatchRootGraph(target.pattern)) {
        return batchResult(
            SequencerSnapshotBatchMutationStatus::INVALID_GRAPH,
            oldLength,
            oldLength
        );
    }

    const uint8_t pageCount = target.pattern.activePageCount();
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

    auto nextRegion = patternPlaybackRegion(target.pattern);
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
        nextRegion = removedPatternPlaybackRegion(
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
    if (target.pattern.ccLanes) {
        const auto ccResult = removeSequencerCcLaneBankStepsUnversioned(
            *target.pattern.ccLanes,
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

    const auto sourceEnabledMask = target.pattern.enabledMask.get();
    oc::note::sequencer::StepBitMask128 nextEnabledMask{};
    bool graphChanged = false;
    uint8_t destination = 0U;
    for (uint16_t source = 0; source < oldLength; ++source) {
        const auto sourceStep = static_cast<uint8_t>(source);
        if (removalMask.test(sourceStep)) continue;

        if (destination != sourceStep) {
            writeStep(target.pattern, destination, readStep(target.pattern, sourceStep));
            if (canEditRootNodes(target.pattern)) {
                graphChanged = assignRootNode(
                    target.pattern,
                    destination,
                    target.pattern.graph->stepNodes[sourceStep]
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
        writeStep(target.pattern, stepIndex, defaultStep());
        graphChanged = clearRootNode(target.pattern, stepIndex) || graphChanged;
    }
    applyBatchPlaybackRegion(target.pattern, nextRegion, nextEnabledMask);

    const SequencerSnapshotBatchDomains domains{
        .stepData = true,
        .graph = graphChanged,
        .ccLanes = ccChanged,
        .timing = true,
    };
    return batchResult(
        SequencerSnapshotBatchMutationStatus::APPLIED,
        oldLength,
        newLength,
        domains
    );
}

FLASHMEM void publishSequencerSnapshotBatchRevisions(
    SequencerPatternState& pattern,
    const SequencerSnapshotBatchDomains& domains
) noexcept {
    if (domains.stepData) pattern.bumpStepDataRevision();
    if (domains.graph) pattern.bumpGraphRevision();
    if (domains.ccLanes && pattern.ccLanes) {
        ++pattern.ccLanes->revision;
        pattern.bumpCcLaneRevision();
    }
    if (domains.timing) pattern.bumpPatternTimingRevision();
}

FLASHMEM bool clearStepRange(SequencerState& target, uint8_t startStep, uint8_t endStep) {
    const uint8_t len = target.pattern.length.get();
    if (len == 0) return false;

    const uint8_t start = static_cast<uint8_t>(std::min(startStep, endStep));
    const uint8_t end = static_cast<uint8_t>(std::max(startStep, endStep));
    if (start >= len || start >= SequencerState::MAX_STEPS) return false;

    const uint8_t clampedEnd = static_cast<uint8_t>(std::min<uint16_t>(end, len - 1));
    auto mask = target.pattern.enabledMask.get();
    bool dataChanged = false;
    bool maskChanged = false;
    bool graphChanged = false;

    for (uint16_t step = start; step <= clampedEnd; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (mask.test(stepIndex)) {
            mask.setBit(stepIndex, false);
            maskChanged = true;
        }

        if (!sameStep(readStep(target.pattern, stepIndex), defaultStep())) {
            writeStep(target.pattern, stepIndex, defaultStep());
            dataChanged = true;
        }

        graphChanged = clearRootNode(target.pattern, stepIndex) || graphChanged;
    }

    if (maskChanged) {
        target.pattern.enabledMask.set(mask);
    }

    target.focusedStep.set(start);
    target.page.set(target.pageForStep(start));

    if (dataChanged || maskChanged) {
        target.pattern.bumpStepDataRevision();
    }

    if (graphChanged) {
        target.pattern.bumpGraphRevision();
        compactSequencerGraph(target);
    }

    return dataChanged || maskChanged || graphChanged;
}

FLASHMEM bool appendPage(SequencerState& target) {
    const uint8_t len = target.pattern.length.get();
    const uint8_t pageCount = target.activePageCount();
    if (len == 0 || pageCount >= SequencerState::PAGE_COUNT) return false;

    const uint8_t newLength = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS,
        static_cast<uint16_t>(len) + SequencerState::STEPS_PER_PAGE
    ));
    if (newLength <= len) return false;

    auto mask = target.pattern.enabledMask.get();
    for (uint16_t step = len; step < newLength; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        writeStep(target.pattern, stepIndex, defaultStep());
        mask.setBit(stepIndex, false);
    }

    const bool resized = target.pattern.setContentLength(newLength);
    assert(resized);
    if (!resized) return false;
    target.pattern.enabledMask.set(mask & lengthMask(newLength));

    const uint8_t newPage = pageCount;
    const uint8_t focused = static_cast<uint8_t>(newPage * SequencerState::STEPS_PER_PAGE);
    target.focusedStep.set(focused);
    target.page.set(newPage);
    target.pattern.bumpStepDataRevision();
    return true;
}

FLASHMEM bool insertPage(SequencerState& target, uint8_t pageIndex) {
    const uint8_t len = target.pattern.length.get();
    const uint8_t pageCount = target.activePageCount();
    if (len == 0 || pageCount >= SequencerState::PAGE_COUNT || pageIndex > pageCount) {
        return false;
    }

    if (pageIndex == pageCount) {
        return appendPage(target);
    }

    const uint8_t insertStart = static_cast<uint8_t>(pageIndex * SequencerState::STEPS_PER_PAGE);
    if (insertStart > len) return false;

    const uint8_t newLength = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS,
        static_cast<uint16_t>(len) + SequencerState::STEPS_PER_PAGE
    ));
    if (newLength <= len) return false;

    auto mask = target.pattern.enabledMask.get();
    bool graphChanged = false;

    for (int dst = static_cast<int>(newLength) - 1;
         dst >= static_cast<int>(insertStart + SequencerState::STEPS_PER_PAGE);
         --dst) {
        const uint8_t dstIndex = static_cast<uint8_t>(dst);
        const uint8_t srcIndex =
            static_cast<uint8_t>(dst - static_cast<int>(SequencerState::STEPS_PER_PAGE));
        writeStep(target.pattern, dstIndex, readStep(target.pattern, srcIndex));
        mask.setBit(dstIndex, mask.test(srcIndex));
        if (canEditRootNodes(target.pattern)) {
            graphChanged = assignRootNode(
                target.pattern,
                dstIndex,
                target.pattern.graph->stepNodes[srcIndex]
            ) || graphChanged;
        }
    }

    const uint8_t clearEnd = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(insertStart + SequencerState::STEPS_PER_PAGE - 1)
    ));
    for (uint16_t step = insertStart; step <= clearEnd; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        writeStep(target.pattern, stepIndex, defaultStep());
        mask.setBit(stepIndex, false);
        graphChanged = clearRootNode(target.pattern, stepIndex) || graphChanged;
    }

    if (target.pattern.ccLanes && insertSequencerCcLaneBankSpan(
            *target.pattern.ccLanes,
            len,
            insertStart,
            static_cast<uint8_t>(newLength - len)
        )) {
        target.pattern.bumpCcLaneRevision();
    }
    const bool regionInserted = insertPatternRegionSpan(
        target.pattern,
        insertStart,
        static_cast<uint8_t>(newLength - len)
    );
    assert(regionInserted);
    if (!regionInserted) return false;
    target.pattern.enabledMask.set(mask & lengthMask(newLength));
    target.focusedStep.set(insertStart);
    target.page.set(pageIndex);
    target.pattern.bumpStepDataRevision();
    if (graphChanged) {
        target.pattern.bumpGraphRevision();
        compactSequencerGraph(target);
    }
    return true;
}

FLASHMEM bool deletePage(SequencerState& target, uint8_t pageIndex) {
    const uint8_t len = target.pattern.length.get();
    if (len <= SequencerState::STEPS_PER_PAGE) return false;

    const uint8_t pageCount = target.activePageCount();
    if (pageCount <= 1 || pageIndex >= pageCount) return false;

    const uint8_t pageStart = static_cast<uint8_t>(pageIndex * SequencerState::STEPS_PER_PAGE);
    if (pageStart >= len) return false;

    const uint8_t deleteSpan = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::STEPS_PER_PAGE,
        static_cast<uint16_t>(len - pageStart)
    ));
    const uint8_t newLength = static_cast<uint8_t>(len - deleteSpan);
    auto mask = target.pattern.enabledMask.get();
    bool graphChanged = false;

    for (uint16_t dst = pageStart; dst + deleteSpan < len; ++dst) {
        const auto dstIndex = static_cast<uint8_t>(dst);
        const uint8_t src = static_cast<uint8_t>(dst + deleteSpan);
        writeStep(target.pattern, dstIndex, readStep(target.pattern, src));
        mask.setBit(dstIndex, mask.test(src));
        if (canEditRootNodes(target.pattern)) {
            graphChanged = assignRootNode(
                target.pattern,
                dstIndex,
                target.pattern.graph->stepNodes[src]
            ) || graphChanged;
        }
    }

    for (uint16_t step = newLength; step < SequencerState::MAX_STEPS; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        writeStep(target.pattern, stepIndex, defaultStep());
        mask.setBit(stepIndex, false);
        graphChanged = clearRootNode(target.pattern, stepIndex) || graphChanged;
    }

    if (target.pattern.ccLanes && removeSequencerCcLaneBankSpan(
            *target.pattern.ccLanes,
            len,
            pageStart,
            deleteSpan
        )) {
        target.pattern.bumpCcLaneRevision();
    }
    const bool regionRemoved = removePatternRegionSpan(
        target.pattern,
        pageStart,
        deleteSpan
    );
    assert(regionRemoved);
    if (!regionRemoved) return false;
    target.pattern.enabledMask.set(mask & lengthMask(newLength));

    const uint8_t focused = static_cast<uint8_t>(std::min<uint16_t>(
        pageStart,
        static_cast<uint16_t>(newLength - 1U)
    ));
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.pattern.bumpStepDataRevision();
    if (graphChanged) {
        target.pattern.bumpGraphRevision();
        compactSequencerGraph(target);
    }
    return true;
}

}  // namespace core::state::sequencer

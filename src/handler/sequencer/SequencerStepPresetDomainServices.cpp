#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"

#include <cstdio>
#include <cstring>

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/type/Result.hpp>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "midi/MidiUtils.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/SequencerGraphAssetCodec.hpp"
#include "state/CoreState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphAsset.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace {

namespace asset_codec = core::persistence::sequencer_graph_asset_codec;

constexpr uint16_t STEP_PRESET_MAX_ENCODED_SIZE = asset_codec::MAX_ENCODED_SIZE;
using core::state::sequencer::SequencerGraphAssetStatus;
using core::state::sequencer::SequencerStepGraphPreset;
using core::state::sequencer::SequencerStepPresetCompatibility;
using core::state::sequencer::SequencerStepPresetContentFlags;
using core::state::sequencer::SequencerStepPresetDescriptor;
using core::state::sequencer::SequencerStepPresetFootprint;
using core::state::sequencer::SequencerStepPresetScope;
using core::state::sequencer::SequencerStepPresetTarget;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;

struct StepPresetBuffer {
    uint8_t bytes[STEP_PRESET_MAX_ENCODED_SIZE];
};

FLASHMEM uint32_t hashPresetPayload(const char* presetId, const uint8_t* bytes, uint16_t size) {
    uint32_t hash = core::state::sequencer::sequencerStepPresetIdHash(presetId);
    constexpr uint32_t FNV_PRIME = 16777619U;
    for (uint16_t i = 0; i < size; ++i) { hash = (hash ^ bytes[i]) * FNV_PRIME; }
    return hash;
}

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings effectiveScale(
    const core::state::CoreState& state) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        state.sequencerTracks.projectScaleSettings(), state.sequencer.pattern.scalePolicy,
        state.sequencer.pattern.scaleOverride);
}

FLASHMEM bool sameScale(oc::note::sequencer::StepSequencerScaleSettings lhs,
                        oc::note::sequencer::StepSequencerScaleSettings rhs) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

FLASHMEM bool destinationUsesScaleRelativePitch(
    const core::state::CoreState& state, oc::note::sequencer::StepSequencerScaleSettings scale) {
    return core::state::sequencer::pitchContextUsesScaleDegrees(
        state.sequencer.pattern.pitchEditMode, scale);
}

FLASHMEM bool presetPitchContextMatchesDestination(
    const SequencerStepGraphPreset& preset, const core::state::CoreState& state,
    oc::note::sequencer::StepSequencerScaleSettings destinationScale) {
    const bool presetScaleRelative =
        preset.scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE;
    return presetScaleRelative == destinationUsesScaleRelativePitch(state, destinationScale);
}

FLASHMEM bool sameFrozenTarget(
    const SequencerStepPresetTarget& lhs,
    const SequencerStepPresetTarget& rhs
) {
    return lhs.valid == rhs.valid &&
        lhs.trackIndex == rhs.trackIndex &&
        lhs.stepIndex == rhs.stepIndex &&
        lhs.contentContext == rhs.contentContext &&
        lhs.ownerNodeId == rhs.ownerNodeId &&
        lhs.sequenceId == rhs.sequenceId &&
        lhs.cycleSetId == rhs.cycleSetId &&
        lhs.targetNodeId == rhs.targetNodeId &&
        lhs.destinationOwnsPitch == rhs.destinationOwnsPitch &&
        lhs.destinationNote == rhs.destinationNote &&
        lhs.drumLaneIndex == rhs.drumLaneIndex &&
        lhs.drumRootStepIndex == rhs.drumRootStepIndex &&
        lhs.drumRootSlot == rhs.drumRootSlot;
}

FLASHMEM void formatScale(oc::note::sequencer::StepSequencerScaleSettings scale, char* out,
                          size_t outSize) {
    scale.clamp();
    std::snprintf(out, outSize, "%s %s",
                  core::state::sequencer::scale_catalog::rootLabel(scale.root),
                  core::state::sequencer::scale_catalog::scaleTypeLabel(scale.type));
}

FLASHMEM void formatNoteName(int note, char* out, size_t outSize) {
    const int clamped = std::clamp(note, 0, 127);
    core::midi::formatNoteName(
        out,
        outSize,
        static_cast<uint8_t>(clamped)
    );
}

FLASHMEM core::app::ExtmemUniquePtr<StepPresetBuffer> makeStepPresetBuffer() {
    return core::app::makeExtmemUniqueForOverwrite<StepPresetBuffer>();
}

FLASHMEM SequencerStepPresetStatus statusFromFileError(oc::type::ErrorCode code) {
    switch (code) {
        case oc::type::ErrorCode::RESOURCE_NOT_FOUND: return SequencerStepPresetStatus::EMPTY;
        case oc::type::ErrorCode::HARDWARE_BUSY: return SequencerStepPresetStatus::QUEUED;
        case oc::type::ErrorCode::INVALID_STATE:
        case oc::type::ErrorCode::HARDWARE_NOT_FOUND:
        case oc::type::ErrorCode::HARDWARE_INIT_FAILED:
            return SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        default: return SequencerStepPresetStatus::FAILED;
    }
}

FLASHMEM SequencerStepPresetStatus statusFromAsset(SequencerGraphAssetStatus status) {
    switch (status) {
        case SequencerGraphAssetStatus::INCOMPATIBLE_TARGET:
            return SequencerStepPresetStatus::INCOMPATIBLE;
        case SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED:
        case SequencerGraphAssetStatus::BUFFER_TOO_SMALL:
            return SequencerStepPresetStatus::CAPACITY;
        case SequencerGraphAssetStatus::INVALID_ARGUMENT:
        case SequencerGraphAssetStatus::INVALID_FORMAT: return SequencerStepPresetStatus::CORRUPT;
        case SequencerGraphAssetStatus::UNSUPPORTED_VERSION:
            return SequencerStepPresetStatus::UNSUPPORTED_VERSION;
        case SequencerGraphAssetStatus::RESOURCE_EXHAUSTED:
            return SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        case SequencerGraphAssetStatus::OK: return SequencerStepPresetStatus::OK;
        default: return SequencerStepPresetStatus::FAILED;
    }
}

FLASHMEM void copyText(char* target, size_t targetSize, const char* source) {
    if (target == nullptr || targetSize == 0) return;
    const char* text = source ? source : "";
    std::strncpy(target, text, targetSize - 1U);
    target[targetSize - 1U] = '\0';
}

FLASHMEM void copyPresetId(char* target, size_t targetSize, const char* source) {
    copyText(target, targetSize, source);
}

FLASHMEM bool samePresetBytesOutsideSemanticName(const uint8_t* before, uint16_t beforeSize,
                                                 const uint8_t* after, uint16_t afterSize) {
    constexpr uint16_t semanticOffset = static_cast<uint16_t>(
        asset_codec::BASE_HEADER_SIZE + 4U + SequencerStepGraphPreset::TECHNICAL_ID_SIZE);
    constexpr uint16_t semanticEnd =
        static_cast<uint16_t>(semanticOffset + SequencerStepGraphPreset::SEMANTIC_NAME_SIZE);
    if (before == nullptr || after == nullptr || beforeSize != afterSize ||
        beforeSize < semanticEnd) {
        return false;
    }
    for (uint16_t i = 0; i < beforeSize; ++i) {
        if (i >= semanticOffset && i < semanticEnd) continue;
        if (before[i] != after[i]) return false;
    }
    return true;
}

FLASHMEM void copyContentViewState(
    core::state::sequencer::SequencerContentViewState& target,
    const core::state::sequencer::SequencerContentViewState& source) {
    target.kind.set(source.kind.get());
    target.parentStep.set(source.parentStep.get());
    target.ownerNodeId.set(source.ownerNodeId.get());
    target.sequenceId.set(source.sequenceId.get());
    target.cycleSetId.set(source.cycleSetId.get());
    target.length.set(source.length.get());
    target.depth.set(source.depth.get());
    target.revision.set(source.revision.get());
    target.rootPageSnapshot = source.rootPageSnapshot;
    target.rootFocusSnapshot = source.rootFocusSnapshot;
    target.stackDepth = source.stackDepth;
    target.frames = source.frames;
    target.drumOwnerActive = source.drumOwnerActive;
    target.drumOwnerTrack = source.drumOwnerTrack;
    target.drumOwnerLane = source.drumOwnerLane;
    target.drumOwnerStep = source.drumOwnerStep;
    target.drumOwnerRootSlot = source.drumOwnerRootSlot;
}

FLASHMEM void copyEditorContextForStaging(core::state::sequencer::SequencerState& target,
                                          const core::state::sequencer::SequencerState& source) {
    target.page.set(source.page.get());
    target.focusedStep.set(source.focusedStep.get());
    target.activeStepProperty.set(source.activeStepProperty.get());
    target.stepEdit.stepIndex.set(source.stepEdit.stepIndex.get());
    copyContentViewState(target.contentView, source.contentView);
}

FLASHMEM void appendSummary(char* target, size_t targetSize, const char* token) {
    if (target == nullptr || targetSize == 0 || token == nullptr || token[0] == '\0') { return; }
    const size_t current = std::strlen(target);
    if (current >= targetSize - 1U) return;
    std::snprintf(target + current, targetSize - current, "%s%s", current > 0 ? " · " : "", token);
}

FLASHMEM bool variationPresent(const oc::note::sequencer::StepSequencerStepNode& node) {
    return node.localVariation.pitchSemitones > 0 || node.localVariation.velocity > 0 ||
           node.localVariation.gatePercent > 0 || node.localVariation.nudge > 0;
}

FLASHMEM bool nodePopulated(const oc::note::sequencer::StepSequencerStepNode& node) {
    return node.flags != 0 || node.noteOffset != 0 || node.velocityOffset != 0 ||
           node.gateOffset != 0 || node.nudgeOffset != 0 || node.probabilityOffset != 0 ||
           variationPresent(node);
}

FLASHMEM bool targetPopulated(const core::state::CoreState& state,
                              const SequencerStepPresetTarget& target) {
    const auto& sequencer = state.sequencer;
    if (!target.valid ||
        target.stepIndex >= core::state::sequencer::activeContentLength(sequencer)) {
        if (!target.destinationOwnsPitch ||
            target.contentContext !=
                core::state::sequencer::SequencerStepPresetTargetContext::ROOT) {
            return false;
        }
    }

    bool populated = false;
    if (target.destinationOwnsPitch &&
        target.contentContext ==
            core::state::sequencer::SequencerStepPresetTargetContext::ROOT) {
        const auto& drum = state.sequencerTracks.drumTrack(target.trackIndex);
        if (target.drumLaneIndex < drum.kit.laneCount &&
            target.drumRootStepIndex <
                drum.pattern.effectiveLength(target.drumLaneIndex)) {
            const auto& lane = drum.pattern.lanes[target.drumLaneIndex];
            const uint8_t step = target.drumRootStepIndex;
            populated =
                drum.pattern.stepEnabled(target.drumLaneIndex, step) ||
                lane.velocity[step] !=
                    core::state::sequencer::DRUM_DEFAULT_VELOCITY ||
                lane.gate[step] !=
                    core::state::sequencer::DRUM_DEFAULT_GATE_PERCENT ||
                lane.nudge[step] != 0 ||
                lane.probability[step] !=
                    core::state::sequencer::DRUM_DEFAULT_PROBABILITY;
        }
    } else if (target.contentContext ==
               core::state::sequencer::SequencerStepPresetTargetContext::ROOT) {
        const uint8_t step = target.stepIndex;
        populated =
            sequencer.pattern.isEnabled(step) ||
            sequencer.pattern.note[step] != core::state::sequencer::SequencerState::DEFAULT_NOTE ||
            sequencer.pattern.velocity[step] !=
                core::state::sequencer::SequencerState::DEFAULT_VELOCITY ||
            sequencer.pattern.gate[step] !=
                core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT ||
            sequencer.pattern.nudge[step] != 0 ||
            sequencer.pattern.probability[step] !=
                core::state::sequencer::SequencerState::DEFAULT_PROBABILITY;
    }

    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* node = graph ? graph->stepNode(target.targetNodeId) : nullptr;
    return populated || (node != nullptr && nodePopulated(*node));
}

FLASHMEM bool presetHasAdvancedGraphPayload(
    const SequencerStepGraphPreset& preset
) {
    const auto* root = preset.graph.stepNode(
        SequencerStepGraphPreset::ASSET_ROOT_NODE_ID
    );
    return root != nullptr &&
        !core::state::sequencer::isDefaultSequencerGraphNodePayload(*root);
}

FLASHMEM bool graphCapacityAvailable(const core::state::sequencer::SequencerState& sequencer,
                                     const SequencerStepGraphPreset& preset) {
    using Limits = oc::note::sequencer::StepSequencerGraphLimits;
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const uint32_t existingNodes =
        graph ? graph->stepNodeCount : core::state::sequencer::SequencerPatternState::MAX_STEPS;
    const uint32_t existingSequences = graph ? graph->sequenceCount : 1U;
    const uint32_t existingCycles = graph ? graph->cycleSetCount : 0U;
    const uint32_t incomingNodes = preset.graph.stepNodeCount > 0
                                       ? static_cast<uint32_t>(preset.graph.stepNodeCount - 1U)
                                       : 0U;
    return existingNodes + incomingNodes <= Limits::MAX_STEP_NODES &&
           existingSequences + preset.graph.sequenceCount <= Limits::MAX_SEQUENCES &&
           existingCycles + preset.graph.cycleSetCount <= Limits::MAX_CYCLE_SETS;
}

FLASHMEM void fillContentFacts(const SequencerStepGraphPreset& preset,
                               SequencerStepPresetDescriptor& descriptor) {
    descriptor.scope =
        preset.rootContext ? SequencerStepPresetScope::ROOT : SequencerStepPresetScope::CHILD;
    descriptor.contentFlags = core::state::sequencer::STEP_PRESET_CONTENT_GRAPH;
    if (preset.rootValuesValid) {
        descriptor.contentFlags = static_cast<uint16_t>(
            descriptor.contentFlags | core::state::sequencer::STEP_PRESET_CONTENT_STEP_VALUES);
    }
    if (preset.graph.sequenceCount > 0) {
        descriptor.contentFlags = static_cast<uint16_t>(
            descriptor.contentFlags | core::state::sequencer::STEP_PRESET_CONTENT_MICRO_SEQUENCE);
    }
    if (preset.graph.cycleSetCount > 0) {
        descriptor.contentFlags = static_cast<uint16_t>(
            descriptor.contentFlags | core::state::sequencer::STEP_PRESET_CONTENT_CYCLE |
            core::state::sequencer::STEP_PRESET_CONTENT_RANDOM);
    }

    bool chord = false;
    bool random = preset.rootValuesValid &&
                  preset.probability != core::state::sequencer::SequencerState::DEFAULT_PROBABILITY;
    uint8_t previewStates = 1;
    for (uint16_t i = 0; i < preset.graph.stepNodeCount; ++i) {
        const auto* node = preset.graph.stepNode(i);
        if (node == nullptr) continue;
        chord = chord || node->has(STEP_NODE_CHORD_MODE) || node->has(STEP_NODE_CHORD_LOCAL);
        random = random || variationPresent(*node) || node->probabilityOffset != 0;
    }
    for (uint8_t i = 0; i < preset.graph.cycleSetCount; ++i) {
        const auto* cycle = preset.graph.cycleSet(i);
        if (cycle != nullptr) previewStates = std::max(previewStates, cycle->length);
    }
    if (chord) {
        descriptor.contentFlags = static_cast<uint16_t>(
            descriptor.contentFlags | core::state::sequencer::STEP_PRESET_CONTENT_CHORD);
    }
    if (random) {
        descriptor.contentFlags = static_cast<uint16_t>(
            descriptor.contentFlags | core::state::sequencer::STEP_PRESET_CONTENT_RANDOM);
    }
    descriptor.previewStateCount = previewStates;
    descriptor.previewStateIndex =
        static_cast<uint8_t>(descriptor.previewStateIndex % previewStates);

    appendSummary(descriptor.contentSummary, sizeof(descriptor.contentSummary),
                  preset.rootContext ? "Root" : "Child");
    if ((descriptor.contentFlags & core::state::sequencer::STEP_PRESET_CONTENT_STEP_VALUES) != 0) {
        appendSummary(descriptor.contentSummary, sizeof(descriptor.contentSummary), "Values");
    }
    if ((descriptor.contentFlags & core::state::sequencer::STEP_PRESET_CONTENT_MICRO_SEQUENCE) !=
        0) {
        appendSummary(descriptor.contentSummary, sizeof(descriptor.contentSummary), "Micro");
    }
    if ((descriptor.contentFlags & core::state::sequencer::STEP_PRESET_CONTENT_CYCLE) != 0) {
        appendSummary(descriptor.contentSummary, sizeof(descriptor.contentSummary), "Cycle");
    }
    if ((descriptor.contentFlags & core::state::sequencer::STEP_PRESET_CONTENT_CHORD) != 0) {
        appendSummary(descriptor.contentSummary, sizeof(descriptor.contentSummary), "Chord");
    }
    if ((descriptor.contentFlags & core::state::sequencer::STEP_PRESET_CONTENT_RANDOM) != 0) {
        appendSummary(descriptor.contentSummary, sizeof(descriptor.contentSummary), "Random");
    }
}

FLASHMEM void fillPreviewExample(const SequencerStepGraphPreset& preset,
                                 SequencerStepPresetDescriptor& descriptor) {
    int previewNote = preset.rootValuesValid ? preset.note : 0;
    if (preset.graph.stepNodeCount > 1) {
        const uint32_t seed = descriptor.previewKey.assetHash ^ descriptor.previewKey.targetHash ^
                              descriptor.previewKey.projectRevision ^
                              (static_cast<uint32_t>(descriptor.previewStateIndex) * 0x9E3779B9U);
        const uint16_t nodeIndex = static_cast<uint16_t>(
            1U + seed % static_cast<uint32_t>(preset.graph.stepNodeCount - 1U));
        const auto* node = preset.graph.stepNode(nodeIndex);
        if (node != nullptr && node->has(STEP_NODE_NOTE_OFFSET)) {
            previewNote =
                preset.scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
                    ? oc::note::sequencer::moveByScaleDegrees(
                          static_cast<uint8_t>(std::clamp(previewNote, 0, 127)), node->noteOffset,
                          preset.sourceScale)
                    : previewNote + node->noteOffset;
        }
    }
    descriptor.previewNote = static_cast<int16_t>(std::clamp(previewNote, 0, 127));
    if (preset.rootValuesValid) {
        char noteName[8]{};
        formatNoteName(descriptor.previewNote, noteName, sizeof(noteName));
        std::snprintf(descriptor.previewSummary, sizeof(descriptor.previewSummary),
                      "Preview %u/%u · %s",
                      static_cast<unsigned>(descriptor.previewStateIndex + 1U),
                      static_cast<unsigned>(descriptor.previewStateCount), noteName);
    } else {
        std::snprintf(descriptor.previewSummary, sizeof(descriptor.previewSummary),
                      "Preview %u/%u · Child example",
                      static_cast<unsigned>(descriptor.previewStateIndex + 1U),
                      static_cast<unsigned>(descriptor.previewStateCount));
    }
}

FLASHMEM SequencerStepPresetCompatibility
compatibilityForAssetStatus(SequencerGraphAssetStatus status) {
    switch (status) {
        case SequencerGraphAssetStatus::UNSUPPORTED_VERSION:
            return SequencerStepPresetCompatibility::UNSUPPORTED_VERSION;
        case SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED:
        case SequencerGraphAssetStatus::BUFFER_TOO_SMALL:
            return SequencerStepPresetCompatibility::BLOCKED_CAPACITY;
        case SequencerGraphAssetStatus::INVALID_ARGUMENT:
        case SequencerGraphAssetStatus::INVALID_FORMAT:
            return SequencerStepPresetCompatibility::CORRUPT;
        default: return SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE;
    }
}

FLASHMEM void setCompatibilityReason(SequencerStepPresetDescriptor& descriptor) {
    copyText(
        descriptor.compatibilityReason, sizeof(descriptor.compatibilityReason),
        core::state::sequencer::sequencerStepPresetCompatibilityLabel(descriptor.compatibility));
}

FLASHMEM bool capturePresetForTarget(
    const core::state::CoreState& state,
    const SequencerStepPresetTarget& target,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    SequencerStepGraphPreset& preset,
    core::state::sequencer::SequencerGraphAssetReport& report
) {
    bool captured = false;
    if (target.destinationOwnsPitch &&
        target.contentContext ==
            core::state::sequencer::SequencerStepPresetTargetContext::ROOT) {
        const auto& drum = state.sequencerTracks.drumTrack(target.trackIndex);
        if (target.drumLaneIndex >= drum.kit.laneCount ||
            target.drumRootStepIndex >=
                drum.pattern.effectiveLength(target.drumLaneIndex)) {
            report.status = SequencerGraphAssetStatus::INCOMPATIBLE_TARGET;
            return false;
        }
        const auto& lane = drum.pattern.lanes[target.drumLaneIndex];
        const uint8_t step = target.drumRootStepIndex;
        const core::state::sequencer::SequencerStepGraphRootValues values{
            .enabled = drum.pattern.stepEnabled(target.drumLaneIndex, step),
            .note = target.destinationNote,
            .velocity = lane.velocity[step],
            .gate = lane.gate[step],
            .nudge = lane.nudge[step],
            .probability = lane.probability[step],
        };
        const auto sourceNodeId = target.drumRootSlot == 0xFFU
            ? oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID
            : core::state::sequencer::rootStepNodeId(target.drumRootSlot);
        captured = core::state::sequencer::captureRootStepGraphPreset(
            state.sequencer.pattern,
            sourceNodeId,
            values,
            sourceScale,
            preset,
            &report
        );
    } else {
        captured = core::state::sequencer::captureStepGraphPreset(
            state.sequencer,
            target.stepIndex,
            sourceScale,
            preset,
            &report
        );
    }
    if (!captured) return false;
    if (target.destinationOwnsPitch &&
        !core::state::sequencer::projectStepGraphPresetToDestinationPitch(
            preset,
            target.destinationNote
        )) {
        report.status = SequencerGraphAssetStatus::INVALID_ARGUMENT;
        return false;
    }
    return true;
}

FLASHMEM SequencerStepPresetStatus historyStatusFor(
    core::state::sequencer::SequencerHistoryOpenOutcome outcome
) {
    return outcome ==
            core::state::sequencer::SequencerHistoryOpenOutcome::ResourceUnavailable
        ? SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE
        : SequencerStepPresetStatus::HISTORY_UNAVAILABLE;
}

FLASHMEM bool applyPreparedDrumPreset(
    core::state::CoreState& state,
    const SequencerStepPresetTarget& target,
    const SequencerStepGraphPreset& preset,
    SequencerStepPresetActionResult& result
) {
    namespace seq = core::state::sequencer;
    if (!target.destinationOwnsPitch ||
        target.trackIndex >= seq::SequencerTrackBankState::TRACK_COUNT ||
        !state.sequencerTracks.isDrumTrack(target.trackIndex)) {
        result.status = SequencerStepPresetStatus::INCOMPATIBLE;
        result.assetStatus = SequencerGraphAssetStatus::INCOMPATIBLE_TARGET;
        return false;
    }

    seq::SequencerHistoryDescriptor descriptor{
        .kind = seq::SequencerHistoryActionKind::DrumAdvancedContent,
        .trackIndex = target.trackIndex,
        .laneIndex = target.drumLaneIndex,
        .stepIndex = target.drumRootStepIndex,
        .property = seq::StepProperty::NOTE,
    };
    const auto opened = state.beginOrContinueSequencerDrumHistory(descriptor, 0U);
    if (!seq::sequencerHistoryOpenAccepted(opened)) {
        result.status = historyStatusFor(opened);
        result.assetStatus = opened == seq::SequencerHistoryOpenOutcome::ResourceUnavailable
            ? SequencerGraphAssetStatus::RESOURCE_EXHAUSTED
            : SequencerGraphAssetStatus::OK;
        return false;
    }

    bool applied = false;
    seq::SequencerGraphAssetReport applyReport{};
    if (target.contentContext == seq::SequencerStepPresetTargetContext::ROOT) {
        auto& drum = state.sequencerTracks.drumTrack(target.trackIndex);
        if (target.drumLaneIndex < drum.kit.laneCount &&
            target.drumRootStepIndex <
                drum.pattern.effectiveLength(target.drumLaneIndex)) {
            auto& pattern = state.sequencer.pattern;
            const bool hasAdvancedPayload = presetHasAdvancedGraphPayload(preset);
            int16_t rootSlot = drum.advancedRootSlot(
                target.drumLaneIndex,
                target.drumRootStepIndex
            );
            if (hasAdvancedPayload && rootSlot < 0) {
                bool mappingChanged = false;
                rootSlot = seq::ensureDrumAdvancedRootSlot(
                    drum,
                    pattern,
                    target.drumLaneIndex,
                    target.drumRootStepIndex,
                    mappingChanged
                );
            }

            bool graphApplied = rootSlot < 0 && !hasAdvancedPayload;
            if (hasAdvancedPayload && rootSlot >= 0) {
                graphApplied = seq::applyStepGraphPresetGraphToNode(
                    pattern,
                    seq::rootStepNodeId(static_cast<uint8_t>(rootSlot)),
                    preset,
                    &applyReport
                );
            } else if (!hasAdvancedPayload && rootSlot >= 0) {
                const auto nodeId = seq::rootStepNodeId(
                    static_cast<uint8_t>(rootSlot)
                );
                const auto* graph = seq::graphView(pattern);
                const auto* node = graph != nullptr ? graph->stepNode(nodeId) : nullptr;
                graphApplied = node == nullptr ||
                    seq::isDefaultSequencerGraphNodePayload(*node) ||
                    seq::resetStepNodePayload(pattern, nodeId);
                if (graphApplied) {
                    graphApplied = seq::compactGraph(pattern).ok &&
                        drum.releaseAdvancedRootSlot(
                            target.drumLaneIndex,
                            target.drumRootStepIndex
                        );
                }
            }

            if (graphApplied && preset.rootValuesValid) {
                const uint8_t lane = target.drumLaneIndex;
                const uint8_t step = target.drumRootStepIndex;
                (void)drum.pattern.setStepEnabled(lane, step, preset.enabled);
                (void)drum.pattern.setStepVelocity(lane, step, preset.velocity);
                (void)drum.pattern.setStepGate(lane, step, preset.gate);
                (void)drum.pattern.setStepNudge(lane, step, preset.nudge);
                (void)drum.pattern.setStepProbability(
                    lane,
                    step,
                    preset.probability
                );
            }
            applied = graphApplied;
        }
    } else {
        applied = seq::applyStepGraphPreset(
            state.sequencer,
            target.stepIndex,
            preset,
            &applyReport
        );
    }

    if (!applied) {
        (void)state.abortSequencerDrumHistory();
        result.assetStatus = applyReport.ok()
            ? SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED
            : applyReport.status;
        result.status = result.assetStatus ==
                SequencerGraphAssetStatus::RESOURCE_EXHAUSTED
            ? SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE
            : statusFromAsset(result.assetStatus);
        return false;
    }

    state.sequencerTracks.publishDrumMutation(target.trackIndex);
    state.sequencer.drumSequencer.bump();
    if (!state.sealSequencerDrumHistory(true, descriptor)) {
        (void)state.abortSequencerDrumHistory();
        result.status = SequencerStepPresetStatus::HISTORY_UNAVAILABLE;
        return false;
    }
    const auto committed = state.commitSequencerDrumHistoryCoalescingOutcome();
    if (committed == seq::SequencerPatternHistoryCommitOutcome::Failed) {
        result.status = SequencerStepPresetStatus::HISTORY_UNAVAILABLE;
        return false;
    }

    state.sequencer.invalidateVariationTelemetry();
    result.status = SequencerStepPresetStatus::OK;
    result.assetStatus = SequencerGraphAssetStatus::OK;
    result.activation = SequencerStepPresetActivation::APPLIED;
    return true;
}

}  // namespace

FLASHMEM SequencerStepPresetDomainServices::SequencerStepPresetDomainServices(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog
) : state_(&state), files_(&files), catalog_(&catalog) {}

FLASHMEM SequencerStepPresetDomainServices SequencerStepPresetDomainServices::fromCoreState(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog
) {
    return SequencerStepPresetDomainServices{state, files, catalog};
}

FLASHMEM SequencerStepPresetListResult SequencerStepPresetDomainServices::listPresetsPage(
    Entry* entries, uint8_t capacity, const char* anchorExclusive,
    core::persistence::StepPresetFilePageDirection direction) const {
    OC_PERF_SCOPE(perfList, "persistence.step-preset.list-page");
    SequencerStepPresetListResult result{};
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_, *catalog_);
    auto listed = store.listPage(entries, capacity, anchorExclusive, direction);
    if (!listed) {
        result.status = statusFromFileError(listed.error().code);
        result.fileError = listed.error().code;
        return result;
    }

    result.count = listed.value().count;
    result.truncated = listed.value().truncated;
    result.hasPrevious = listed.value().hasPrevious;
    result.hasNext = listed.value().hasNext;
    result.totalCount = listed.value().totalCount;
    OC_PERF_UNITS(perfList, result.totalCount, result.count);
    return result;
}

FLASHMEM SequencerStepPresetActionResult
SequencerStepPresetDomainServices::nextPresetId(char* out, size_t outSize) const {
    SequencerStepPresetActionResult result{};
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_, *catalog_);
    auto next = store.nextPresetId(out, outSize);
    if (!next) {
        result.status = statusFromFileError(next.error().code);
        result.fileError = next.error().code;
        return result;
    }
    copyPresetId(result.presetId, sizeof(result.presetId), out);
    return result;
}

FLASHMEM SequencerStepPresetTarget SequencerStepPresetDomainServices::captureTarget() const {
    SequencerStepPresetTarget target{};
    if (state_ == nullptr) return target;

    const auto& sequencer = state_->sequencer;
    target.trackIndex = state_->sequencerTracks.activeTrackIndex();
    target.stepIndex = sequencer.stepEdit.stepIndex.get();
    switch (sequencer.contentView.kind.get()) {
        case core::state::sequencer::SequencerContentViewKind::MICRO_SEQUENCE:
            target.contentContext =
                core::state::sequencer::SequencerStepPresetTargetContext::MICRO_SEQUENCE;
            break;
        case core::state::sequencer::SequencerContentViewKind::CYCLE_STATES:
            target.contentContext =
                core::state::sequencer::SequencerStepPresetTargetContext::CYCLE_STATES;
            break;
        case core::state::sequencer::SequencerContentViewKind::ROOT:
        default:
            target.contentContext = core::state::sequencer::SequencerStepPresetTargetContext::ROOT;
            break;
    }
    target.ownerNodeId = sequencer.contentView.ownerNodeId.get();
    target.sequenceId = sequencer.contentView.sequenceId.get();
    target.cycleSetId = sequencer.contentView.cycleSetId.get();
    const bool drumContext = sequencer.stepEdit.drumContext &&
        state_->sequencerTracks.isDrumTrack(target.trackIndex);
    if (drumContext) {
        target.destinationOwnsPitch = true;
        if (core::state::sequencer::isDrumContentView(sequencer)) {
            target.drumLaneIndex = sequencer.contentView.drumOwnerLane;
            target.drumRootStepIndex = sequencer.contentView.drumOwnerStep;
            target.drumRootSlot = sequencer.contentView.drumOwnerRootSlot;
        } else {
            target.drumLaneIndex = sequencer.stepEdit.drumLane;
            target.drumRootStepIndex = sequencer.stepEdit.drumStep;
            target.drumRootSlot = sequencer.stepEdit.drumRootSlot;
        }
        const auto& drum = state_->sequencerTracks.drumTrack(target.trackIndex);
        if (target.drumLaneIndex < drum.kit.laneCount) {
            target.destinationNote =
                drum.kit.lanes[target.drumLaneIndex].midiNote;
        }
    }
    if (target.destinationOwnsPitch &&
        target.contentContext ==
            core::state::sequencer::SequencerStepPresetTargetContext::ROOT) {
        target.targetNodeId = target.drumRootSlot == 0xFFU
            ? oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID
            : core::state::sequencer::rootStepNodeId(target.drumRootSlot);
    } else {
        target.targetNodeId =
            core::state::sequencer::activeContentStepNodeId(sequencer, target.stepIndex);
    }
    target.projectRevision = state_->project.metadata.modifiedCounter;
    if (target.destinationOwnsPitch &&
        target.contentContext ==
            core::state::sequencer::SequencerStepPresetTargetContext::ROOT) {
        const auto& drum = state_->sequencerTracks.drumTrack(target.trackIndex);
        target.valid = target.drumLaneIndex < drum.kit.laneCount &&
            target.drumRootStepIndex <
                drum.pattern.effectiveLength(target.drumLaneIndex);
    } else {
        target.valid =
            target.stepIndex < core::state::sequencer::activeContentLength(sequencer) &&
            target.targetNodeId !=
                oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    }

    const char* context = "Root";
    if (target.contentContext ==
        core::state::sequencer::SequencerStepPresetTargetContext::MICRO_SEQUENCE) {
        context = "Micro";
    } else if (target.contentContext ==
               core::state::sequencer::SequencerStepPresetTargetContext::CYCLE_STATES) {
        context = "Cycle";
    }
    if (target.destinationOwnsPitch) {
        std::snprintf(
            target.contextLabel,
            sizeof(target.contextLabel),
            "T%u Drum L%u S%02u",
            static_cast<unsigned>(target.trackIndex + 1U),
            static_cast<unsigned>(target.drumLaneIndex + 1U),
            static_cast<unsigned>(target.drumRootStepIndex + 1U)
        );
    } else {
        std::snprintf(target.contextLabel, sizeof(target.contextLabel), "T%u %s S%02u",
                      static_cast<unsigned>(target.trackIndex + 1U), context,
                      static_cast<unsigned>(target.stepIndex + 1U));
    }
    return target;
}

FLASHMEM bool SequencerStepPresetDomainServices::targetMatches(
    const SequencerStepPresetTarget& target) const {
    if (state_ == nullptr || !target.valid ||
        state_->sequencerTracks.activeTrackIndex() != target.trackIndex) {
        return false;
    }
    return sameFrozenTarget(captureTarget(), target);
}

FLASHMEM uint32_t SequencerStepPresetDomainServices::projectRevision() const {
    return state_ ? state_->project.metadata.modifiedCounter : 0;
}

FLASHMEM SequencerStepPresetInspectResult SequencerStepPresetDomainServices::inspectPreset(
    const char* presetId, const SequencerStepPresetTarget& target, uint8_t previewStateIndex,
    uint32_t generation) const {
    auto buffer = makeStepPresetBuffer();
    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    return inspectPresetPrepared(presetId, target, previewStateIndex, generation,
                                 buffer ? buffer->bytes : nullptr, preset.get());
}

FLASHMEM SequencerStepPresetInspectResult SequencerStepPresetDomainServices::inspectPresetPrepared(
    const char* presetId, const SequencerStepPresetTarget& target, uint8_t previewStateIndex,
    uint32_t generation, uint8_t* encodedWorkspace,
    SequencerStepGraphPreset* preparedPreset) const {
    OC_PERF_SCOPE(perfInspect, "persistence.step-preset.inspect");
    OC_PERF_UNITS(perfInspect, 0U, previewStateIndex);
    SequencerStepPresetInspectResult result{};
    auto& descriptor = result.descriptor;
    descriptor.valid = true;
    descriptor.previewStateIndex = previewStateIndex;
    descriptor.generation = generation;
    copyPresetId(descriptor.technicalId, sizeof(descriptor.technicalId), presetId);
    core::state::sequencer::sequencerStepPresetSemanticName(presetId, descriptor.semanticName,
                                                            sizeof(descriptor.semanticName));
    descriptor.previewKey = {
        .assetHash = core::state::sequencer::sequencerStepPresetIdHash(presetId),
        .targetHash = core::state::sequencer::sequencerStepPresetTargetHash(target),
        .projectRevision = target.projectRevision,
        .stateIndex = previewStateIndex,
    };

    if (state_ == nullptr || files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        descriptor.compatibility = SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE;
        setCompatibilityReason(descriptor);
        return result;
    }

    if (encodedWorkspace == nullptr || preparedPreset == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        descriptor.compatibility = SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE;
        setCompatibilityReason(descriptor);
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_, *catalog_);
    uint16_t payloadSize = 0;
    auto loaded = store.load(presetId, encodedWorkspace, STEP_PRESET_MAX_ENCODED_SIZE, payloadSize);
    if (!loaded) {
        result.status = statusFromFileError(loaded.error().code);
        result.fileError = loaded.error().code;
        descriptor.compatibility = result.status == SequencerStepPresetStatus::STORAGE_UNAVAILABLE
                                       ? SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE
                                       : SequencerStepPresetCompatibility::CORRUPT;
        setCompatibilityReason(descriptor);
        return result;
    }
    descriptor.previewKey.assetHash = hashPresetPayload(presetId, encodedWorkspace, payloadSize);
    result.bytes = payloadSize;
    OC_PERF_UNITS(perfInspect, payloadSize, previewStateIndex);

    core::state::sequencer::SequencerGraphAssetReport report{};
    if (!asset_codec::decode(encodedWorkspace, payloadSize, *preparedPreset, &report)) {
        result.assetStatus = report.status;
        result.status = statusFromAsset(report.status);
        descriptor.compatibility = compatibilityForAssetStatus(report.status);
        setCompatibilityReason(descriptor);
        return result;
    }

    if (!core::state::sequencer::validStepGraphPresetTechnicalId(preparedPreset->technicalId) ||
        std::strcmp(preparedPreset->technicalId, presetId) != 0 ||
        !core::state::sequencer::validStepGraphPresetSemanticName(preparedPreset->semanticName)) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_FORMAT;
        descriptor.compatibility = SequencerStepPresetCompatibility::CORRUPT;
        setCompatibilityReason(descriptor);
        return result;
    }
    copyText(descriptor.semanticName, sizeof(descriptor.semanticName),
             preparedPreset->semanticName);

    bool destinationPitchAdapted = false;
    if (target.destinationOwnsPitch &&
        !core::state::sequencer::projectStepGraphPresetToDestinationPitch(
            *preparedPreset,
            target.destinationNote,
            &destinationPitchAdapted
        )) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_FORMAT;
        descriptor.compatibility = SequencerStepPresetCompatibility::CORRUPT;
        setCompatibilityReason(descriptor);
        return result;
    }

    descriptor.stepNodeCount = preparedPreset->graph.stepNodeCount;
    descriptor.sequenceCount = preparedPreset->graph.sequenceCount;
    descriptor.cycleSetCount = preparedPreset->graph.cycleSetCount;
    fillContentFacts(*preparedPreset, descriptor);
    descriptor.footprint = targetPopulated(*state_, target)
                               ? SequencerStepPresetFootprint::REPLACE
                               : SequencerStepPresetFootprint::FREE;

    const auto destinationScale = effectiveScale(*state_);
    const auto sourceScale = preparedPreset->sourceScale;
    const bool scaleRelative =
        preparedPreset->scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE;
    const bool pitchContextMatches = target.destinationOwnsPitch ||
        presetPitchContextMatchesDestination(*preparedPreset, *state_, destinationScale);
    descriptor.scalePolicy =
        scaleRelative ? core::state::sequencer::SequencerStepPresetScalePolicy::SCALE_RELATIVE
                      : core::state::sequencer::SequencerStepPresetScalePolicy::CHROMATIC;
    bool pitchAdapted = false;
    if (!target.destinationOwnsPitch && pitchContextMatches &&
        !core::state::sequencer::adaptStepGraphPresetPitchToDestination(
            *preparedPreset,
            destinationScale,
            &pitchAdapted
        )) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_FORMAT;
        descriptor.compatibility = SequencerStepPresetCompatibility::CORRUPT;
        setCompatibilityReason(descriptor);
        return result;
    }
    if (target.destinationOwnsPitch) {
        descriptor.adaptation =
            core::state::sequencer::SequencerStepPresetAdaptation::DESTINATION_PITCH;
        char noteName[8]{};
        formatNoteName(target.destinationNote, noteName, sizeof(noteName));
        std::snprintf(
            descriptor.adaptationSummary,
            sizeof(descriptor.adaptationSummary),
            "Pitch from lane · %s",
            noteName
        );
    } else if (!pitchContextMatches) {
        copyText(descriptor.adaptationSummary, sizeof(descriptor.adaptationSummary),
                 scaleRelative ? "Requires Follow Scale" : "Requires Chromatic");
    } else if (scaleRelative) {
        char sourceLabel[20]{};
        char destinationLabel[20]{};
        formatScale(sourceScale, sourceLabel, sizeof(sourceLabel));
        formatScale(destinationScale, destinationLabel, sizeof(destinationLabel));
        descriptor.adaptation =
            pitchAdapted ? core::state::sequencer::SequencerStepPresetAdaptation::DESTINATION_SCALE
                         : core::state::sequencer::SequencerStepPresetAdaptation::PRESERVED;
        std::snprintf(descriptor.adaptationSummary, sizeof(descriptor.adaptationSummary),
                      "%.17s -> %.17s", sourceLabel, destinationLabel);
    } else {
        descriptor.adaptation = core::state::sequencer::SequencerStepPresetAdaptation::PRESERVED;
        copyText(descriptor.adaptationSummary, sizeof(descriptor.adaptationSummary),
                 "Chromatic: absolute pitch");
    }
    copyText(
        descriptor.replaceFacts,
        sizeof(descriptor.replaceFacts),
        preparedPreset->rootContext ? "Step values + child graph" : "Child values + graph"
    );
    copyText(
        descriptor.preserveFacts,
        sizeof(descriptor.preserveFacts),
        target.destinationOwnsPitch
            ? "Lane note, name, icon, color, route"
            : (preparedPreset->rootContext
                ? "Track route, scale, other steps"
                : "Root step and track route")
    );

    if (!targetMatches(target) || projectRevision() != target.projectRevision) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        descriptor.compatibility = SequencerStepPresetCompatibility::STALE_TARGET;
    } else if (preparedPreset->rootContext !=
               (target.contentContext ==
                core::state::sequencer::SequencerStepPresetTargetContext::ROOT)) {
        result.status = SequencerStepPresetStatus::INCOMPATIBLE;
        descriptor.compatibility = SequencerStepPresetCompatibility::BLOCKED_CONTEXT;
    } else if (!pitchContextMatches) {
        result.status = SequencerStepPresetStatus::INCOMPATIBLE;
        descriptor.compatibility = SequencerStepPresetCompatibility::BLOCKED_PITCH_CONTEXT;
    } else if (!graphCapacityAvailable(state_->sequencer, *preparedPreset)) {
        result.status = SequencerStepPresetStatus::CAPACITY;
        descriptor.compatibility = SequencerStepPresetCompatibility::BLOCKED_CAPACITY;
    } else {
        result.status = SequencerStepPresetStatus::OK;
        if (destinationPitchAdapted ||
            (scaleRelative && !sameScale(sourceScale, destinationScale))) {
            descriptor.compatibility = SequencerStepPresetCompatibility::WARNING_ADAPTED;
        } else {
            descriptor.compatibility = SequencerStepPresetCompatibility::READY;
        }
    }
    setCompatibilityReason(descriptor);
    fillPreviewExample(*preparedPreset, descriptor);
    return result;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::savePreset(
    const char* presetId, const SequencerStepPresetTarget& target, bool allowOverwrite) const {
    SequencerStepPresetActionResult result{};
    copyPresetId(result.presetId, sizeof(result.presetId), presetId);
    if (state_ == nullptr || files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (!targetMatches(target) || projectRevision() != target.projectRevision) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_, *catalog_);
    const auto existing = store.exists(presetId);
    if (!existing) {
        result.status = statusFromFileError(existing.error().code);
        result.fileError = existing.error().code;
        return result;
    }
    if (existing.value() && !allowOverwrite) {
        result.status = SequencerStepPresetStatus::COLLISION;
        return result;
    }

    auto buffer = makeStepPresetBuffer();
    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    if (!buffer || !preset) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        return result;
    }

    char semanticName[SequencerStepGraphPreset::SEMANTIC_NAME_SIZE]{};
    core::state::sequencer::sequencerStepPresetSemanticName(presetId, semanticName,
                                                            sizeof(semanticName));
    if (existing.value()) {
        uint16_t existingSize = 0;
        const auto loaded =
            store.load(presetId, buffer->bytes, STEP_PRESET_MAX_ENCODED_SIZE, existingSize);
        core::state::sequencer::SequencerGraphAssetReport existingReport{};
        if (loaded && asset_codec::decode(buffer->bytes, existingSize, *preset, &existingReport) &&
            std::strcmp(preset->technicalId, presetId) == 0 &&
            core::state::sequencer::validStepGraphPresetSemanticName(preset->semanticName)) {
            copyText(semanticName, sizeof(semanticName), preset->semanticName);
        }
    }

    core::state::sequencer::SequencerGraphAssetReport captureReport{};
    const auto sourceScale = effectiveScale(*state_);
    if (!capturePresetForTarget(
            *state_,
            target,
            sourceScale,
            *preset,
            captureReport
        )) {
        result.assetStatus = captureReport.status;
        result.status = statusFromAsset(captureReport.status);
        return result;
    }
    if (!core::state::sequencer::setStepGraphPresetMetadata(
            *preset, presetId, semanticName, preset->scalePolicy, preset->sourceScale)) {
        result.status = SequencerStepPresetStatus::FAILED;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_ARGUMENT;
        return result;
    }

    const auto encoded = asset_codec::encode(*preset, buffer->bytes, STEP_PRESET_MAX_ENCODED_SIZE);
    result.assetStatus = encoded.status;
    result.bytes = encoded.bytesWritten;
    if (!encoded.ok()) {
        result.status = statusFromAsset(encoded.status);
        return result;
    }

    if (!targetMatches(target) || projectRevision() != target.projectRevision) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }

    auto saved = store.save(presetId, buffer->bytes, encoded.bytesWritten);
    if (!saved) {
        result.status = statusFromFileError(saved.error().code);
        result.fileError = saved.error().code;
        return result;
    }

    result.bytes = static_cast<uint16_t>(saved.value().bytesWritten);
    result.activation = SequencerStepPresetActivation::APPLIED;
    copyPresetId(result.presetId, sizeof(result.presetId), saved.value().presetId);
    return result;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::applyPreset(
    const char* presetId, const SequencerStepPresetTarget& target,
    const core::state::sequencer::SequencerStepPresetPreviewKey& expectedPreview) const {
    SequencerStepPresetActionResult result{};
    copyPresetId(result.presetId, sizeof(result.presetId), presetId);
    if (state_ == nullptr || files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    // Apply owns one independent undo unit. Its caller must close any earlier
    // continuous edit first; doing that here would mutate History even when a
    // later preview, storage, capacity, or activation preflight failed.
    if (state_->hasPendingSequencerPatternHistoryCoalescing()) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }

    auto buffer = makeStepPresetBuffer();
    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    if (!buffer || !preset) {
        result.status = SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        return result;
    }
    const auto preflight = inspectPresetPrepared(presetId, target, expectedPreview.stateIndex, 0,
                                                 buffer->bytes, preset.get());
    result.bytes = preflight.bytes;
    if (preflight.descriptor.previewKey != expectedPreview ||
        !core::state::sequencer::sequencerStepPresetCanApply(preflight.descriptor.compatibility)) {
        result.status = preflight.status == SequencerStepPresetStatus::OK
                            ? SequencerStepPresetStatus::STALE_TARGET
                            : preflight.status;
        result.assetStatus = preflight.assetStatus;
        result.fileError = preflight.fileError;
        return result;
    }

    // Re-read only the encoded bytes before publication. This closes the
    // storage TOCTOU window without paying for a second decode/adaptation:
    // the prepared graph remains usable only while the payload fingerprint
    // still matches the admitted preview.
    {
        OC_PERF_SCOPE(perfVerify, "persistence.step-preset.verify-payload");
        core::persistence::StepPresetFileStore store(*files_, *catalog_);
        uint16_t verifiedSize = 0;
        const auto verified =
            store.load(presetId, buffer->bytes, STEP_PRESET_MAX_ENCODED_SIZE, verifiedSize);
        if (!verified) {
            result.status = statusFromFileError(verified.error().code);
            result.fileError = verified.error().code;
            return result;
        }
        result.bytes = verifiedSize;
        OC_PERF_UNITS(perfVerify, verifiedSize, 0U);
        if (hashPresetPayload(presetId, buffer->bytes, verifiedSize) != expectedPreview.assetHash) {
            result.status = SequencerStepPresetStatus::STALE_TARGET;
            return result;
        }
    }

    // Release the encoded workspace before allocating staging/history graphs,
    // which lowers peak PSRAM while retaining the prepared decoded graph.
    buffer.reset();

    if (target.destinationOwnsPitch) {
        if (!targetMatches(target) ||
            projectRevision() != target.projectRevision) {
            result.status = SequencerStepPresetStatus::STALE_TARGET;
            return result;
        }
        (void)applyPreparedDrumPreset(*state_, target, *preset, result);
        return result;
    }

    auto change =
        core::app::makeExtmemUnique<core::state::sequencer::SequencerHistoryPatternChange>();
    auto staged = core::app::makeExtmemUnique<core::state::sequencer::SequencerState>();
    if (!change || !staged ||
        !core::state::sequencer::captureHistorySnapshot(state_->sequencer, change->before) ||
        !core::state::sequencer::reserveHistorySnapshotGraphStorage(change->after) ||
        !core::state::sequencer::copyPatternState(staged->pattern, state_->sequencer.pattern)) {
        result.status = SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE;
        result.assetStatus = SequencerGraphAssetStatus::RESOURCE_EXHAUSTED;
        return result;
    }
    copyEditorContextForStaging(*staged, state_->sequencer);

    core::state::sequencer::SequencerGraphAssetReport applyReport{};
    // The generic graph-copy primitive reports a false return for both graph
    // capacity and first-owner allocation failures. Reserve that owner here so
    // this publication boundary can preserve the actionable D-OOM reason.
    if (core::state::sequencer::graphView(staged->pattern) == nullptr &&
        !core::state::sequencer::ensureGraphRoot(staged->pattern)) {
        result.status = SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE;
        result.assetStatus = SequencerGraphAssetStatus::RESOURCE_EXHAUSTED;
        return result;
    }
    const bool applied = core::state::sequencer::applyStepGraphPreset(*staged, target.stepIndex,
                                                                      *preset, &applyReport);
    result.assetStatus = applyReport.status;
    if (!applied) {
        result.status = applyReport.status == SequencerGraphAssetStatus::RESOURCE_EXHAUSTED
                            ? SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE
                            : statusFromAsset(applyReport.status);
        return result;
    }

    if (!core::state::sequencer::captureHistorySnapshotUsingReservedGraph(*staged, change->after)) {
        result.status = SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE;
        result.assetStatus = SequencerGraphAssetStatus::RESOURCE_EXHAUSTED;
        return result;
    }

    if (core::state::sequencer::sameMusicalHistorySnapshot(change->before, change->after)) {
        result.activation = SequencerStepPresetActivation::APPLIED;
        return result;
    }

    change->trackIndex = target.trackIndex;
    change->descriptor = {
        .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
        .trackIndex = target.trackIndex,
        .stepIndex = target.stepIndex,
        .property = core::state::sequencer::StepProperty::NOTE,
        .hasValue = false,
    };
    core::state::sequencer::SequencerPatternSnapshot stagedFlat{};
    core::state::sequencer::captureSnapshot(staged->pattern, stagedFlat);
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> bankGraph;
    if (!core::state::cloneSequencerGraph(bankGraph,
                                          core::state::sequencer::graphView(staged->pattern))) {
        result.status = SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE;
        result.assetStatus = SequencerGraphAssetStatus::RESOURCE_EXHAUSTED;
        return result;
    }

    const uint16_t targetTrackBit = static_cast<uint16_t>(1U << target.trackIndex);
    const uint16_t enabledMask = state_->sequencerTracks.currentEnabledMask();
    const uint16_t targetAudibleMask =
        core::state::project::audibleMask(state_->projectTracks, enabledMask);
    core::state::sequencer::SequencerTrackActivationBatch activationBatch{};
    if (!state_->sequencerTrackActivations.prepare(
            targetTrackBit, targetAudibleMask, state_->statusBar.playing.get(), activationBatch,
            core::state::sequencer::SequencerTrackActivationOrigin::PRESET)) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }
    change->auxiliary.activation.reference =
        core::state::sequencer::activationHistoryRef(activationBatch);
    change->auxiliary.activation.targetAudibleMask = targetAudibleMask;
    if (!state_->sequencerHistory.canRecordPattern(*change)) {
        result.status = SequencerStepPresetStatus::HISTORY_UNAVAILABLE;
        return result;
    }

    // Last fallible gate: the project/selection identity is checked directly
    // before arming runtime and before the first live editor/bank write.
    if (!targetMatches(target) || projectRevision() != target.projectRevision ||
        state_->sequencerTracks.currentEnabledMask() != enabledMask ||
        core::state::project::audibleMask(state_->projectTracks, enabledMask) !=
            targetAudibleMask) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }
    if (!state_->sequencerTrackActivations.armPrepared(activationBatch)) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }

    // From here to publication every operation is an ownership transfer,
    // fixed-capacity history commit, or Signal write and therefore cannot
    // fail. Runtime sees the target Track frozen while both editor and bank
    // receive the same prepared generation.
    auto editorGraph = std::move(staged->pattern.graph);
    core::state::sequencer::installTrackContentSnapshotToEditorWithOwnedGraph(
        state_->sequencer, stagedFlat, std::move(editorGraph));
    state_->sequencer.page.set(staged->page.get());
    state_->sequencer.focusedStep.set(staged->focusedStep.get());
    copyContentViewState(state_->sequencer.contentView, staged->contentView);
    state_->sequencer.invalidateVariationTelemetry();

    core::state::sequencer::installTrackContentSnapshotWithOwnedGraph(
        state_->sequencerTracks.track(target.trackIndex), stagedFlat, std::move(bankGraph));
    state_->sequencerHistory.recordPreparedPattern(std::move(change));
    state_->publishPreparedSequencerMutation();
    state_->sequencerTrackActivations.publishPrepared(activationBatch);

    result.activationGeneration = activationBatch.generation;
    result.activation = (activationBatch.localLoopBoundaryMask & targetTrackBit) != 0
                            ? SequencerStepPresetActivation::QUEUED
                            : SequencerStepPresetActivation::APPLIED;
    result.status = result.activation == SequencerStepPresetActivation::QUEUED
                        ? SequencerStepPresetStatus::QUEUED
                        : SequencerStepPresetStatus::OK;
    return result;
}

FLASHMEM core::state::sequencer::SequencerTrackActivationStatus
SequencerStepPresetDomainServices::activationStatus(uint8_t trackIndex, uint32_t generation) const {
    using Status = core::state::sequencer::SequencerTrackActivationStatus;
    if (state_ == nullptr || generation == 0 ||
        trackIndex >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        return Status::IDLE;
    }

    const auto telemetry = state_->sequencerTrackActivations.telemetry(trackIndex);
    return telemetry.generation == generation ? telemetry.status : Status::IDLE;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::renamePreset(
    const char* presetId, const char* expectedSemanticName, const char* newSemanticName) const {
    SequencerStepPresetActionResult result{};
    copyPresetId(result.presetId, sizeof(result.presetId), presetId);
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (!core::state::sequencer::validStepGraphPresetTechnicalId(presetId) ||
        !core::state::sequencer::validStepGraphPresetSemanticName(expectedSemanticName) ||
        !core::state::sequencer::validStepGraphPresetSemanticName(newSemanticName)) {
        result.status = SequencerStepPresetStatus::FAILED;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_ARGUMENT;
        return result;
    }

    auto before = makeStepPresetBuffer();
    auto after = makeStepPresetBuffer();
    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    if (!before || !after || !preset) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_, *catalog_);
    uint16_t beforeSize = 0;
    const auto loaded =
        store.load(presetId, before->bytes, STEP_PRESET_MAX_ENCODED_SIZE, beforeSize);
    if (!loaded) {
        result.status = statusFromFileError(loaded.error().code);
        result.fileError = loaded.error().code;
        return result;
    }
    result.bytes = beforeSize;

    core::state::sequencer::SequencerGraphAssetReport report{};
    if (!asset_codec::decode(before->bytes, beforeSize, *preset, &report)) {
        result.status = statusFromAsset(report.status);
        result.assetStatus = report.status;
        return result;
    }
    if (std::strcmp(preset->technicalId, presetId) != 0) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_FORMAT;
        return result;
    }
    if (std::strcmp(preset->semanticName, expectedSemanticName) != 0) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }

    if (!core::state::sequencer::setStepGraphPresetMetadata(
            *preset, presetId, newSemanticName, preset->scalePolicy, preset->sourceScale)) {
        result.status = SequencerStepPresetStatus::FAILED;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_ARGUMENT;
        return result;
    }
    const auto encoded = asset_codec::encode(*preset, after->bytes, STEP_PRESET_MAX_ENCODED_SIZE);
    result.assetStatus = encoded.status;
    if (!encoded.ok()) {
        result.status = statusFromAsset(encoded.status);
        return result;
    }
    if (!samePresetBytesOutsideSemanticName(before->bytes, beforeSize, after->bytes,
                                            encoded.bytesWritten)) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_FORMAT;
        return result;
    }

    preset->reset();
    core::state::sequencer::SequencerGraphAssetReport verification{};
    if (!asset_codec::decode(after->bytes, encoded.bytesWritten, *preset, &verification) ||
        std::strcmp(preset->technicalId, presetId) != 0 ||
        std::strcmp(preset->semanticName, newSemanticName) != 0) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus =
            verification.ok() ? SequencerGraphAssetStatus::INVALID_FORMAT : verification.status;
        return result;
    }

    const auto saved = store.save(presetId, after->bytes, encoded.bytesWritten);
    if (!saved) {
        result.status = statusFromFileError(saved.error().code);
        result.fileError = saved.error().code;
        return result;
    }
    result.bytes = static_cast<uint16_t>(saved.value().bytesWritten);
    result.activation = SequencerStepPresetActivation::APPLIED;
    return result;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::deletePreset(
    const char* presetId, const char* expectedSemanticName) const {
    SequencerStepPresetActionResult result{};
    copyPresetId(result.presetId, sizeof(result.presetId), presetId);
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (!core::state::sequencer::validStepGraphPresetTechnicalId(presetId) ||
        !core::state::sequencer::validStepGraphPresetSemanticName(expectedSemanticName)) {
        result.status = SequencerStepPresetStatus::FAILED;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_ARGUMENT;
        return result;
    }

    auto buffer = makeStepPresetBuffer();
    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    if (!buffer || !preset) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_, *catalog_);
    uint16_t payloadSize = 0;
    const auto loaded =
        store.load(presetId, buffer->bytes, STEP_PRESET_MAX_ENCODED_SIZE, payloadSize);
    if (!loaded) {
        result.status = statusFromFileError(loaded.error().code);
        result.fileError = loaded.error().code;
        return result;
    }
    result.bytes = payloadSize;

    core::state::sequencer::SequencerGraphAssetReport report{};
    if (!asset_codec::decode(buffer->bytes, payloadSize, *preset, &report)) {
        result.status = statusFromAsset(report.status);
        result.assetStatus = report.status;
        return result;
    }
    if (std::strcmp(preset->technicalId, presetId) != 0) {
        result.status = SequencerStepPresetStatus::CORRUPT;
        result.assetStatus = SequencerGraphAssetStatus::INVALID_FORMAT;
        return result;
    }
    if (std::strcmp(preset->semanticName, expectedSemanticName) != 0) {
        result.status = SequencerStepPresetStatus::STALE_TARGET;
        return result;
    }

    const auto removed = store.remove(presetId);
    if (!removed) {
        result.status = statusFromFileError(removed.error().code);
        result.fileError = removed.error().code;
        return result;
    }
    result.activation = SequencerStepPresetActivation::APPLIED;
    return result;
}

}  // namespace core::handler

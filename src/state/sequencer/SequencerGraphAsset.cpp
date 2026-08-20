#include "state/sequencer/SequencerGraphAsset.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphCanonicalPolicy.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOpsInternal.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::state::sequencer {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerStepNode;
using namespace graph_ops_internal;

namespace graph_policy = graph_canonical_policy;

constexpr uint16_t kAssetRootNodeId =
    SequencerStepGraphPreset::ASSET_ROOT_NODE_ID;

FLASHMEM void setReportStatus(
    SequencerGraphAssetReport* report,
    SequencerGraphAssetStatus status
) {
    if (report != nullptr) report->status = status;
}

FLASHMEM void fillReportCounts(
    SequencerGraphAssetReport* report,
    const StepSequencerGraph& graph
) {
    if (report == nullptr) return;
    report->stepNodeCount = graph.stepNodeCount;
    report->sequenceCount = graph.sequenceCount;
    report->cycleSetCount = graph.cycleSetCount;
}

FLASHMEM bool scaleSettingsCanonical(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    return settings.root < 12U &&
           static_cast<uint8_t>(settings.type) <=
               static_cast<uint8_t>(
                   oc::note::sequencer::StepSequencerScaleType::WholeTone
               ) &&
           static_cast<uint8_t>(settings.mode) <=
               static_cast<uint8_t>(
                   oc::note::sequencer::
                       StepSequencerScaleConstraintMode::ConstrainDown
               );
}

FLASHMEM bool sourceScaleCanonical(
    SequencerStepGraphPreset::ScalePolicy scalePolicy,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale
) {
    if (scalePolicy > SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE ||
        !scaleSettingsCanonical(sourceScale)) {
        return false;
    }
    if (scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE) {
        return sourceScale.isConstrained();
    }
    return sourceScale.root == 0 &&
           sourceScale.type ==
               oc::note::sequencer::StepSequencerScaleType::Chromatic &&
           sourceScale.mode ==
               oc::note::sequencer::StepSequencerScaleConstraintMode::Free;
}

FLASHMEM bool rootValuesCanonical(
    bool rootContext,
    bool rootValuesValid,
    bool enabled,
    uint8_t note,
    uint8_t velocity,
    uint16_t gate,
    int8_t nudge,
    uint8_t probability
) {
    if (rootValuesValid) {
        return rootContext &&
               note <= 127U &&
               velocity <= 127U &&
               gate <= SequencerState::MAX_GATE_PERCENT &&
               nudge >= -50 &&
               nudge <= 50 &&
               probability <= 100U;
    }
    return !enabled &&
           note == SequencerState::DEFAULT_NOTE &&
           velocity == SequencerState::DEFAULT_VELOCITY &&
           gate == SequencerState::DEFAULT_GATE_PERCENT &&
           nudge == 0 &&
           probability == SequencerState::DEFAULT_PROBABILITY;
}

FLASHMEM bool sameScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings lhs,
    oc::note::sequencer::StepSequencerScaleSettings rhs
) {
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

FLASHMEM int floorDiv12(int value) {
    if (value >= 0) return value / 12;
    return -static_cast<int>((-value + 11) / 12);
}

FLASHMEM uint8_t scaleDegreeForPitchClass(
    oc::note::sequencer::StepSequencerScaleSettings scale,
    uint8_t notePitchClass
) {
    const uint8_t relative = static_cast<uint8_t>(
        (notePitchClass + 12U - scale.root) % 12U
    );
    const uint16_t mask = oc::note::sequencer::scaleMask(scale.type);
    uint8_t degree = 0;
    for (uint8_t interval = 0; interval < relative; ++interval) {
        if ((mask & static_cast<uint16_t>(1U << interval)) != 0) ++degree;
    }
    return degree;
}

FLASHMEM uint8_t scaleIntervalForDegree(
    oc::note::sequencer::StepSequencerScaleSettings scale,
    uint8_t degree
) {
    const uint16_t mask = oc::note::sequencer::scaleMask(scale.type);
    uint8_t seen = 0;
    for (uint8_t interval = 0; interval < 12; ++interval) {
        if ((mask & static_cast<uint16_t>(1U << interval)) == 0) continue;
        if (seen == degree) return interval;
        ++seen;
    }
    return 0;
}

FLASHMEM uint8_t scaleDegreeCount(
    oc::note::sequencer::StepSequencerScaleSettings scale
) {
    const uint16_t mask = oc::note::sequencer::scaleMask(scale.type);
    uint8_t count = 0;
    for (uint8_t interval = 0; interval < 12; ++interval) {
        if ((mask & static_cast<uint16_t>(1U << interval)) != 0) ++count;
    }
    return count == 0 ? 1 : count;
}

FLASHMEM bool buildAssetGraphFromSourceNode(
    const StepSequencerGraph* sourceGraph,
    SequencerGraphNodeId sourceNodeId,
    StepSequencerGraph& out
) {
    StepSequencerStepNode sourceNode{};
    if (sourceGraph != nullptr) {
        const auto* node = sourceGraph->stepNode(sourceNodeId);
        if (node == nullptr) return false;
        sourceNode = *node;
    }

    out.reset();
    out.enabled = true;
    out.rootSequenceId = StepSequencerGraphLimits::INVALID_ID;
    out.stepNodeCount = 1;
    out.sequenceCount = 0;
    out.cycleSetCount = 0;
    copyStepNodeValuesWithoutChildren(
        out.stepNodes[kAssetRootNodeId],
        sourceNode
    );

    if (sourceGraph == nullptr) return true;
    return copyChildrenIntoNode(
        out,
        out.stepNodes[kAssetRootNodeId],
        *sourceGraph,
        sourceNode
    );
}

FLASHMEM bool captureRootPresetFromSource(
    const SequencerPatternState& pattern,
    const StepSequencerGraph* sourceGraph,
    SequencerGraphNodeId sourceNodeId,
    const SequencerStepGraphRootValues& rootValues,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report
) {
    if (!scaleSettingsCanonical(sourceScale) ||
        !rootValuesCanonical(
            true,
            true,
            rootValues.enabled,
            rootValues.note,
            rootValues.velocity,
            rootValues.gate,
            rootValues.nudge,
            rootValues.probability
        )) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (!buildAssetGraphFromSourceNode(sourceGraph, sourceNodeId, out.graph)) {
        setReportStatus(report, SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED);
        return false;
    }

    out.valid = true;
    out.rootContext = true;
    out.rootValuesValid = true;
    const auto scalePolicy = pitchContextUsesScaleDegrees(
        pattern.pitchEditMode,
        sourceScale
    )
        ? SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
        : SequencerStepGraphPreset::ScalePolicy::CHROMATIC;
    const auto persistedSourceScale =
        scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
            ? sourceScale
            : oc::note::sequencer::StepSequencerScaleSettings{};
    if (!setStepGraphPresetMetadata(
            out,
            "unsaved",
            "Untitled",
            scalePolicy,
            persistedSourceScale
        )) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }

    out.enabled = rootValues.enabled;
    out.note = rootValues.note;
    out.velocity = rootValues.velocity;
    out.gate = rootValues.gate;
    out.nudge = rootValues.nudge;
    out.probability = rootValues.probability;
    if (report != nullptr) {
        report->flags = static_cast<uint16_t>(
            report->flags |
            SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES |
            SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD
        );
        fillReportCounts(report, out.graph);
    }
    return true;
}

}  // namespace

FLASHMEM void SequencerGraphAssetReport::reset() {
    status = SequencerGraphAssetStatus::OK;
    flags = SEQUENCER_GRAPH_ASSET_REPORT_NONE;
    stepNodeCount = 0;
    sequenceCount = 0;
    cycleSetCount = 0;
}

FLASHMEM void SequencerStepGraphPreset::reset() {
    valid = false;
    formatVersion = CURRENT_FORMAT_VERSION;
    technicalId[0] = '\0';
    semanticName[0] = '\0';
    scalePolicy = ScalePolicy::CHROMATIC;
    sourceScale = {};
    rootContext = true;
    rootValuesValid = false;
    enabled = false;
    note = SequencerState::DEFAULT_NOTE;
    velocity = SequencerState::DEFAULT_VELOCITY;
    gate = SequencerState::DEFAULT_GATE_PERCENT;
    nudge = 0;
    probability = SequencerState::DEFAULT_PROBABILITY;
    graph.reset();
}

FLASHMEM bool stepGraphPresetMetadataIsCanonical(
    const SequencerStepGraphPreset& preset
) {
    return preset.formatVersion ==
               SequencerStepGraphPreset::CURRENT_FORMAT_VERSION &&
           sourceScaleCanonical(preset.scalePolicy, preset.sourceScale) &&
           rootValuesCanonical(
               preset.rootContext,
               preset.rootValuesValid,
               preset.enabled,
               preset.note,
               preset.velocity,
               preset.gate,
               preset.nudge,
               preset.probability
           );
}

FLASHMEM bool stepGraphPresetGraphIsCanonical(
    const StepSequencerGraph& graph
) {
    if (!graph.enabled ||
        graph.stepNodeCount == 0 ||
        graph.stepNodeCount > graph.stepNodes.size() ||
        graph.sequenceCount > graph.sequences.size() ||
        graph.cycleSetCount > graph.cycleSets.size() ||
        graph.stepNode(kAssetRootNodeId) == nullptr) {
        return false;
    }

    for (uint8_t i = 0; i < graph.sequenceCount; ++i) {
        const auto& sequence = graph.sequences[i];
        if (!graph_policy::sequenceIsCanonical(sequence) ||
            graph.sequence(i) == nullptr) {
            return false;
        }
    }
    for (uint8_t i = 0; i < graph.cycleSetCount; ++i) {
        if (graph.cycleSet(i) == nullptr) return false;
    }
    for (uint16_t i = 0; i < graph.stepNodeCount; ++i) {
        const auto* node = graph.stepNode(i);
        if (node == nullptr ||
            !graph_policy::stepNodeIsCanonical(*node)) {
            return false;
        }
        if (node->has(STEP_NODE_CHILD_SEQUENCE) &&
            graph.sequence(node->childSequenceId) == nullptr) {
            return false;
        }
        if (node->has(STEP_NODE_CYCLE_SET) &&
            graph.cycleSet(node->cycleSetId) == nullptr) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool captureStepGraphPreset(
    const SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    out.reset();
    if (!scaleSettingsCanonical(sourceScale) ||
        step >= activeContentLength(sequencer)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }

    const bool rootContext = isRootContentView(sequencer);
    const auto* graph = graphView(sequencer.pattern);
    const SequencerGraphNodeId sourceNodeId =
        activeContentStepNodeId(sequencer, step);
    if (sourceNodeId == StepSequencerGraphLimits::INVALID_ID) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (!rootContext &&
        (graph == nullptr || graph->stepNode(sourceNodeId) == nullptr)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (!buildAssetGraphFromSourceNode(graph, sourceNodeId, out.graph)) {
        setReportStatus(report, SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED);
        return false;
    }

    out.valid = true;
    out.rootContext = rootContext;
    out.rootValuesValid = rootContext;
    const auto scalePolicy = pitchContextUsesScaleDegrees(
        authoringPattern(sequencer).pitchEditMode,
        sourceScale
    )
        ? SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
        : SequencerStepGraphPreset::ScalePolicy::CHROMATIC;
    const auto persistedSourceScale =
        scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
            ? sourceScale
            : oc::note::sequencer::StepSequencerScaleSettings{};
    if (!setStepGraphPresetMetadata(
            out,
            "unsaved",
            "Untitled",
            scalePolicy,
            persistedSourceScale
        )) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (rootContext) {
        out.enabled = sequencer.pattern.isEnabled(step);
        out.note = sequencer.pattern.note[step];
        out.velocity = sequencer.pattern.velocity[step];
        out.gate = sequencer.pattern.gate[step];
        out.nudge = sequencer.pattern.nudge[step];
        out.probability = sequencer.pattern.probability[step];
        if (report != nullptr) {
            report->flags = static_cast<uint16_t>(
                report->flags | SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES
            );
        }
    }

    if (report != nullptr) {
        report->flags = static_cast<uint16_t>(
            report->flags | SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD
        );
        fillReportCounts(report, out.graph);
    }
    return true;
}

FLASHMEM bool captureRootStepGraphPreset(
    const SequencerPatternState& pattern,
    SequencerGraphNodeId sourceNodeId,
    const SequencerStepGraphRootValues& rootValues,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    out.reset();
    const auto* graph = sourceNodeId == StepSequencerGraphLimits::INVALID_ID
        ? nullptr
        : graphView(pattern);
    if (graph != nullptr && graph->stepNode(sourceNodeId) == nullptr) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    return captureRootPresetFromSource(
        pattern,
        graph,
        sourceNodeId,
        rootValues,
        sourceScale,
        out,
        report
    );
}

FLASHMEM bool applyStepGraphPreset(
    SequencerState& sequencer,
    uint8_t step,
    const SequencerStepGraphPreset& preset,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    if (!preset.valid ||
        !stepGraphPresetMetadataIsCanonical(preset) ||
        !stepGraphPresetGraphIsCanonical(preset.graph) ||
        preset.graph.stepNode(kAssetRootNodeId) == nullptr) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (step >= activeContentLength(sequencer)) {
        setReportStatus(
            report,
            SequencerGraphAssetStatus::INCOMPATIBLE_TARGET
        );
        return false;
    }
    if (preset.rootContext != isRootContentView(sequencer)) {
        setReportStatus(
            report,
            SequencerGraphAssetStatus::INCOMPATIBLE_TARGET
        );
        return false;
    }

    const auto targetNodeId = activeContentStepNodeId(sequencer, step);
    if (targetNodeId == StepSequencerGraphLimits::INVALID_ID) {
        setReportStatus(
            report,
            SequencerGraphAssetStatus::INCOMPATIBLE_TARGET
        );
        return false;
    }

    const bool rootValuesExisted =
        preset.rootContext &&
        preset.rootValuesValid &&
        (sequencer.pattern.isEnabled(step) ||
         sequencer.pattern.note[step] != SequencerState::DEFAULT_NOTE ||
         sequencer.pattern.velocity[step] !=
             SequencerState::DEFAULT_VELOCITY ||
         sequencer.pattern.gate[step] !=
             SequencerState::DEFAULT_GATE_PERCENT ||
         sequencer.pattern.nudge[step] != 0 ||
         sequencer.pattern.probability[step] !=
             SequencerState::DEFAULT_PROBABILITY);

    if (!copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNodeId,
            preset.graph,
            kAssetRootNodeId
        )) {
        setReportStatus(report, SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED);
        return false;
    }
    const bool compacted = compactSequencerGraph(sequencer);

    if (preset.rootContext && preset.rootValuesValid) {
        if (report != nullptr && rootValuesExisted) {
            report->flags = static_cast<uint16_t>(
                report->flags | SEQUENCER_GRAPH_ASSET_REPORT_OVERWRITE
            );
        }
        sequencer.pattern.setEnabled(step, preset.enabled);
        (void)sequencer.setStepDataAt(
            step,
            preset.note,
            preset.velocity,
            preset.gate,
            preset.nudge,
            preset.probability
        );
        if (report != nullptr) {
            report->flags = static_cast<uint16_t>(
                report->flags | SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES
            );
        }
    }

    if (!compacted) {
        refreshContentView(sequencer);
        if (!preset.rootContext) sequencer.contentView.bump();
    }
    if (report != nullptr) {
        report->flags = static_cast<uint16_t>(
            report->flags | SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD
        );
        fillReportCounts(report, preset.graph);
    }
    return true;
}

FLASHMEM bool applyStepGraphPresetGraphToNode(
    SequencerPatternState& pattern,
    SequencerGraphNodeId targetNodeId,
    const SequencerStepGraphPreset& preset,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    if (!preset.valid ||
        !stepGraphPresetMetadataIsCanonical(preset) ||
        !stepGraphPresetGraphIsCanonical(preset.graph) ||
        preset.graph.stepNode(kAssetRootNodeId) == nullptr ||
        targetNodeId == StepSequencerGraphLimits::INVALID_ID) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (!copyStepNodePayloadFromGraph(
            pattern,
            targetNodeId,
            preset.graph,
            kAssetRootNodeId
        )) {
        setReportStatus(report, SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED);
        return false;
    }
    (void)compactGraph(pattern);
    if (report != nullptr) {
        report->flags = SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD;
        fillReportCounts(report, preset.graph);
    }
    return true;
}

FLASHMEM bool projectStepGraphPresetToDestinationPitch(
    SequencerStepGraphPreset& preset,
    uint8_t destinationNote,
    bool* changed
) {
    if (changed != nullptr) *changed = false;
    if (destinationNote > 127U || !preset.valid ||
        !stepGraphPresetMetadataIsCanonical(preset) ||
        !stepGraphPresetGraphIsCanonical(preset.graph)) {
        return false;
    }

    bool adapted = preset.rootValuesValid && preset.note != destinationNote;
    if (preset.rootValuesValid) preset.note = destinationNote;
    for (uint16_t index = 0; index < preset.graph.stepNodeCount; ++index) {
        auto& node = preset.graph.stepNodes[index];
        const bool nodePitch =
            node.has(STEP_NODE_NOTE_OFFSET) ||
            node.has(STEP_NODE_CHORD_MODE) ||
            node.has(STEP_NODE_CHORD_LOCAL) ||
            node.noteOffset != 0 ||
            node.localVariation.pitchSemitones != 0;
        adapted = adapted || nodePitch;
        node.flags = static_cast<decltype(node.flags)>(
            node.flags &
            ~(STEP_NODE_NOTE_OFFSET | STEP_NODE_CHORD_MODE |
              STEP_NODE_CHORD_LOCAL)
        );
        const StepSequencerStepNode defaults{};
        node.noteOffset = defaults.noteOffset;
        node.chordMode = defaults.chordMode;
        node.chordSpec = defaults.chordSpec;
        node.localVariation.pitchSemitones = 0;
    }

    // Once every authored pitch relation has been removed, the payload is
    // scale-independent. Canonicalizing it to Chromatic prevents a Drum-saved
    // rhythm from being needlessly blocked by another destination scale.
    if (preset.scalePolicy != SequencerStepGraphPreset::ScalePolicy::CHROMATIC ||
        preset.sourceScale.isConstrained()) {
        adapted = true;
        preset.scalePolicy = SequencerStepGraphPreset::ScalePolicy::CHROMATIC;
        preset.sourceScale = {};
    }
    if (changed != nullptr) *changed = adapted;
    return stepGraphPresetMetadataIsCanonical(preset) &&
        stepGraphPresetGraphIsCanonical(preset.graph);
}

FLASHMEM bool validStepGraphPresetTechnicalId(const char* technicalId) {
    return validSequencerPresetTechnicalId(technicalId);
}

FLASHMEM bool validStepGraphPresetSemanticName(const char* semanticName) {
    return validSequencerPresetSemanticName(semanticName);
}

FLASHMEM bool setStepGraphPresetMetadata(
    SequencerStepGraphPreset& preset,
    const char* technicalId,
    const char* semanticName,
    SequencerStepGraphPreset::ScalePolicy scalePolicy,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale
) {
    if (!validStepGraphPresetTechnicalId(technicalId) ||
        !validStepGraphPresetSemanticName(semanticName) ||
        scalePolicy > SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE ||
        !scaleSettingsCanonical(sourceScale)) {
        return false;
    }
    if (scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE) {
        if (!sourceScale.isConstrained()) return false;
    } else {
        sourceScale = {};
    }

    std::memset(preset.technicalId, 0, sizeof(preset.technicalId));
    std::memset(preset.semanticName, 0, sizeof(preset.semanticName));
    std::strncpy(
        preset.technicalId,
        technicalId,
        sizeof(preset.technicalId) - 1U
    );
    std::strncpy(
        preset.semanticName,
        semanticName,
        sizeof(preset.semanticName) - 1U
    );
    preset.formatVersion = SequencerStepGraphPreset::CURRENT_FORMAT_VERSION;
    preset.scalePolicy = scalePolicy;
    preset.sourceScale = sourceScale;
    return true;
}

FLASHMEM bool adaptStepGraphPresetPitchToDestination(
    SequencerStepGraphPreset& preset,
    oc::note::sequencer::StepSequencerScaleSettings destinationScale,
    bool* changed
) {
    if (changed != nullptr) *changed = false;
    if (!preset.valid || !stepGraphPresetMetadataIsCanonical(preset)) {
        return false;
    }
    if (preset.scalePolicy == SequencerStepGraphPreset::ScalePolicy::CHROMATIC ||
        !preset.rootContext ||
        !preset.rootValuesValid) {
        return true;
    }

    const auto sourceScale = preset.sourceScale;
    if (!scaleSettingsCanonical(destinationScale) ||
        !destinationScale.isConstrained()) {
        return false;
    }
    const uint8_t sourceNote =
        oc::note::sequencer::resolveScaleNote(
            preset.note,
            sourceScale
        ).outputNote;
    const int sourceRelative =
        static_cast<int>(sourceNote) - sourceScale.root;
    const int sourceOctave = floorDiv12(sourceRelative);
    const uint8_t sourceDegree = scaleDegreeForPitchClass(
        sourceScale,
        oc::note::sequencer::pitchClass(sourceNote)
    );
    const uint8_t destinationDegreeCount = scaleDegreeCount(destinationScale);
    const int destinationOctave =
        sourceOctave +
        static_cast<int>(sourceDegree / destinationDegreeCount);
    const uint8_t destinationDegree = static_cast<uint8_t>(
        sourceDegree % destinationDegreeCount
    );
    const int destinationNote =
        static_cast<int>(destinationScale.root) +
        destinationOctave * 12 +
        scaleIntervalForDegree(destinationScale, destinationDegree);
    const uint8_t adapted =
        oc::note::sequencer::clampScaleMidiNote(destinationNote);
    if (changed != nullptr) {
        *changed =
            adapted != preset.note ||
            !sameScaleSettings(sourceScale, destinationScale);
    }
    preset.note = adapted;
    preset.sourceScale = destinationScale;
    return true;
}

}  // namespace core::state::sequencer

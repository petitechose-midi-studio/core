#include "state/sequencer/SequencerGraphAssetCodec.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "state/project/ProjectSlug.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphAssetRecords.hpp"
#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;
using namespace graph_ops_internal;
namespace binary = core::persistence::binary_codec;

constexpr uint32_t kStepGraphPresetMagic = 0x31504753;  // "SGP1"
constexpr uint8_t kStepGraphPresetVersion =
    SequencerStepGraphPreset::CURRENT_FORMAT_VERSION;
constexpr uint8_t kStepGraphPresetKind = 1;
constexpr uint8_t kStepGraphPresetFlagRootContext = 1U << 0;
constexpr uint8_t kStepGraphPresetFlagRootValues = 1U << 1;
constexpr uint16_t kAssetRootNodeId = SequencerStepGraphPreset::ASSET_ROOT_NODE_ID;
constexpr uint8_t kStepGraphPresetScalePolicyMask = 0x01U;
constexpr uint8_t kStepGraphPresetScalePolicyMixed = 0x80U;

struct StepGraphPresetHeader {
    uint32_t magic = kStepGraphPresetMagic;
    uint8_t version = kStepGraphPresetVersion;
    uint8_t kind = kStepGraphPresetKind;
    uint8_t headerSize = static_cast<uint8_t>(STEP_GRAPH_PRESET_HEADER_SIZE);
    uint8_t flags = kStepGraphPresetFlagRootContext;
    uint8_t enabled = 0;
    uint8_t note = SequencerState::DEFAULT_NOTE;
    uint8_t velocity = SequencerState::DEFAULT_VELOCITY;
    uint16_t gate = SequencerState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
    uint16_t stepNodeCount = 0;
    uint8_t sequenceCount = 0;
    uint8_t cycleSetCount = 0;
    uint16_t reserved0 = 0;
};

struct StepGraphPresetMetadata {
    uint8_t scalePolicy = static_cast<uint8_t>(
        SequencerStepGraphPreset::ScalePolicy::CHROMATIC
    );
    uint8_t sourceScaleRoot = 0;
    uint8_t sourceScaleType = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleType::Chromatic
    );
    uint8_t sourceScaleMode = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleConstraintMode::Free
    );
    char technicalId[SequencerStepGraphPreset::TECHNICAL_ID_SIZE] = {};
    char semanticName[SequencerStepGraphPreset::SEMANTIC_NAME_SIZE] = {};
};

static_assert(STEP_GRAPH_PRESET_HEADER_SIZE <= UINT8_MAX);

FLASHMEM void setReportStatus(
    SequencerGraphAssetReport* report,
    SequencerGraphAssetStatus status
) {
    if (report != nullptr) {
        report->status = status;
    }
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

FLASHMEM SequencerGraphSequenceRecord sequenceRecord(
    const StepSequencerSequence& source
) {
    return SequencerGraphSequenceRecord{
        .kind = static_cast<uint8_t>(source.kind),
        .firstStepNode = source.firstStepNode,
        .length = source.length,
        .offset = source.offset,
    };
}

FLASHMEM SequencerGraphStepNodeRecord stepNodeRecord(
    const StepSequencerStepNode& source
) {
    return SequencerGraphStepNodeRecord{
        .flags = source.flags,
        .noteOffset = source.noteOffset,
        .velocityOffset = source.velocityOffset,
        .gateOffset = source.gateOffset,
        .nudgeOffset = source.nudgeOffset,
        .probabilityOffset = source.probabilityOffset,
        .childSequenceId = source.childSequenceId,
        .cycleSetId = source.cycleSetId,
        .localVariationPitchSemitones = source.localVariation.pitchSemitones,
        .localVariationVelocity = source.localVariation.velocity,
        .localVariationGatePercent = source.localVariation.gatePercent,
        .localVariationNudge = source.localVariation.nudge,
        .chordMode = static_cast<uint8_t>(source.chordMode),
        .chordVoiceCount = source.chordSpec.voiceCount,
        .chordHarmonyData = source.chordSpec.harmonyData,
        .chordVoicingData = source.chordSpec.voicingData,
        .chordInversionData = source.chordSpec.inversionData,
        .chordStrum = source.chordSpec.strum,
        .chordVelocityCurve = source.chordSpec.velocityCurve,
    };
}

FLASHMEM SequencerGraphCycleSetRecord cycleSetRecord(
    const StepSequencerCycleStateSet& source
) {
    return SequencerGraphCycleSetRecord{
        .firstStateNode = source.firstStateNode,
        .length = source.length,
        .offset = source.offset,
    };
}

FLASHMEM bool writeBaseHeader(binary::Writer& writer, const StepGraphPresetHeader& header) {
    return writer.writeU32(header.magic) &&
           writer.writeU8(header.version) &&
           writer.writeU8(header.kind) &&
           writer.writeU8(header.headerSize) &&
           writer.writeU8(header.flags) &&
           writer.writeU8(header.enabled) &&
           writer.writeU8(header.note) &&
           writer.writeU8(header.velocity) &&
           writer.writeU16(header.gate) &&
           writer.writeI8(header.nudge) &&
           writer.writeU8(header.probability) &&
           writer.writeU16(header.stepNodeCount) &&
           writer.writeU8(header.sequenceCount) &&
           writer.writeU8(header.cycleSetCount) &&
           writer.writeU16(header.reserved0);
}

FLASHMEM bool readBaseHeader(
    binary::Reader& reader,
    StepGraphPresetHeader& header
) {
    return reader.readU32(header.magic) &&
           reader.readU8(header.version) &&
           reader.readU8(header.kind) &&
           reader.readU8(header.headerSize) &&
           reader.readU8(header.flags) &&
           reader.readU8(header.enabled) &&
           reader.readU8(header.note) &&
           reader.readU8(header.velocity) &&
           reader.readU16(header.gate) &&
           reader.readI8(header.nudge) &&
           reader.readU8(header.probability) &&
           reader.readU16(header.stepNodeCount) &&
           reader.readU8(header.sequenceCount) &&
           reader.readU8(header.cycleSetCount) &&
           reader.readU16(header.reserved0);
}

FLASHMEM bool writeMetadata(
    binary::Writer& writer,
    const StepGraphPresetMetadata& metadata
) {
    return writer.writeU8(metadata.scalePolicy) &&
           writer.writeU8(metadata.sourceScaleRoot) &&
           writer.writeU8(metadata.sourceScaleType) &&
           writer.writeU8(metadata.sourceScaleMode) &&
           writer.writeBytes(metadata.technicalId, sizeof(metadata.technicalId)) &&
           writer.writeBytes(metadata.semanticName, sizeof(metadata.semanticName));
}

FLASHMEM bool readMetadata(
    binary::Reader& reader,
    StepGraphPresetMetadata& metadata
) {
    return reader.readU8(metadata.scalePolicy) &&
           reader.readU8(metadata.sourceScaleRoot) &&
           reader.readU8(metadata.sourceScaleType) &&
           reader.readU8(metadata.sourceScaleMode) &&
           reader.readBytes(metadata.technicalId, sizeof(metadata.technicalId)) &&
           reader.readBytes(metadata.semanticName, sizeof(metadata.semanticName));
}

template <size_t N>
FLASHMEM bool fixedTextValid(const char (&text)[N], bool allowEmpty) {
    if (text[N - 1U] != '\0') return false;
    const size_t length = std::strlen(text);
    if (!allowEmpty && length == 0) return false;
    for (size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 32U || byte == 127U) return false;
    }
    return true;
}

FLASHMEM bool boundedTextLength(
    const char* text,
    size_t capacity,
    size_t& length
) {
    length = 0;
    if (text == nullptr || capacity == 0) return false;
    while (length < capacity && text[length] != '\0') ++length;
    return length < capacity;
}

FLASHMEM bool validUtf8Text(const char* text, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const uint8_t first = static_cast<uint8_t>(text[offset]);
        uint32_t codePoint = 0;
        size_t width = 0;
        if (first < 0x80U) {
            codePoint = first;
            width = 1;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            codePoint = static_cast<uint32_t>(first & 0x1FU);
            width = 2;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codePoint = static_cast<uint32_t>(first & 0x0FU);
            width = 3;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codePoint = static_cast<uint32_t>(first & 0x07U);
            width = 4;
        } else {
            return false;
        }
        if (width > length - offset) return false;
        for (size_t i = 1; i < width; ++i) {
            const uint8_t continuation = static_cast<uint8_t>(text[offset + i]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }
        const bool overlong =
            (width == 2 && codePoint < 0x80U) ||
            (width == 3 && codePoint < 0x800U) ||
            (width == 4 && codePoint < 0x10000U);
        if (overlong || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint < 0x20U ||
            (codePoint >= 0x7FU && codePoint <= 0x9FU)) {
            return false;
        }
        offset += width;
    }
    return true;
}

FLASHMEM bool sameScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings lhs,
    oc::note::sequencer::StepSequencerScaleSettings rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

FLASHMEM int floorDiv12(int value) {
    if (value >= 0) return value / 12;
    return -static_cast<int>((-value + 11) / 12);
}

FLASHMEM uint8_t scaleDegreeForPitchClass(
    oc::note::sequencer::StepSequencerScaleSettings scale,
    uint8_t notePitchClass
) {
    scale.clamp();
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
    scale.clamp();
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
    scale.clamp();
    const uint16_t mask = oc::note::sequencer::scaleMask(scale.type);
    uint8_t count = 0;
    for (uint8_t interval = 0; interval < 12; ++interval) {
        if ((mask & static_cast<uint16_t>(1U << interval)) != 0) ++count;
    }
    return count == 0 ? 1 : count;
}

FLASHMEM bool nodeUsesChromaticPitch(const StepSequencerStepNode& node) {
    return node.has(oc::note::sequencer::STEP_NODE_PITCH_CHROMATIC);
}

FLASHMEM bool graphHasMixedPitchPolicy(
    const SequencerStepGraphPreset& preset,
    SequencerStepGraphPreset::ScalePolicy defaultPolicy
) {
    const bool defaultChromatic =
        defaultPolicy == SequencerStepGraphPreset::ScalePolicy::CHROMATIC;
    for (uint16_t i = 0; i < preset.graph.stepNodeCount; ++i) {
        if (nodeUsesChromaticPitch(preset.graph.stepNodes[i]) != defaultChromatic) {
            return true;
        }
    }
    return false;
}

FLASHMEM bool appendSequenceRecord(binary::Writer& writer,
                                   const SequencerGraphSequenceRecord& record) {
    uint8_t bytes[SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE] = {};
    return encodeSequencerGraphSequenceRecord(
               record,
               bytes,
               SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE
           ) &&
           writer.writeBytes(bytes, SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE);
}

FLASHMEM bool appendStepNodeRecord(binary::Writer& writer,
                                   const SequencerGraphStepNodeRecord& record) {
    uint8_t bytes[SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE] = {};
    return encodeSequencerGraphStepNodeRecord(
               record,
               bytes,
               SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE
           ) &&
           writer.writeBytes(bytes, SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE);
}

FLASHMEM bool appendCycleSetRecord(binary::Writer& writer,
                                   const SequencerGraphCycleSetRecord& record) {
    uint8_t bytes[SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE] = {};
    return encodeSequencerGraphCycleSetRecord(
               record,
               bytes,
               SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE
           ) &&
           writer.writeBytes(bytes, SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE);
}

FLASHMEM bool linksValid(const StepSequencerGraph& graph) {
    if (!graph.enabled || graph.stepNodeCount == 0 || graph.stepNode(kAssetRootNodeId) == nullptr) {
        return false;
    }

    for (uint16_t i = 0; i < graph.stepNodeCount; ++i) {
        const auto* node = graph.stepNode(i);
        if (node == nullptr) return false;
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
    copyStepNodeValuesWithoutChildren(out.stepNodes[kAssetRootNodeId], sourceNode);

    if (sourceGraph == nullptr) return true;
    return copyChildrenIntoNode(
        out,
        out.stepNodes[kAssetRootNodeId],
        *sourceGraph,
        sourceNode
    );
}

FLASHMEM bool decodeGraph(
    const uint8_t* data,
    uint16_t size,
    uint16_t& offset,
    const StepGraphPresetHeader& header,
    StepSequencerGraph& graph
) {
    if (header.stepNodeCount == 0 ||
        header.stepNodeCount > StepSequencerGraphLimits::MAX_STEP_NODES ||
        header.sequenceCount > StepSequencerGraphLimits::MAX_SEQUENCES ||
        header.cycleSetCount > StepSequencerGraphLimits::MAX_CYCLE_SETS) {
        return false;
    }

    graph.reset();
    graph.enabled = true;
    graph.rootSequenceId = StepSequencerGraphLimits::INVALID_ID;
    graph.stepNodeCount = header.stepNodeCount;
    graph.sequenceCount = header.sequenceCount;
    graph.cycleSetCount = header.cycleSetCount;

    for (uint16_t i = 0; i < header.sequenceCount; ++i) {
        SequencerGraphSequenceRecord record{};
        if (offset > size ||
            SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE > static_cast<uint16_t>(size - offset) ||
            !decodeSequencerGraphSequenceRecord(
                data + offset,
                SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE,
                record
            )) {
            return false;
        }
        offset = static_cast<uint16_t>(offset + SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE);
        graph.sequences[i] = StepSequencerSequence{
            .kind = static_cast<StepSequencerSequenceKind>(record.kind),
            .firstStepNode = record.firstStepNode,
            .length = record.length,
            .offset = record.offset,
        };
    }

    for (uint16_t i = 0; i < header.stepNodeCount; ++i) {
        SequencerGraphStepNodeRecord record{};
        if (offset > size ||
            SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE > static_cast<uint16_t>(size - offset) ||
            !decodeSequencerGraphStepNodeRecord(
                data + offset,
                SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE,
                record
            )) {
            return false;
        }
        offset = static_cast<uint16_t>(offset + SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE);
        StepSequencerChordSpec chordSpec{};
        if (!decodeSequencerGraphChordSpec(record, chordSpec)) return false;
        graph.stepNodes[i] = StepSequencerStepNode{
            .flags = record.flags,
            .noteOffset = record.noteOffset,
            .velocityOffset = record.velocityOffset,
            .gateOffset = record.gateOffset,
            .nudgeOffset = record.nudgeOffset,
            .probabilityOffset = record.probabilityOffset,
            .localVariation = oc::note::sequencer::StepSequencerVariationRanges{
                .pitchSemitones = record.localVariationPitchSemitones,
                .velocity = record.localVariationVelocity,
                .gatePercent = record.localVariationGatePercent,
                .nudge = record.localVariationNudge,
            },
            .chordMode = sanitizeSequencerGraphChordMode(record.chordMode),
            .chordSpec = chordSpec,
            .childSequenceId = record.childSequenceId,
            .cycleSetId = record.cycleSetId,
        };
        graph.stepNodes[i].localVariation.clamp();
    }

    for (uint16_t i = 0; i < header.cycleSetCount; ++i) {
        SequencerGraphCycleSetRecord record{};
        if (offset > size ||
            SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE > static_cast<uint16_t>(size - offset) ||
            !decodeSequencerGraphCycleSetRecord(
                data + offset,
                SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE,
                record
            )) {
            return false;
        }
        offset = static_cast<uint16_t>(offset + SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE);
        graph.cycleSets[i] = StepSequencerCycleStateSet{
            .firstStateNode = record.firstStateNode,
            .length = record.length,
            .offset = record.offset,
        };
    }

    return linksValid(graph);
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
    mixedPitchPolicy = false;
    technicalId[0] = '\0';
    semanticName[0] = '\0';
    scalePolicy = ScalePolicy::CHROMATIC;
    sourceScale = {};
    sourceScale.clamp();
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

FLASHMEM bool captureStepGraphPreset(
    const SequencerState& sequencer,
    uint8_t step,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    out.reset();
    if (step >= activeContentLength(sequencer)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }

    const bool rootContext = isRootContentView(sequencer);
    const auto* graph = graphView(sequencer.pattern);
    const SequencerGraphNodeId sourceNodeId = activeContentStepNodeId(sequencer, step);
    if (sourceNodeId == StepSequencerGraphLimits::INVALID_ID) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }

    if (!rootContext && (graph == nullptr || graph->stepNode(sourceNodeId) == nullptr)) {
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
    if (!setStepGraphPresetMetadata(
            out,
            "unsaved",
            "Untitled",
            sequencer.pattern.pitchEditMode == SequencerPitchEditMode::SCALE_DEGREES
                ? SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
                : SequencerStepGraphPreset::ScalePolicy::CHROMATIC,
            {}
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

FLASHMEM bool applyStepGraphPreset(
    SequencerState& sequencer,
    uint8_t step,
    const SequencerStepGraphPreset& preset,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    if (!preset.valid || preset.graph.stepNode(kAssetRootNodeId) == nullptr) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (step >= activeContentLength(sequencer)) {
        setReportStatus(report, SequencerGraphAssetStatus::INCOMPATIBLE_TARGET);
        return false;
    }
    if (preset.rootContext != isRootContentView(sequencer)) {
        setReportStatus(report, SequencerGraphAssetStatus::INCOMPATIBLE_TARGET);
        return false;
    }

    const auto targetNodeId = activeContentStepNodeId(sequencer, step);
    if (targetNodeId == StepSequencerGraphLimits::INVALID_ID) {
        setReportStatus(report, SequencerGraphAssetStatus::INCOMPATIBLE_TARGET);
        return false;
    }

    const bool rootValuesExisted = preset.rootContext && preset.rootValuesValid &&
        (sequencer.pattern.isEnabled(step) ||
         sequencer.pattern.note[step] != SequencerState::DEFAULT_NOTE ||
         sequencer.pattern.velocity[step] != SequencerState::DEFAULT_VELOCITY ||
         sequencer.pattern.gate[step] != SequencerState::DEFAULT_GATE_PERCENT ||
         sequencer.pattern.nudge[step] != 0 ||
         sequencer.pattern.probability[step] != SequencerState::DEFAULT_PROBABILITY);

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

FLASHMEM SequencerGraphAssetEncodeResult encodeStepGraphPreset(
    const SequencerStepGraphPreset& preset,
    uint8_t* out,
    uint16_t capacity
) {
    if (!preset.valid ||
        out == nullptr ||
        preset.graph.stepNode(kAssetRootNodeId) == nullptr ||
        (preset.rootValuesValid && !preset.rootContext) ||
        !linksValid(preset.graph) ||
        !fixedTextValid(preset.technicalId, false) ||
        !fixedTextValid(preset.semanticName, false) ||
        !validStepGraphPresetTechnicalId(preset.technicalId) ||
        !validStepGraphPresetSemanticName(preset.semanticName)) {
        return {.status = SequencerGraphAssetStatus::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    StepGraphPresetHeader header{};
    header.flags = 0;
    if (preset.rootContext) {
        header.flags = static_cast<uint8_t>(header.flags | kStepGraphPresetFlagRootContext);
    }
    if (preset.rootValuesValid) {
        header.flags = static_cast<uint8_t>(header.flags | kStepGraphPresetFlagRootValues);
    }
    header.enabled = preset.enabled ? 1U : 0U;
    header.note = preset.note;
    header.velocity = preset.velocity;
    header.gate = preset.gate;
    header.nudge = preset.nudge;
    header.probability = preset.probability;
    header.stepNodeCount = preset.graph.stepNodeCount;
    header.sequenceCount = preset.graph.sequenceCount;
    header.cycleSetCount = preset.graph.cycleSetCount;

    StepGraphPresetMetadata metadata{};
    const bool mixedPitchPolicy = graphHasMixedPitchPolicy(
        preset,
        preset.scalePolicy
    );
    metadata.scalePolicy = static_cast<uint8_t>(preset.scalePolicy);
    if (mixedPitchPolicy) {
        metadata.scalePolicy = static_cast<uint8_t>(
            metadata.scalePolicy | kStepGraphPresetScalePolicyMixed
        );
    }
    auto sourceScale = preset.sourceScale;
    sourceScale.clamp();
    metadata.sourceScaleRoot = sourceScale.root;
    metadata.sourceScaleType = static_cast<uint8_t>(sourceScale.type);
    metadata.sourceScaleMode = static_cast<uint8_t>(sourceScale.mode);
    std::memcpy(
        metadata.technicalId,
        preset.technicalId,
        sizeof(metadata.technicalId)
    );
    std::memcpy(
        metadata.semanticName,
        preset.semanticName,
        sizeof(metadata.semanticName)
    );

    binary::Writer writer(out, capacity);
    if (!writeBaseHeader(writer, header) || !writeMetadata(writer, metadata)) {
        return {.status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL, .bytesWritten = 0};
    }

    for (uint16_t i = 0; i < preset.graph.sequenceCount; ++i) {
        const auto record = sequenceRecord(preset.graph.sequences[i]);
        if (!appendSequenceRecord(writer, record)) {
            return {.status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL, .bytesWritten = 0};
        }
    }
    for (uint16_t i = 0; i < preset.graph.stepNodeCount; ++i) {
        const auto record = stepNodeRecord(preset.graph.stepNodes[i]);
        if (!appendStepNodeRecord(writer, record)) {
            return {.status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL, .bytesWritten = 0};
        }
    }
    for (uint16_t i = 0; i < preset.graph.cycleSetCount; ++i) {
        const auto record = cycleSetRecord(preset.graph.cycleSets[i]);
        if (!appendCycleSetRecord(writer, record)) {
            return {.status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL, .bytesWritten = 0};
        }
    }

    return writer.ok()
        ? SequencerGraphAssetEncodeResult{
              .status = SequencerGraphAssetStatus::OK,
              .bytesWritten = static_cast<uint16_t>(writer.offset()),
          }
        : SequencerGraphAssetEncodeResult{
              .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
              .bytesWritten = 0,
          };
}

FLASHMEM bool decodeStepGraphPresetMetadata(
    const uint8_t* data,
    uint16_t size,
    SequencerStepGraphPresetMetadataView& out,
    SequencerGraphAssetReport* report
) {
    out = {};
    if (report != nullptr) report->reset();
    if (data == nullptr || size < STEP_GRAPH_PRESET_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }

    binary::Reader reader(data, size);
    StepGraphPresetHeader header{};
    if (!readBaseHeader(reader, header) ||
        reader.offset() != STEP_GRAPH_PRESET_BASE_HEADER_SIZE ||
        header.magic != kStepGraphPresetMagic ||
        header.kind != kStepGraphPresetKind) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.version != kStepGraphPresetVersion) {
        setReportStatus(report, SequencerGraphAssetStatus::UNSUPPORTED_VERSION);
        return false;
    }
    if (header.headerSize != STEP_GRAPH_PRESET_HEADER_SIZE ||
        size < STEP_GRAPH_PRESET_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    StepGraphPresetMetadata metadata{};
    const uint8_t allowedPolicyBits = static_cast<uint8_t>(
        kStepGraphPresetScalePolicyMask | kStepGraphPresetScalePolicyMixed
    );
    if (!readMetadata(reader, metadata) ||
        reader.offset() != STEP_GRAPH_PRESET_HEADER_SIZE ||
        (metadata.scalePolicy & static_cast<uint8_t>(~allowedPolicyBits)) != 0 ||
        !fixedTextValid(metadata.technicalId, false) ||
        !fixedTextValid(metadata.semanticName, false) ||
        !validStepGraphPresetTechnicalId(metadata.technicalId) ||
        !validStepGraphPresetSemanticName(metadata.semanticName)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    out.formatVersion = header.version;
    out.mixedPitchPolicy =
        (metadata.scalePolicy & kStepGraphPresetScalePolicyMixed) != 0;
    out.scalePolicy = static_cast<SequencerStepGraphPreset::ScalePolicy>(
        metadata.scalePolicy & kStepGraphPresetScalePolicyMask
    );
    out.sourceScale = {
        .root = metadata.sourceScaleRoot,
        .type = static_cast<oc::note::sequencer::StepSequencerScaleType>(
            metadata.sourceScaleType
        ),
        .mode = static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(
            metadata.sourceScaleMode
        ),
    };
    const auto rawRoot = out.sourceScale.root;
    const auto rawType = out.sourceScale.type;
    const auto rawMode = out.sourceScale.mode;
    out.sourceScale.clamp();
    if (out.sourceScale.root != rawRoot || out.sourceScale.type != rawType ||
        out.sourceScale.mode != rawMode) {
        out = {};
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    std::memcpy(out.technicalId, metadata.technicalId, sizeof(out.technicalId));
    std::memcpy(out.semanticName, metadata.semanticName, sizeof(out.semanticName));
    return true;
}

FLASHMEM bool decodeStepGraphPreset(
    const uint8_t* data,
    uint16_t size,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    out.reset();
    if (data == nullptr || size < STEP_GRAPH_PRESET_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }

    binary::Reader reader(data, size);
    StepGraphPresetHeader header{};
    if (!readBaseHeader(reader, header) ||
        reader.offset() != STEP_GRAPH_PRESET_BASE_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.magic != kStepGraphPresetMagic ||
        header.kind != kStepGraphPresetKind) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.version != kStepGraphPresetVersion) {
        setReportStatus(report, SequencerGraphAssetStatus::UNSUPPORTED_VERSION);
        return false;
    }

    if (header.headerSize != STEP_GRAPH_PRESET_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    StepGraphPresetMetadata metadata{};
    const uint8_t allowedPolicyBits = static_cast<uint8_t>(
        kStepGraphPresetScalePolicyMask | kStepGraphPresetScalePolicyMixed
    );
    if (!readMetadata(reader, metadata) ||
        reader.offset() != STEP_GRAPH_PRESET_HEADER_SIZE ||
        (metadata.scalePolicy & static_cast<uint8_t>(~allowedPolicyBits)) != 0 ||
        !fixedTextValid(metadata.technicalId, false) ||
        !fixedTextValid(metadata.semanticName, false) ||
        !validStepGraphPresetTechnicalId(metadata.technicalId) ||
        !validStepGraphPresetSemanticName(metadata.semanticName)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    out.formatVersion = header.version;
    out.mixedPitchPolicy =
        (metadata.scalePolicy & kStepGraphPresetScalePolicyMixed) != 0;
    out.scalePolicy = static_cast<SequencerStepGraphPreset::ScalePolicy>(
        metadata.scalePolicy & kStepGraphPresetScalePolicyMask
    );
    out.sourceScale = {
        .root = metadata.sourceScaleRoot,
        .type = static_cast<oc::note::sequencer::StepSequencerScaleType>(
            metadata.sourceScaleType
        ),
        .mode = static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(
            metadata.sourceScaleMode
        ),
    };
    const auto rawRoot = out.sourceScale.root;
    const auto rawType = out.sourceScale.type;
    const auto rawMode = out.sourceScale.mode;
    out.sourceScale.clamp();
    if (out.sourceScale.root != rawRoot || out.sourceScale.type != rawType ||
        out.sourceScale.mode != rawMode) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    std::memcpy(out.technicalId, metadata.technicalId, sizeof(out.technicalId));
    std::memcpy(out.semanticName, metadata.semanticName, sizeof(out.semanticName));

    out.rootContext = (header.flags & kStepGraphPresetFlagRootContext) != 0;
    out.rootValuesValid = (header.flags & kStepGraphPresetFlagRootValues) != 0;
    if (out.rootValuesValid && !out.rootContext) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    out.enabled = header.enabled != 0;
    out.note = static_cast<uint8_t>(std::min<unsigned>(header.note, 127U));
    out.velocity = static_cast<uint8_t>(std::min<unsigned>(header.velocity, 127U));
    out.gate = SequencerState::clampGatePercent(header.gate);
    out.nudge = header.nudge;
    out.probability = SequencerState::clampProbability(header.probability);

    uint16_t offset = static_cast<uint16_t>(reader.offset());
    if (!decodeGraph(data, size, offset, header, out.graph) || offset != size) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }

    if (graphHasMixedPitchPolicy(out, out.scalePolicy) !=
        out.mixedPitchPolicy) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }

    out.valid = true;
    if (report != nullptr) {
        if (out.rootValuesValid) {
            report->flags = static_cast<uint16_t>(
                report->flags | SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES
            );
        }
        report->flags = static_cast<uint16_t>(
            report->flags | SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD
        );
        if (out.mixedPitchPolicy) {
            report->flags = static_cast<uint16_t>(
                report->flags |
                SEQUENCER_GRAPH_ASSET_REPORT_PITCH_POLICY_MIXED
            );
        }
        fillReportCounts(report, out.graph);
    }
    return true;
}

FLASHMEM bool validStepGraphPresetTechnicalId(const char* technicalId) {
    size_t length = 0;
    return boundedTextLength(
               technicalId,
               SequencerStepGraphPreset::TECHNICAL_ID_SIZE,
               length
           ) &&
           length > 0 &&
           core::state::project::validProjectSlug(technicalId);
}

FLASHMEM bool validStepGraphPresetSemanticName(const char* semanticName) {
    size_t length = 0;
    if (!boundedTextLength(
            semanticName,
            SequencerStepGraphPreset::SEMANTIC_NAME_SIZE,
            length
        ) ||
        length == 0 ||
        semanticName[0] == ' ' || semanticName[length - 1U] == ' ') {
        return false;
    }
    return validUtf8Text(semanticName, length);
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
        scalePolicy > SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE) {
        return false;
    }
    sourceScale.clamp();
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
    preset.mixedPitchPolicy = graphHasMixedPitchPolicy(preset, scalePolicy);
    return true;
}

FLASHMEM bool adaptStepGraphPresetPitchToDestination(
    SequencerStepGraphPreset& preset,
    oc::note::sequencer::StepSequencerScaleSettings destinationScale,
    bool* changed
) {
    if (changed != nullptr) *changed = false;
    if (!preset.valid) return false;
    if (preset.scalePolicy == SequencerStepGraphPreset::ScalePolicy::CHROMATIC ||
        !preset.rootContext || !preset.rootValuesValid) {
        return true;
    }

    auto sourceScale = preset.sourceScale;
    sourceScale.clamp();
    destinationScale.clamp();
    const uint8_t sourceNote =
        oc::note::sequencer::resolveScaleNote(preset.note, sourceScale).outputNote;
    const int sourceRelative = static_cast<int>(sourceNote) - sourceScale.root;
    const int sourceOctave = floorDiv12(sourceRelative);
    const uint8_t sourceDegree = scaleDegreeForPitchClass(
        sourceScale,
        oc::note::sequencer::pitchClass(sourceNote)
    );
    const uint8_t destinationDegreeCount = scaleDegreeCount(destinationScale);
    const int destinationOctave = sourceOctave +
        static_cast<int>(sourceDegree / destinationDegreeCount);
    const uint8_t destinationDegree = static_cast<uint8_t>(
        sourceDegree % destinationDegreeCount
    );
    const int destinationNote = static_cast<int>(destinationScale.root) +
        destinationOctave * 12 +
        scaleIntervalForDegree(destinationScale, destinationDegree);
    const uint8_t adapted = oc::note::sequencer::clampScaleMidiNote(destinationNote);
    if (changed != nullptr) {
        *changed = adapted != preset.note ||
            !sameScaleSettings(sourceScale, destinationScale);
    }
    preset.note = adapted;
    // The in-memory copy now describes behavior in the destination context;
    // keeping this scale lets deterministic previews resolve nested offsets
    // with the same semantics as runtime playback.
    preset.sourceScale = destinationScale;
    return true;
}

}  // namespace core::state::sequencer

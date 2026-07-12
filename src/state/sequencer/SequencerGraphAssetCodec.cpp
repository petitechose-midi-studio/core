#include "state/sequencer/SequencerGraphAssetCodec.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphAssetRecords.hpp"
#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;
using namespace graph_ops_internal;

constexpr uint32_t kStepGraphPresetMagic = 0x31504753;  // "SGP1"
constexpr uint8_t kStepGraphPresetVersion = 1;
constexpr uint8_t kStepGraphPresetKind = 1;
constexpr uint8_t kStepGraphPresetFlagRootContext = 1U << 0;
constexpr uint8_t kStepGraphPresetFlagRootValues = 1U << 1;
constexpr uint16_t kAssetRootNodeId = SequencerStepGraphPreset::ASSET_ROOT_NODE_ID;

struct StepGraphPresetHeader {
    uint32_t magic = kStepGraphPresetMagic;
    uint8_t version = kStepGraphPresetVersion;
    uint8_t kind = kStepGraphPresetKind;
    uint8_t headerSize = STEP_GRAPH_PRESET_HEADER_SIZE;
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

FLASHMEM StepSequencerChordMode sanitizeChordMode(uint8_t mode) {
    if (mode > static_cast<uint8_t>(StepSequencerChordMode::Local)) {
        return StepSequencerChordMode::Single;
    }
    return static_cast<StepSequencerChordMode>(mode);
}

FLASHMEM StepSequencerChordSpec sanitizeChordSpec(StepSequencerChordSpec spec) {
    spec.clamp();
    return spec;
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
        .chordColor = source.chordSpec.color,
        .chordVariant = source.chordSpec.variant,
        .chordSpread = source.chordSpec.spread,
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

class PresetWriter {
public:
    PresetWriter(uint8_t* out, uint16_t capacity)
        : out_(out), capacity_(capacity) {}

    bool writeU8(uint8_t value) {
        return append(&value, 1);
    }

    bool writeI8(int8_t value) {
        return writeU8(static_cast<uint8_t>(value));
    }

    bool writeU16(uint16_t value) {
        return writeU8(static_cast<uint8_t>(value & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    }

    bool writeU32(uint32_t value) {
        return writeU8(static_cast<uint8_t>(value & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 8U) & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 16U) & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 24U) & 0xFFU));
    }

    bool append(const void* data, uint16_t size) {
        if (!ok_) return false;
        if (data == nullptr && size > 0) {
            ok_ = false;
            return false;
        }
        if (offset_ > capacity_ || size > static_cast<uint16_t>(capacity_ - offset_)) {
            ok_ = false;
            return false;
        }
        if (size > 0) {
            std::memcpy(out_ + offset_, data, size);
            offset_ = static_cast<uint16_t>(offset_ + size);
        }
        return true;
    }

    bool ok() const { return ok_; }
    uint16_t size() const { return offset_; }

private:
    uint8_t* out_ = nullptr;
    uint16_t capacity_ = 0;
    uint16_t offset_ = 0;
    bool ok_ = true;
};

FLASHMEM bool readU8(const uint8_t* data, uint16_t size, uint16_t& offset, uint8_t& out) {
    if (data == nullptr || offset >= size) return false;
    out = data[offset++];
    return true;
}

FLASHMEM bool readI8(const uint8_t* data, uint16_t size, uint16_t& offset, int8_t& out) {
    uint8_t raw = 0;
    if (!readU8(data, size, offset, raw)) return false;
    out = static_cast<int8_t>(raw);
    return true;
}

FLASHMEM bool readU16(const uint8_t* data, uint16_t size, uint16_t& offset, uint16_t& out) {
    uint8_t lo = 0;
    uint8_t hi = 0;
    if (!readU8(data, size, offset, lo) || !readU8(data, size, offset, hi)) return false;
    out = static_cast<uint16_t>(lo | static_cast<uint16_t>(hi << 8U));
    return true;
}

FLASHMEM bool readU32(const uint8_t* data, uint16_t size, uint16_t& offset, uint32_t& out) {
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    uint8_t b3 = 0;
    if (!readU8(data, size, offset, b0) ||
        !readU8(data, size, offset, b1) ||
        !readU8(data, size, offset, b2) ||
        !readU8(data, size, offset, b3)) {
        return false;
    }
    out = static_cast<uint32_t>(b0) |
          (static_cast<uint32_t>(b1) << 8U) |
          (static_cast<uint32_t>(b2) << 16U) |
          (static_cast<uint32_t>(b3) << 24U);
    return true;
}

FLASHMEM bool writeHeader(PresetWriter& writer, const StepGraphPresetHeader& header) {
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

FLASHMEM bool readHeader(const uint8_t* data,
                         uint16_t size,
                         uint16_t& offset,
                         StepGraphPresetHeader& header) {
    return readU32(data, size, offset, header.magic) &&
           readU8(data, size, offset, header.version) &&
           readU8(data, size, offset, header.kind) &&
           readU8(data, size, offset, header.headerSize) &&
           readU8(data, size, offset, header.flags) &&
           readU8(data, size, offset, header.enabled) &&
           readU8(data, size, offset, header.note) &&
           readU8(data, size, offset, header.velocity) &&
           readU16(data, size, offset, header.gate) &&
           readI8(data, size, offset, header.nudge) &&
           readU8(data, size, offset, header.probability) &&
           readU16(data, size, offset, header.stepNodeCount) &&
           readU8(data, size, offset, header.sequenceCount) &&
           readU8(data, size, offset, header.cycleSetCount) &&
           readU16(data, size, offset, header.reserved0);
}

FLASHMEM bool appendSequenceRecord(PresetWriter& writer,
                                   const SequencerGraphSequenceRecord& record) {
    uint8_t bytes[SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE] = {};
    return encodeSequencerGraphSequenceRecord(
               record,
               bytes,
               SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE
           ) &&
           writer.append(bytes, SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE);
}

FLASHMEM bool appendStepNodeRecord(PresetWriter& writer,
                                   const SequencerGraphStepNodeRecord& record) {
    uint8_t bytes[SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE] = {};
    return encodeSequencerGraphStepNodeRecord(
               record,
               bytes,
               SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE
           ) &&
           writer.append(bytes, SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE);
}

FLASHMEM bool appendCycleSetRecord(PresetWriter& writer,
                                   const SequencerGraphCycleSetRecord& record) {
    uint8_t bytes[SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE] = {};
    return encodeSequencerGraphCycleSetRecord(
               record,
               bytes,
               SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE
           ) &&
           writer.append(bytes, SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE);
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
        const StepSequencerChordSpec chordSpec = sanitizeChordSpec({
            .voiceCount = record.chordVoiceCount,
            .color = record.chordColor,
            .variant = record.chordVariant,
            .spread = record.chordSpread,
            .strum = record.chordStrum,
            .velocityCurve = record.chordVelocityCurve,
        });
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
            .chordMode = sanitizeChordMode(record.chordMode),
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
        !linksValid(preset.graph)) {
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

    PresetWriter writer(out, capacity);
    if (!writeHeader(writer, header)) {
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
              .bytesWritten = writer.size(),
          }
        : SequencerGraphAssetEncodeResult{
              .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
              .bytesWritten = 0,
          };
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

    uint16_t offset = 0;
    StepGraphPresetHeader header{};
    if (!readHeader(data, size, offset, header) ||
        offset != STEP_GRAPH_PRESET_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.magic != kStepGraphPresetMagic ||
        header.kind != kStepGraphPresetKind ||
        header.headerSize != STEP_GRAPH_PRESET_HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.version != kStepGraphPresetVersion) {
        setReportStatus(report, SequencerGraphAssetStatus::UNSUPPORTED_VERSION);
        return false;
    }

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

    if (!decodeGraph(data, size, offset, header, out.graph) || offset != size) {
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
        fillReportCounts(report, out.graph);
    }
    return true;
}

}  // namespace core::state::sequencer

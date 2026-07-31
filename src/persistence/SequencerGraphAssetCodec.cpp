#include "persistence/SequencerGraphAssetCodec.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::sequencer_graph_asset_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace graph_record =
    core::persistence::sequencer_graph_record_codec;
namespace sequencer = core::state::sequencer;

using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;

using sequencer::SequencerGraphAssetReport;
using sequencer::SequencerGraphAssetStatus;
using sequencer::SequencerStepGraphPreset;

constexpr uint32_t kMagic = 0x31504753;  // "SGP1"
constexpr uint8_t kVersion =
    SequencerStepGraphPreset::CURRENT_FORMAT_VERSION;
constexpr uint8_t kKind = 1;
constexpr uint8_t kFlagRootContext = 1U << 0;
constexpr uint8_t kFlagRootValues = 1U << 1;
constexpr uint8_t kKnownFlags = kFlagRootContext | kFlagRootValues;

struct Header {
    uint32_t magic = kMagic;
    uint8_t version = kVersion;
    uint8_t kind = kKind;
    uint8_t headerSize = static_cast<uint8_t>(HEADER_SIZE);
    uint8_t flags = kFlagRootContext;
    uint8_t enabled = 0;
    uint8_t note = sequencer::SequencerState::DEFAULT_NOTE;
    uint8_t velocity = sequencer::SequencerState::DEFAULT_VELOCITY;
    uint16_t gate = sequencer::SequencerState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = sequencer::SequencerState::DEFAULT_PROBABILITY;
    uint16_t stepNodeCount = 0;
    uint8_t sequenceCount = 0;
    uint8_t cycleSetCount = 0;
    uint16_t reserved0 = 0;
};

struct Metadata {
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

static_assert(HEADER_SIZE <= UINT8_MAX);

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

FLASHMEM bool writeHeader(binary::Writer& writer, const Header& header) {
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

FLASHMEM bool readHeader(binary::Reader& reader, Header& header) {
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
    const Metadata& metadata
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
    Metadata& metadata
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

FLASHMEM bool headerShapeCanonical(const Header& header) {
    return (header.flags & static_cast<uint8_t>(~kKnownFlags)) == 0 &&
           header.reserved0 == 0 &&
           header.enabled <= 1U &&
           header.stepNodeCount > 0 &&
           header.stepNodeCount <= StepSequencerGraphLimits::MAX_STEP_NODES &&
           header.sequenceCount <= StepSequencerGraphLimits::MAX_SEQUENCES &&
           header.cycleSetCount <= StepSequencerGraphLimits::MAX_CYCLE_SETS;
}

FLASHMEM void applyHeader(
    const Header& header,
    SequencerStepGraphPreset& preset
) {
    preset.formatVersion = header.version;
    preset.rootContext = (header.flags & kFlagRootContext) != 0;
    preset.rootValuesValid = (header.flags & kFlagRootValues) != 0;
    preset.enabled = header.enabled != 0;
    preset.note = header.note;
    preset.velocity = header.velocity;
    preset.gate = header.gate;
    preset.nudge = header.nudge;
    preset.probability = header.probability;
}

FLASHMEM bool applyMetadata(
    const Metadata& metadata,
    SequencerStepGraphPreset& preset
) {
    if (metadata.scalePolicy >
            static_cast<uint8_t>(
                SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
            ) ||
        !fixedTextValid(metadata.technicalId, false) ||
        !fixedTextValid(metadata.semanticName, false) ||
        !sequencer::validStepGraphPresetTechnicalId(metadata.technicalId) ||
        !sequencer::validStepGraphPresetSemanticName(metadata.semanticName)) {
        return false;
    }

    preset.scalePolicy =
        static_cast<SequencerStepGraphPreset::ScalePolicy>(
            metadata.scalePolicy
        );
    preset.sourceScale = {
        .root = metadata.sourceScaleRoot,
        .type = static_cast<oc::note::sequencer::StepSequencerScaleType>(
            metadata.sourceScaleType
        ),
        .mode = static_cast<
            oc::note::sequencer::StepSequencerScaleConstraintMode>(
                metadata.sourceScaleMode
            ),
    };
    std::memcpy(
        preset.technicalId,
        metadata.technicalId,
        sizeof(preset.technicalId)
    );
    std::memcpy(
        preset.semanticName,
        metadata.semanticName,
        sizeof(preset.semanticName)
    );
    return sequencer::stepGraphPresetMetadataIsCanonical(preset);
}

FLASHMEM bool appendSequence(
    binary::Writer& writer,
    const oc::note::sequencer::StepSequencerSequence& sequence
) {
    uint8_t bytes[graph_record::SEQUENCE_RECORD_SIZE] = {};
    return graph_record::encodeSequence(
               sequence,
               bytes,
               graph_record::SEQUENCE_RECORD_SIZE
           ) &&
           writer.writeBytes(bytes, graph_record::SEQUENCE_RECORD_SIZE);
}

FLASHMEM bool appendStepNode(
    binary::Writer& writer,
    const oc::note::sequencer::StepSequencerStepNode& node
) {
    uint8_t bytes[graph_record::STEP_NODE_RECORD_SIZE] = {};
    return graph_record::encodeStepNode(
               node,
               bytes,
               graph_record::STEP_NODE_RECORD_SIZE
           ) &&
           writer.writeBytes(bytes, graph_record::STEP_NODE_RECORD_SIZE);
}

FLASHMEM bool appendCycleSet(
    binary::Writer& writer,
    const oc::note::sequencer::StepSequencerCycleStateSet& cycleSet
) {
    uint8_t bytes[graph_record::CYCLE_SET_RECORD_SIZE] = {};
    return graph_record::encodeCycleSet(
               cycleSet,
               bytes,
               graph_record::CYCLE_SET_RECORD_SIZE
           ) &&
           writer.writeBytes(bytes, graph_record::CYCLE_SET_RECORD_SIZE);
}

FLASHMEM bool decodeGraph(
    const uint8_t* data,
    uint16_t size,
    uint16_t& offset,
    const Header& header,
    StepSequencerGraph& graph
) {
    graph.reset();
    graph.enabled = true;
    graph.rootSequenceId = StepSequencerGraphLimits::INVALID_ID;
    graph.stepNodeCount = header.stepNodeCount;
    graph.sequenceCount = header.sequenceCount;
    graph.cycleSetCount = header.cycleSetCount;

    for (uint16_t i = 0; i < header.sequenceCount; ++i) {
        if (offset > size ||
            graph_record::SEQUENCE_RECORD_SIZE >
                static_cast<uint16_t>(size - offset) ||
            !graph_record::decodeSequence(
                data + offset,
                graph_record::SEQUENCE_RECORD_SIZE,
                graph.sequences[i]
            )) {
            return false;
        }
        offset = static_cast<uint16_t>(
            offset + graph_record::SEQUENCE_RECORD_SIZE
        );
    }

    for (uint16_t i = 0; i < header.stepNodeCount; ++i) {
        if (offset > size ||
            graph_record::STEP_NODE_RECORD_SIZE >
                static_cast<uint16_t>(size - offset) ||
            !graph_record::decodeStepNode(
                data + offset,
                graph_record::STEP_NODE_RECORD_SIZE,
                graph.stepNodes[i]
            )) {
            return false;
        }
        offset = static_cast<uint16_t>(
            offset + graph_record::STEP_NODE_RECORD_SIZE
        );
    }

    for (uint16_t i = 0; i < header.cycleSetCount; ++i) {
        if (offset > size ||
            graph_record::CYCLE_SET_RECORD_SIZE >
                static_cast<uint16_t>(size - offset) ||
            !graph_record::decodeCycleSet(
                data + offset,
                graph_record::CYCLE_SET_RECORD_SIZE,
                graph.cycleSets[i]
            )) {
            return false;
        }
        offset = static_cast<uint16_t>(
            offset + graph_record::CYCLE_SET_RECORD_SIZE
        );
    }

    return sequencer::stepGraphPresetGraphIsCanonical(graph);
}

FLASHMEM bool readAndValidatePrefix(
    const uint8_t* data,
    uint16_t size,
    binary::Reader& reader,
    Header& header,
    SequencerGraphAssetReport* report
) {
    if (data == nullptr || size < HEADER_SIZE) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_ARGUMENT);
        return false;
    }
    if (!readHeader(reader, header) ||
        reader.offset() != BASE_HEADER_SIZE ||
        header.magic != kMagic ||
        header.kind != kKind) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.version != kVersion) {
        setReportStatus(
            report,
            SequencerGraphAssetStatus::UNSUPPORTED_VERSION
        );
        return false;
    }
    if (header.headerSize != HEADER_SIZE ||
        !headerShapeCanonical(header)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }
    return true;
}

}  // namespace

FLASHMEM EncodeResult encode(
    const SequencerStepGraphPreset& preset,
    uint8_t* out,
    uint16_t capacity
) {
    if (!preset.valid ||
        out == nullptr ||
        !sequencer::stepGraphPresetMetadataIsCanonical(preset) ||
        !sequencer::stepGraphPresetGraphIsCanonical(preset.graph) ||
        !fixedTextValid(preset.technicalId, false) ||
        !fixedTextValid(preset.semanticName, false) ||
        !sequencer::validStepGraphPresetTechnicalId(preset.technicalId) ||
        !sequencer::validStepGraphPresetSemanticName(preset.semanticName)) {
        return {
            .status = SequencerGraphAssetStatus::INVALID_ARGUMENT,
            .bytesWritten = 0,
        };
    }

    Header header{};
    header.flags = 0;
    if (preset.rootContext) {
        header.flags = static_cast<uint8_t>(
            header.flags | kFlagRootContext
        );
    }
    if (preset.rootValuesValid) {
        header.flags = static_cast<uint8_t>(
            header.flags | kFlagRootValues
        );
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

    Metadata metadata{};
    metadata.scalePolicy = static_cast<uint8_t>(preset.scalePolicy);
    metadata.sourceScaleRoot = preset.sourceScale.root;
    metadata.sourceScaleType = static_cast<uint8_t>(preset.sourceScale.type);
    metadata.sourceScaleMode = static_cast<uint8_t>(preset.sourceScale.mode);
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
    if (!writeHeader(writer, header) ||
        !writeMetadata(writer, metadata)) {
        return {
            .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
            .bytesWritten = 0,
        };
    }

    for (uint16_t i = 0; i < preset.graph.sequenceCount; ++i) {
        if (!appendSequence(writer, preset.graph.sequences[i])) {
            return {
                .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
                .bytesWritten = 0,
            };
        }
    }
    for (uint16_t i = 0; i < preset.graph.stepNodeCount; ++i) {
        if (!appendStepNode(writer, preset.graph.stepNodes[i])) {
            return {
                .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
                .bytesWritten = 0,
            };
        }
    }
    for (uint16_t i = 0; i < preset.graph.cycleSetCount; ++i) {
        if (!appendCycleSet(writer, preset.graph.cycleSets[i])) {
            return {
                .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
                .bytesWritten = 0,
            };
        }
    }

    return writer.ok()
        ? EncodeResult{
              .status = SequencerGraphAssetStatus::OK,
              .bytesWritten = static_cast<uint16_t>(writer.offset()),
          }
        : EncodeResult{
              .status = SequencerGraphAssetStatus::BUFFER_TOO_SMALL,
              .bytesWritten = 0,
          };
}

FLASHMEM bool decodeMetadata(
    const uint8_t* data,
    uint16_t size,
    MetadataView& out,
    SequencerGraphAssetReport* report
) {
    out = {};
    if (report != nullptr) report->reset();

    binary::Reader reader(data, size);
    Header header{};
    if (!readAndValidatePrefix(data, size, reader, header, report)) {
        return false;
    }

    Metadata metadata{};
    SequencerStepGraphPreset candidate{};
    candidate.reset();
    applyHeader(header, candidate);
    if (!readMetadata(reader, metadata) ||
        reader.offset() != HEADER_SIZE ||
        !applyMetadata(metadata, candidate)) {
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }

    out.formatVersion = candidate.formatVersion;
    out.scalePolicy = candidate.scalePolicy;
    out.sourceScale = candidate.sourceScale;
    std::memcpy(
        out.technicalId,
        candidate.technicalId,
        sizeof(out.technicalId)
    );
    std::memcpy(
        out.semanticName,
        candidate.semanticName,
        sizeof(out.semanticName)
    );
    return true;
}

FLASHMEM bool decode(
    const uint8_t* data,
    uint16_t size,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report
) {
    if (report != nullptr) report->reset();
    out.reset();

    binary::Reader reader(data, size);
    Header header{};
    if (!readAndValidatePrefix(data, size, reader, header, report)) {
        return false;
    }

    applyHeader(header, out);
    Metadata metadata{};
    if (!readMetadata(reader, metadata) ||
        reader.offset() != HEADER_SIZE ||
        !applyMetadata(metadata, out)) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }

    uint16_t offset = static_cast<uint16_t>(reader.offset());
    if (!decodeGraph(data, size, offset, header, out.graph) ||
        offset != size) {
        out.reset();
        setReportStatus(report, SequencerGraphAssetStatus::INVALID_FORMAT);
        return false;
    }

    out.valid = true;
    if (report != nullptr) {
        if (out.rootValuesValid) {
            report->flags = static_cast<uint16_t>(
                report->flags |
                sequencer::SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES
            );
        }
        report->flags = static_cast<uint16_t>(
            report->flags |
            sequencer::SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD
        );
        fillReportCounts(report, out.graph);
    }
    return true;
}

}  // namespace core::persistence::sequencer_graph_asset_codec

#include "persistence/SequencerPersistenceEnvelope.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/SequencerPersistenceCodec.hpp"
#include "persistence/SequencerCcLanePersistenceCodec.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphAssetRecords.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::persistence::sequencer_codec {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_PITCH_CHROMATIC;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;

constexpr uint32_t kEnvelopeMagic = 0x53514534;  // "SQE4"
constexpr uint8_t kLegacyEnvelopeVersion = LEGACY_ENVELOPE_VERSION;
constexpr uint8_t kPitchPolicyEnvelopeVersion = PITCH_POLICY_ENVELOPE_VERSION;
constexpr uint8_t kCcLaneEnvelopeVersion = CC_LANE_ENVELOPE_VERSION;
constexpr uint8_t kLegacyCcLaneEnvelopeVersion =
    LEGACY_CC_LANE_ENVELOPE_VERSION;
constexpr uint16_t kEnvelopeHeaderSize = 12;
constexpr uint16_t kSectionHeaderSize = 10;
constexpr uint8_t kNoTrack = 0xFF;

enum class EnvelopeKind : uint8_t {
    Pattern = 1,
    ProjectSequencer = 2,
    Set = 3,
};

enum class SectionId : uint16_t {
    FlatPattern = 1,
    FlatProjectSequencer = 2,
    FlatSet = 3,
    GraphSequences = 16,
    GraphStepNodes = 17,
    GraphCycleSets = 18,
    CcLaneBank = 19,
};

struct EnvelopeHeader {
    uint32_t magic = kEnvelopeMagic;
    uint8_t version = kLegacyEnvelopeVersion;
    uint8_t kind = 0;
    uint16_t headerSize = kEnvelopeHeaderSize;
    uint16_t sectionCount = 0;
    uint16_t reserved0 = 0;
};

struct SectionHeader {
    uint16_t id = 0;
    uint8_t track = kNoTrack;
    uint8_t reserved0 = 0;
    uint16_t recordSize = 0;
    uint16_t count = 0;
    uint16_t byteSize = 0;
};

using SequenceRecord = core::state::sequencer::SequencerGraphSequenceRecord;
using StepNodeRecord = core::state::sequencer::SequencerGraphStepNodeRecord;
using CycleSetRecord = core::state::sequencer::SequencerGraphCycleSetRecord;
using GraphPtr = core::app::ExtmemUniquePtr<StepSequencerGraph>;
using CcLanePtr = state::sequencer::SequencerCcLaneBankPtr;

struct SectionView {
    const uint8_t* data = nullptr;
    uint16_t recordSize = 0;
    uint16_t count = 0;
    uint16_t byteSize = 0;
};

struct GraphSectionViews {
    SectionView sequences{};
    SectionView stepNodes{};
    SectionView cycleSets{};
    SectionView ccLaneBank{};
};

FLASHMEM bool assignSectionView(SectionView& target, const SectionView& source) {
    if (target.data != nullptr) return false;
    target = source;
    return true;
}

class EnvelopeWriter {
public:
    EnvelopeWriter(uint8_t* out,
                   uint32_t capacity,
                   EnvelopeKind kind,
                   uint8_t version)
        : out_(out), capacity_(capacity) {
        if (out_ == nullptr || capacity_ < kEnvelopeHeaderSize) {
            ok_ = false;
            return;
        }
        appendU32_(kEnvelopeMagic);
        appendU8_(version);
        appendU8_(static_cast<uint8_t>(kind));
        appendU16_(kEnvelopeHeaderSize);
        appendU16_(0);
        appendU16_(0);
    }

    bool reserveSection(SectionId id,
                        uint8_t track,
                        uint16_t recordSize,
                        uint16_t count,
                        uint16_t byteSize,
                        uint8_t*& destination) {
        destination = nullptr;
        if (!ok_) return false;
        if (!appendU16_(static_cast<uint16_t>(id)) ||
            !appendU8_(track) ||
            !appendU8_(0) ||
            !appendU16_(recordSize) ||
            !appendU16_(count) ||
            !appendU16_(byteSize)) {
            return false;
        }
        if (offset_ > capacity_ || byteSize > capacity_ - offset_) {
            ok_ = false;
            return false;
        }
        destination = out_ + offset_;
        offset_ += byteSize;
        ++sectionCount_;
        return true;
    }

    EnvelopeEncodeResult finish() {
        if (!ok_ || out_ == nullptr || offset_ < kEnvelopeHeaderSize) {
            return {};
        }

        writeU16At_(8, sectionCount_);
        return {.ok = true, .size = offset_};
    }

private:
    bool appendU8_(uint8_t value) {
        return appendRaw_(&value, 1);
    }

    bool appendU16_(uint16_t value) {
        const uint8_t bytes[2] = {
            static_cast<uint8_t>(value & 0xFFU),
            static_cast<uint8_t>((value >> 8U) & 0xFFU),
        };
        return appendRaw_(bytes, sizeof(bytes));
    }

    bool appendU32_(uint32_t value) {
        const uint8_t bytes[4] = {
            static_cast<uint8_t>(value & 0xFFU),
            static_cast<uint8_t>((value >> 8U) & 0xFFU),
            static_cast<uint8_t>((value >> 16U) & 0xFFU),
            static_cast<uint8_t>((value >> 24U) & 0xFFU),
        };
        return appendRaw_(bytes, sizeof(bytes));
    }

    void writeU16At_(uint32_t offset, uint16_t value) {
        if (out_ == nullptr || offset > capacity_) {
            ok_ = false;
            return;
        }
        if (capacity_ - offset < 2U) {
            ok_ = false;
            return;
        }
        out_[offset] = static_cast<uint8_t>(value & 0xFFU);
        out_[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    }

    bool appendRaw_(const void* data, uint32_t size) {
        if (size == 0) return true;
        if (data == nullptr) {
            ok_ = false;
            return false;
        }
        if (offset_ > capacity_ || size > capacity_ - offset_) {
            ok_ = false;
            return false;
        }
        std::memcpy(out_ + offset_, data, size);
        offset_ += size;
        return true;
    }

    uint8_t* out_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t offset_ = 0;
    uint16_t sectionCount_ = 0;
    bool ok_ = true;
};

FLASHMEM bool hasPersistableGraph(const StepSequencerGraph* graph) {
    if (graph == nullptr || !graph->enabled) return false;
    if (graph->sequenceCount > 1 || graph->cycleSetCount > 0) return true;

    const uint16_t count = static_cast<uint16_t>(
        std::min<uint16_t>(graph->stepNodeCount, graph->stepNodes.size())
    );
    for (uint16_t i = 0; i < count; ++i) {
        const auto& node = graph->stepNodes[i];
        if (node.flags != 0) return true;
        auto localVariation = node.localVariation;
        localVariation.clamp();
        if (localVariation.pitchSemitones != 0 ||
            localVariation.velocity != 0 ||
            localVariation.gatePercent != 0 ||
            localVariation.nudge != 0) {
            return true;
        }
    }
    return false;
}

FLASHMEM bool graphRequiresPitchPolicyEnvelope(const StepSequencerGraph* graph) {
    if (graph == nullptr || !graph->enabled) return false;
    const uint16_t count = static_cast<uint16_t>(
        std::min<uint16_t>(graph->stepNodeCount, graph->stepNodes.size())
    );
    for (uint16_t i = 0; i < count; ++i) {
        if (graph->stepNodes[i].has(STEP_NODE_PITCH_CHROMATIC)) return true;
    }
    return false;
}

FLASHMEM uint8_t envelopeVersionForGraph(const StepSequencerGraph* graph) {
    return graphRequiresPitchPolicyEnvelope(graph)
        ? kPitchPolicyEnvelopeVersion
        : kLegacyEnvelopeVersion;
}

FLASHMEM bool hasPersistableCcLanes(
    const state::sequencer::SequencerCcLaneBank* lanes
) {
    return lanes != nullptr &&
           state::sequencer::sequencerCcLaneCount(*lanes) > 0;
}

FLASHMEM uint8_t envelopeVersionForPattern(
    const StepSequencerGraph* graph,
    const state::sequencer::SequencerCcLaneBank* lanes
) {
    return hasPersistableCcLanes(lanes)
        ? kCcLaneEnvelopeVersion
        : envelopeVersionForGraph(graph);
}

FLASHMEM bool addCcLaneSection(
    EnvelopeWriter& writer,
    const state::sequencer::SequencerCcLaneBank* lanes,
    uint8_t track
) {
    if (!hasPersistableCcLanes(lanes)) return true;
    uint8_t* data = nullptr;
    if (!writer.reserveSection(
            SectionId::CcLaneBank,
            track,
            SEQUENCER_CC_LANE_BANK_RECORD_SIZE,
            1,
            SEQUENCER_CC_LANE_BANK_RECORD_SIZE,
            data
        )) {
        return false;
    }
    return encodeSequencerCcLaneBankRecord(
        *lanes,
        data,
        SEQUENCER_CC_LANE_BANK_RECORD_SIZE
    );
}

FLASHMEM bool addGraphSections(EnvelopeWriter& writer,
                               const StepSequencerGraph* graph,
                               uint8_t track) {
    if (!hasPersistableGraph(graph)) return true;

    const auto sequenceCount = static_cast<uint16_t>(
        std::min<uint16_t>(graph->sequenceCount, graph->sequences.size())
    );
    const auto nodeCount = static_cast<uint16_t>(
        std::min<uint16_t>(graph->stepNodeCount, graph->stepNodes.size())
    );
    const auto cycleSetCount = static_cast<uint16_t>(
        std::min<uint16_t>(graph->cycleSetCount, graph->cycleSets.size())
    );

    const uint16_t sequenceBytes = static_cast<uint16_t>(
        sequenceCount * state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE
    );
    const uint16_t nodeBytes = static_cast<uint16_t>(
        nodeCount * state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE
    );
    const uint16_t cycleSetBytes = static_cast<uint16_t>(
        cycleSetCount * state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE
    );

    uint8_t* sequenceData = nullptr;
    uint8_t* nodeData = nullptr;
    uint8_t* cycleSetData = nullptr;
    if (!writer.reserveSection(SectionId::GraphSequences,
                               track,
                               state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE,
                               sequenceCount,
                               sequenceBytes,
                               sequenceData) ||
        !writer.reserveSection(SectionId::GraphStepNodes,
                               track,
                               state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE,
                               nodeCount,
                               nodeBytes,
                               nodeData) ||
        !writer.reserveSection(SectionId::GraphCycleSets,
                               track,
                               state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE,
                               cycleSetCount,
                               cycleSetBytes,
                               cycleSetData)) {
        return false;
    }

    for (uint16_t i = 0; i < sequenceCount; ++i) {
        const auto& source = graph->sequences[i];
        const SequenceRecord record{
            .kind = static_cast<uint8_t>(source.kind),
            .firstStepNode = source.firstStepNode,
            .length = source.length,
            .offset = source.offset,
        };
        if (!state::sequencer::encodeSequencerGraphSequenceRecord(
                record,
                sequenceData +
                    i * state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE,
                state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE
            )) {
            return false;
        }
    }

    for (uint16_t i = 0; i < nodeCount; ++i) {
        const auto& source = graph->stepNodes[i];
        const StepNodeRecord record{
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
        if (!state::sequencer::encodeSequencerGraphStepNodeRecord(
                record,
                nodeData +
                    i * state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE,
                state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE
            )) {
            return false;
        }
    }

    for (uint16_t i = 0; i < cycleSetCount; ++i) {
        const auto& source = graph->cycleSets[i];
        const CycleSetRecord record{
            .firstStateNode = source.firstStateNode,
            .length = source.length,
            .offset = source.offset,
        };
        if (!state::sequencer::encodeSequencerGraphCycleSetRecord(
                record,
                cycleSetData +
                    i * state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE,
                state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE
            )) {
            return false;
        }
    }

    return true;
}

FLASHMEM bool readU8(const uint8_t* data, uint32_t size, uint32_t& offset, uint8_t& out) {
    if (data == nullptr || offset >= size) return false;
    out = data[offset++];
    return true;
}

FLASHMEM bool readU16(const uint8_t* data, uint32_t size, uint32_t& offset, uint16_t& out) {
    uint8_t lo = 0;
    uint8_t hi = 0;
    if (!readU8(data, size, offset, lo) || !readU8(data, size, offset, hi)) return false;
    out = static_cast<uint16_t>(lo | static_cast<uint16_t>(hi << 8U));
    return true;
}

FLASHMEM bool readU32(const uint8_t* data, uint32_t size, uint32_t& offset, uint32_t& out) {
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

FLASHMEM bool readEnvelopeHeader(const uint8_t* data,
                                 uint32_t size,
                                 EnvelopeHeader& out) {
    uint32_t offset = 0;
    return readU32(data, size, offset, out.magic) &&
           readU8(data, size, offset, out.version) &&
           readU8(data, size, offset, out.kind) &&
           readU16(data, size, offset, out.headerSize) &&
           readU16(data, size, offset, out.sectionCount) &&
           readU16(data, size, offset, out.reserved0) &&
           offset == kEnvelopeHeaderSize;
}

FLASHMEM bool isHeaderValid(const EnvelopeHeader& header, EnvelopeKind kind) {
    return header.magic == kEnvelopeMagic &&
           (header.version == kLegacyEnvelopeVersion ||
            header.version == kPitchPolicyEnvelopeVersion ||
            header.version == kLegacyCcLaneEnvelopeVersion ||
            header.version == kCcLaneEnvelopeVersion) &&
           header.kind == static_cast<uint8_t>(kind) &&
           header.headerSize == kEnvelopeHeaderSize &&
           header.reserved0 == 0;
}

FLASHMEM bool readSectionHeader(const uint8_t* data,
                                uint32_t size,
                                uint32_t& offset,
                                 SectionHeader& out) {
    if (data == nullptr) return false;
    if (offset > size || kSectionHeaderSize > size - offset) {
        return false;
    }
    if (!readU16(data, size, offset, out.id) ||
        !readU8(data, size, offset, out.track) ||
        !readU8(data, size, offset, out.reserved0) ||
        !readU16(data, size, offset, out.recordSize) ||
        !readU16(data, size, offset, out.count) ||
        !readU16(data, size, offset, out.byteSize)) {
        return false;
    }
    if (out.reserved0 != 0) return false;
    if (out.byteSize > size - offset) {
        return false;
    }
    return true;
}

FLASHMEM bool findSections(const uint8_t* data,
                           uint32_t size,
                           EnvelopeKind kind,
                           SectionId flatId,
                           SectionView& flat,
                           std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>* graphViews,
                           uint8_t& envelopeVersion) {
    if (data == nullptr || size < kEnvelopeHeaderSize) return false;

    EnvelopeHeader header{};
    if (!readEnvelopeHeader(data, size, header)) return false;
    if (!isHeaderValid(header, kind)) return false;
    envelopeVersion = header.version;

    uint32_t offset = header.headerSize;
    for (uint16_t i = 0; i < header.sectionCount; ++i) {
        SectionHeader section{};
        if (!readSectionHeader(data, size, offset, section)) return false;

        SectionView view{
            .data = data + offset,
            .recordSize = section.recordSize,
            .count = section.count,
            .byteSize = section.byteSize,
        };

        const auto id = static_cast<SectionId>(section.id);
        if (id == flatId && section.track == kNoTrack) {
            if (!assignSectionView(flat, view)) return false;
        } else if (graphViews != nullptr && section.track < graphViews->size()) {
            auto& graph = (*graphViews)[section.track];
            switch (id) {
                case SectionId::GraphSequences:
                    if (!assignSectionView(graph.sequences, view)) return false;
                    break;
                case SectionId::GraphStepNodes:
                    if (!assignSectionView(graph.stepNodes, view)) return false;
                    break;
                case SectionId::GraphCycleSets:
                    if (!assignSectionView(graph.cycleSets, view)) return false;
                    break;
                case SectionId::CcLaneBank:
                    if (header.version < kLegacyCcLaneEnvelopeVersion ||
                        !assignSectionView(graph.ccLaneBank, view)) {
                        return false;
                    }
                    break;
                default:
                    return false;
            }
        } else {
            // Unknown or misplaced sections cannot be preserved by this
            // decoder. Refuse the envelope so the containing project becomes
            // explicitly partial/non-overwrite-safe instead of losing bytes.
            return false;
        }

        offset += section.byteSize;
    }

    return flat.data != nullptr && offset == size;
}

FLASHMEM bool sectionHasExactRecordShape(const SectionView& section, uint16_t recordSize) {
    return section.recordSize == recordSize &&
           section.byteSize == static_cast<uint16_t>(section.count * recordSize);
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

FLASHMEM bool linkSequenceValid(const StepSequencerGraph& graph, uint16_t id) {
    return graph.sequence(id) != nullptr;
}

FLASHMEM bool linkCycleSetValid(const StepSequencerGraph& graph, uint16_t id) {
    return graph.cycleSet(id) != nullptr;
}

FLASHMEM bool graphIsPersistableAfterSanitize(const StepSequencerGraph& graph) {
    return hasPersistableGraph(&graph);
}

FLASHMEM bool decodeGraphSections(
    const GraphSectionViews& sections,
    uint8_t envelopeVersion,
    GraphPtr& out
) {
    out.reset();
    const bool hasAnyGraphSection =
        sections.sequences.data != nullptr ||
        sections.stepNodes.data != nullptr ||
        sections.cycleSets.data != nullptr;
    if (!hasAnyGraphSection) return true;

    if (sections.sequences.data == nullptr || sections.stepNodes.data == nullptr) {
        return false;
    }
    if (!sectionHasExactRecordShape(
            sections.sequences,
            state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE
        ) ||
        !sectionHasExactRecordShape(
            sections.stepNodes,
            state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE
        )) {
        return false;
    }
    if (sections.cycleSets.data != nullptr &&
        !sectionHasExactRecordShape(
            sections.cycleSets,
            state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE
        )) {
        return false;
    }

    if (sections.sequences.count == 0 ||
        sections.sequences.count > StepSequencerGraphLimits::MAX_SEQUENCES ||
        sections.stepNodes.count < state::sequencer::SequencerPatternState::MAX_STEPS ||
        sections.stepNodes.count > StepSequencerGraphLimits::MAX_STEP_NODES ||
        sections.cycleSets.count > StepSequencerGraphLimits::MAX_CYCLE_SETS) {
        return false;
    }

    auto graph = core::app::makeExtmemUnique<StepSequencerGraph>();
    if (!graph) return false;
    graph->enabled = true;
    graph->rootSequenceId = 0;
    graph->sequenceCount = static_cast<uint8_t>(sections.sequences.count);
    graph->stepNodeCount = sections.stepNodes.count;
    graph->cycleSetCount = static_cast<uint8_t>(sections.cycleSets.count);

    for (uint16_t i = 0; i < sections.sequences.count; ++i) {
        SequenceRecord record{};
        if (!state::sequencer::decodeSequencerGraphSequenceRecord(
                sections.sequences.data +
                    i * state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE,
                state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE,
                record
            )) {
            return false;
        }
        graph->sequences[i] = StepSequencerSequence{
            .kind = static_cast<StepSequencerSequenceKind>(record.kind),
            .firstStepNode = record.firstStepNode,
            .length = record.length,
            .offset = record.offset,
        };
    }

    for (uint16_t i = 0; i < sections.stepNodes.count; ++i) {
        StepNodeRecord record{};
        if (!state::sequencer::decodeSequencerGraphStepNodeRecord(
                sections.stepNodes.data +
                    i * state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE,
                state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE,
                record
            )) {
            return false;
        }
        const StepSequencerChordSpec chordSpec = sanitizeChordSpec({
            .voiceCount = record.chordVoiceCount,
            .color = record.chordColor,
            .variant = record.chordVariant,
            .spread = record.chordSpread,
            .strum = record.chordStrum,
            .velocityCurve = record.chordVelocityCurve,
        });
        graph->stepNodes[i] = StepSequencerStepNode{
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
        if (envelopeVersion < kPitchPolicyEnvelopeVersion &&
            graph->stepNodes[i].has(STEP_NODE_PITCH_CHROMATIC)) {
            // A legacy envelope claiming the new semantic bit is never safe to
            // interpret or overwrite. Current writers always upgrade to v4.
            return false;
        }
        graph->stepNodes[i].localVariation.clamp();
    }

    for (uint16_t i = 0; i < sections.cycleSets.count; ++i) {
        CycleSetRecord record{};
        if (!state::sequencer::decodeSequencerGraphCycleSetRecord(
                sections.cycleSets.data +
                    i * state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE,
                state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE,
                record
            )) {
            return false;
        }
        graph->cycleSets[i] = StepSequencerCycleStateSet{
            .firstStateNode = record.firstStateNode,
            .length = record.length,
            .offset = record.offset,
        };
    }

    const auto* root = graph->sequence(graph->rootSequenceId);
    if (root == nullptr ||
        root->kind != StepSequencerSequenceKind::RootPattern ||
        root->firstStepNode != 0 ||
        root->length != state::sequencer::SequencerPatternState::MAX_STEPS) {
        return false;
    }

    for (uint16_t i = 0; i < graph->stepNodeCount; ++i) {
        const auto& node = graph->stepNodes[i];
        if ((node.flags & STEP_NODE_CHILD_SEQUENCE) != 0 &&
            !linkSequenceValid(*graph, node.childSequenceId)) {
            return false;
        }
        if ((node.flags & STEP_NODE_CYCLE_SET) != 0 &&
            !linkCycleSetValid(*graph, node.cycleSetId)) {
            return false;
        }
    }

    if (!graphIsPersistableAfterSanitize(*graph)) {
        return false;
    }

    out = std::move(graph);
    return true;
}

FLASHMEM bool decodeCcLaneSection(
    const GraphSectionViews& sections,
    uint8_t envelopeVersion,
    CcLanePtr& out
) {
    out.reset();
    if (sections.ccLaneBank.data == nullptr) return true;
    const bool supportedRecord = sectionHasExactRecordShape(
        sections.ccLaneBank,
        SEQUENCER_CC_LANE_BANK_RECORD_SIZE
    ) || sectionHasExactRecordShape(
        sections.ccLaneBank,
        LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE
    ) || sectionHasExactRecordShape(
        sections.ccLaneBank,
        LEGACY_SEQUENCER_CC_LANE_BANK_RECORD_SIZE
    );
    if (envelopeVersion < kLegacyCcLaneEnvelopeVersion ||
        sections.ccLaneBank.count != 1 ||
        !supportedRecord) {
        return false;
    }

    state::sequencer::SequencerCcLaneBank decoded{};
    if (!decodeSequencerCcLaneBankRecord(
            sections.ccLaneBank.data,
            sections.ccLaneBank.byteSize,
            decoded
        )) {
        return false;
    }
    if (state::sequencer::sequencerCcLaneCount(decoded) == 0) return true;
    out = core::app::makeExtmemUnique<state::sequencer::SequencerCcLaneBank>(decoded);
    return static_cast<bool>(out);
}

FLASHMEM void installDecodedGraph(state::sequencer::SequencerPatternState& target,
                                  GraphPtr graph) {
    if (!graph) {
        state::sequencer::clearGraph(target);
        return;
    }
    target.graph = std::move(graph);
    target.bumpGraphRevision();
}

FLASHMEM uint16_t readU16At(const SectionView& flat, uint16_t offset) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(flat.data[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(flat.data[offset + 1U]) << 8U)
    );
}

FLASHMEM uint8_t projectActiveTrack(const SectionView& flat) {
    const uint8_t requested =
        state::sequencer::SequencerTrackBankState::clampTrackIndex(flat.data[0]);
    return state::sequencer::SequencerTrackBankState::sanitizeActiveTrack(
        readU16At(flat, 1),
        requested
    );
}

FLASHMEM uint8_t setActiveTrack(const SectionView& flat) {
    const uint8_t trackCount = static_cast<uint8_t>(std::min<uint16_t>(
        flat.data[0] == 0 ? 1 : flat.data[0],
        state::sequencer::SequencerTrackBankState::TRACK_COUNT
    ));
    const uint8_t requested =
        std::min<uint8_t>(flat.data[1], static_cast<uint8_t>(trackCount - 1U));
    return state::sequencer::SequencerTrackBankState::sanitizeActiveTrack(
        readU16At(flat, 2),
        requested
    );
}

FLASHMEM bool decodeTrackGraphs(
    const std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>& sections,
    uint8_t envelopeVersion,
    std::array<GraphPtr, PERSISTED_TRACK_COUNT>& graphs
) {
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!decodeGraphSections(sections[i], envelopeVersion, graphs[i])) return false;
    }
    return true;
}

FLASHMEM bool decodeTrackCcLanes(
    const std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>& sections,
    uint8_t envelopeVersion,
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT>& lanes
) {
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!decodeCcLaneSection(sections[i], envelopeVersion, lanes[i])) return false;
    }
    return true;
}

FLASHMEM bool cloneActiveGraph(const std::array<GraphPtr, PERSISTED_TRACK_COUNT>& graphs,
                               uint8_t activeTrack,
                               GraphPtr& out) {
    out.reset();
    if (activeTrack >= graphs.size() || !graphs[activeTrack]) return true;
    out = core::app::makeExtmemUnique<StepSequencerGraph>(*graphs[activeTrack]);
    return static_cast<bool>(out);
}

FLASHMEM bool cloneActiveCcLanes(
    const std::array<CcLanePtr, PERSISTED_TRACK_COUNT>& lanes,
    uint8_t activeTrack,
    CcLanePtr& out
) {
    out.reset();
    if (activeTrack >= lanes.size() || !lanes[activeTrack]) return true;
    return state::sequencer::cloneSequencerCcLaneBank(
        out,
        lanes[activeTrack].get()
    );
}

FLASHMEM void installTrackGraphs(
    std::array<GraphPtr, PERSISTED_TRACK_COUNT>& graphs,
    GraphPtr activeGraph,
    state::sequencer::SequencerTrackBankState& trackBank,
    state::sequencer::SequencerState& active
) {
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        installDecodedGraph(trackBank.track(i), std::move(graphs[i]));
    }
    installDecodedGraph(active.pattern, std::move(activeGraph));
}

FLASHMEM void installTrackCcLanes(
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT>& lanes,
    CcLanePtr activeLanes,
    state::sequencer::SequencerTrackBankState& trackBank,
    state::sequencer::SequencerState& active
) {
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        state::sequencer::installSequencerCcLaneBank(
            trackBank.track(i),
            std::move(lanes[i])
        );
    }
    state::sequencer::installSequencerCcLaneBank(
        active.pattern,
        std::move(activeLanes)
    );
}

FLASHMEM const state::sequencer::SequencerPatternState& sourceTrack(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t index
) {
    const uint8_t activeTrack =
        state::sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    return (index == activeTrack) ? active.pattern : trackBank.track(index);
}

}  // namespace

FLASHMEM EnvelopeEncodeResult fillPatternEnvelope(
    const state::sequencer::SequencerPatternState& source,
    uint8_t* out,
    uint32_t capacity
) {
    const auto* graph = state::sequencer::graphView(source);
    const auto* lanes = state::sequencer::sequencerCcLaneView(source);
    EnvelopeWriter writer(
        out,
        capacity,
        EnvelopeKind::Pattern,
        envelopeVersionForPattern(graph, lanes)
    );
    uint8_t* flat = nullptr;
    if (!writer.reserveSection(SectionId::FlatPattern,
                               kNoTrack,
                               PATTERN_PAYLOAD_SIZE,
                               1,
                               PATTERN_PAYLOAD_SIZE,
                               flat) ||
        !fillPatternPayload(source, flat, PATTERN_PAYLOAD_SIZE)) {
        return {};
    }
    if (!addGraphSections(writer, graph, 0) ||
        !addCcLaneSection(writer, lanes, 0)) {
        return {};
    }
    return writer.finish();
}

FLASHMEM bool applyPatternEnvelope(const uint8_t* data,
                                   uint32_t size,
                                   state::sequencer::SequencerPatternState& target) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> graphs{};
    uint8_t envelopeVersion = 0;
    if (!findSections(
            data,
            size,
            EnvelopeKind::Pattern,
            SectionId::FlatPattern,
            flat,
            &graphs,
            envelopeVersion
        )) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, PATTERN_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    GraphPtr graph;
    CcLanePtr lanes;
    if (!decodeGraphSections(graphs[0], envelopeVersion, graph) ||
        !decodeCcLaneSection(graphs[0], envelopeVersion, lanes)) {
        return false;
    }
    if (!applyPatternPayload(flat.data, flat.byteSize, target)) return false;
    installDecodedGraph(target, std::move(graph));
    state::sequencer::installSequencerCcLaneBank(target, std::move(lanes));
    return true;
}

FLASHMEM EnvelopeEncodeResult fillProjectSequencerEnvelope(
    const ProjectSequencerSnapshotEncodeSource& source,
    uint8_t* out,
    uint32_t capacity
) {
    if (source.flat == nullptr) return {};
    uint8_t envelopeVersion = kLegacyEnvelopeVersion;
    for (const auto* lanes : source.ccLanes) {
        if (hasPersistableCcLanes(lanes)) {
            envelopeVersion = kCcLaneEnvelopeVersion;
            break;
        }
    }
    for (const auto* graph : source.graphs) {
        if (envelopeVersion == kCcLaneEnvelopeVersion) break;
        if (graphRequiresPitchPolicyEnvelope(graph)) {
            envelopeVersion = kPitchPolicyEnvelopeVersion;
            break;
        }
    }
    EnvelopeWriter writer(
        out,
        capacity,
        EnvelopeKind::ProjectSequencer,
        envelopeVersion
    );
    uint8_t* flat = nullptr;
    if (!writer.reserveSection(SectionId::FlatProjectSequencer,
                               kNoTrack,
                               PROJECT_SEQUENCER_PAYLOAD_SIZE,
                               1,
                               PROJECT_SEQUENCER_PAYLOAD_SIZE,
                               flat) ||
        !fillProjectSequencerPayload(
            *source.flat,
            source.focusedStep,
            source.activeStepProperty,
            flat,
            PROJECT_SEQUENCER_PAYLOAD_SIZE
        )) {
        return {};
    }
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!addGraphSections(writer, source.graphs[i], i) ||
            !addCcLaneSection(writer, source.ccLanes[i], i)) {
            return {};
        }
    }
    return writer.finish();
}

FLASHMEM bool applyProjectSequencerEnvelope(const uint8_t* data,
                                            uint32_t size,
                                            state::sequencer::SequencerTrackBankState& trackBank,
                                            state::sequencer::SequencerState& active) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> graphs{};
    uint8_t envelopeVersion = 0;
    if (!findSections(data,
                      size,
                      EnvelopeKind::ProjectSequencer,
                      SectionId::FlatProjectSequencer,
                      flat,
                      &graphs,
                      envelopeVersion)) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, PROJECT_SEQUENCER_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    std::array<GraphPtr, PERSISTED_TRACK_COUNT> decodedGraphs{};
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT> decodedLanes{};
    GraphPtr activeGraph;
    CcLanePtr activeLanes;
    const uint8_t activeTrack = projectActiveTrack(flat);
    if (!decodeTrackGraphs(graphs, envelopeVersion, decodedGraphs) ||
        !decodeTrackCcLanes(graphs, envelopeVersion, decodedLanes) ||
        !cloneActiveGraph(decodedGraphs, activeTrack, activeGraph) ||
        !cloneActiveCcLanes(decodedLanes, activeTrack, activeLanes)) {
        return false;
    }
    if (!applyProjectSequencerPayload(flat.data, flat.byteSize, trackBank, active)) {
        return false;
    }
    installTrackGraphs(decodedGraphs, std::move(activeGraph), trackBank, active);
    installTrackCcLanes(decodedLanes, std::move(activeLanes), trackBank, active);
    return true;
}

FLASHMEM EnvelopeEncodeResult fillSetEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint32_t capacity
) {
    uint8_t envelopeVersion = kLegacyEnvelopeVersion;
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (hasPersistableCcLanes(
                state::sequencer::sequencerCcLaneView(sourceTrack(trackBank, active, i))
            )) {
            envelopeVersion = kCcLaneEnvelopeVersion;
            break;
        }
    }
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (envelopeVersion == kCcLaneEnvelopeVersion) break;
        if (graphRequiresPitchPolicyEnvelope(
                state::sequencer::graphView(sourceTrack(trackBank, active, i))
            )) {
            envelopeVersion = kPitchPolicyEnvelopeVersion;
            break;
        }
    }
    EnvelopeWriter writer(out, capacity, EnvelopeKind::Set, envelopeVersion);
    uint8_t* flat = nullptr;
    if (!writer.reserveSection(SectionId::FlatSet,
                               kNoTrack,
                               SET_PAYLOAD_SIZE,
                               1,
                               SET_PAYLOAD_SIZE,
                               flat) ||
        !fillSetPayload(trackBank, active, flat, SET_PAYLOAD_SIZE)) {
        return {};
    }
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        const auto& track = sourceTrack(trackBank, active, i);
        if (!addGraphSections(
                writer,
                state::sequencer::graphView(track),
                i
            ) ||
            !addCcLaneSection(
                writer,
                state::sequencer::sequencerCcLaneView(track),
                i
            )) {
            return {};
        }
    }
    return writer.finish();
}

FLASHMEM bool applySetEnvelope(const uint8_t* data,
                               uint32_t size,
                               state::sequencer::SequencerTrackBankState& trackBank,
                               state::sequencer::SequencerState& active) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> graphs{};
    uint8_t envelopeVersion = 0;
    if (!findSections(
            data,
            size,
            EnvelopeKind::Set,
            SectionId::FlatSet,
            flat,
            &graphs,
            envelopeVersion
        )) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, SET_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    std::array<GraphPtr, PERSISTED_TRACK_COUNT> decodedGraphs{};
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT> decodedLanes{};
    GraphPtr activeGraph;
    CcLanePtr activeLanes;
    const uint8_t activeTrack = setActiveTrack(flat);
    if (!decodeTrackGraphs(graphs, envelopeVersion, decodedGraphs) ||
        !decodeTrackCcLanes(graphs, envelopeVersion, decodedLanes) ||
        !cloneActiveGraph(decodedGraphs, activeTrack, activeGraph) ||
        !cloneActiveCcLanes(decodedLanes, activeTrack, activeLanes)) {
        return false;
    }
    if (!applySetPayload(flat.data, flat.byteSize, trackBank, active)) {
        return false;
    }
    installTrackGraphs(decodedGraphs, std::move(activeGraph), trackBank, active);
    installTrackCcLanes(decodedLanes, std::move(activeLanes), trackBank, active);
    return true;
}

}  // namespace core::persistence::sequencer_codec

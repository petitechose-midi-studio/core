#include "persistence/SequencerPersistenceEnvelope.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/SequencerCcLanePersistenceCodec.hpp"
#include "persistence/SequencerPersistenceCodec.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphAssetRecords.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::persistence::sequencer_codec {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_PITCH_CHROMATIC;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;
namespace binary = core::persistence::binary_codec;

constexpr uint32_t kEnvelopeMagic = 0x53514534;  // "SQE4"
constexpr uint8_t kEnvelopeVersion = ENVELOPE_VERSION;
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
    PatternRegion = 20,
};

struct EnvelopeHeader {
    uint32_t magic = kEnvelopeMagic;
    uint8_t version = kEnvelopeVersion;
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
    SectionView patternRegion{};
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
        : writer_(out, capacity) {
        ok_ = capacity >= kEnvelopeHeaderSize &&
              writer_.writeU32(kEnvelopeMagic) &&
              writer_.writeU8(version) &&
              writer_.writeU8(static_cast<uint8_t>(kind)) &&
              writer_.writeU16(kEnvelopeHeaderSize) &&
              writer_.writeU16(0) &&
              writer_.writeU16(0);
    }

    bool reserveSection(SectionId id,
                        uint8_t track,
                        uint16_t recordSize,
                        uint16_t count,
                        uint16_t byteSize,
                        uint8_t*& destination) {
        destination = nullptr;
        if (!ok_ || !writer_.ok()) return false;
        if (!writer_.writeU16(static_cast<uint16_t>(id)) ||
            !writer_.writeU8(track) ||
            !writer_.writeU8(0) ||
            !writer_.writeU16(recordSize) ||
            !writer_.writeU16(count) ||
            !writer_.writeU16(byteSize)) {
            return false;
        }
        if (!writer_.reserveBytes(byteSize, destination)) return false;
        ++sectionCount_;
        return true;
    }

    EnvelopeEncodeResult finish() {
        if (!ok_ || !writer_.ok() || writer_.offset() < kEnvelopeHeaderSize ||
            !writer_.patchU16(8, sectionCount_)) {
            return {};
        }
        return {.ok = true, .size = writer_.offset()};
    }

private:
    binary::Writer writer_;
    uint16_t sectionCount_ = 0;
    bool ok_ = false;
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

FLASHMEM bool hasPersistableCcLanes(
    const state::sequencer::SequencerCcLaneBank* lanes
) {
    return lanes != nullptr &&
           state::sequencer::sequencerCcLaneCount(*lanes) > 0;
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

FLASHMEM bool addPatternRegionSection(
    EnvelopeWriter& writer,
    const state::sequencer::SequencerPatternPlaybackRegion& region,
    uint8_t track
) {
    if (!region.isValid()) return false;

    uint8_t* data = nullptr;
    if (!writer.reserveSection(
            SectionId::PatternRegion,
            track,
            PATTERN_REGION_RECORD_SIZE,
            1,
            PATTERN_REGION_RECORD_SIZE,
            data
        )) {
        return false;
    }
    data[0] = region.playStart;
    data[1] = region.loopStart;
    data[2] = region.loopEnd;
    return true;
}

FLASHMEM state::sequencer::SequencerPatternPlaybackRegion snapshotPlaybackRegion(
    const state::sequencer::SequencerPatternSnapshot& snapshot
) {
    return {
        snapshot.length,
        snapshot.playStart,
        snapshot.loopStart,
        snapshot.loopEnd,
    };
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
            .chordHarmonyData = source.chordSpec.harmonyData,
            .chordVoicingData = source.chordSpec.voicingData,
            .chordInversionData = source.chordSpec.inversionData,
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

FLASHMEM bool readEnvelopeHeader(binary::Reader& reader, EnvelopeHeader& out) {
    return reader.readU32(out.magic) &&
           reader.readU8(out.version) &&
           reader.readU8(out.kind) &&
           reader.readU16(out.headerSize) &&
           reader.readU16(out.sectionCount) &&
           reader.readU16(out.reserved0) &&
           reader.offset() == kEnvelopeHeaderSize;
}

FLASHMEM bool isHeaderValid(const EnvelopeHeader& header, EnvelopeKind kind) {
    return header.magic == kEnvelopeMagic &&
           header.version == kEnvelopeVersion &&
           header.kind == static_cast<uint8_t>(kind) &&
           header.headerSize == kEnvelopeHeaderSize &&
           header.reserved0 == 0;
}

FLASHMEM bool readSectionHeader(binary::Reader& reader, SectionHeader& out) {
    if (reader.remaining() < kSectionHeaderSize ||
        !reader.readU16(out.id) ||
        !reader.readU8(out.track) ||
        !reader.readU8(out.reserved0) ||
        !reader.readU16(out.recordSize) ||
        !reader.readU16(out.count) ||
        !reader.readU16(out.byteSize)) {
        return false;
    }
    if (out.reserved0 != 0) return false;
    return out.byteSize <= reader.remaining();
}

FLASHMEM bool findSections(const uint8_t* data,
                           uint32_t size,
                           EnvelopeKind kind,
                           SectionId flatId,
                           SectionView& flat,
                           std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>* graphViews) {
    if (data == nullptr || size < kEnvelopeHeaderSize) return false;

    binary::Reader reader(data, size);
    EnvelopeHeader header{};
    if (!readEnvelopeHeader(reader, header)) return false;
    if (!isHeaderValid(header, kind)) return false;
    for (uint16_t i = 0; i < header.sectionCount; ++i) {
        SectionHeader section{};
        if (!readSectionHeader(reader, section)) return false;

        SectionView view{
            .data = reader.current(),
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
                    if (!assignSectionView(graph.ccLaneBank, view)) {
                        return false;
                    }
                    break;
                case SectionId::PatternRegion:
                    if (!assignSectionView(graph.patternRegion, view)) {
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

        if (!reader.skip(section.byteSize)) return false;
    }

    return flat.data != nullptr && reader.remaining() == 0;
}

FLASHMEM bool sectionHasExactRecordShape(const SectionView& section, uint16_t recordSize) {
    return section.recordSize == recordSize &&
           section.byteSize == static_cast<uint16_t>(section.count * recordSize);
}

using PatternRegionArray = std::array<
    state::sequencer::SequencerPatternPlaybackRegion,
    PERSISTED_TRACK_COUNT>;

FLASHMEM bool flatPatternContentLength(
    const SectionView& flat,
    EnvelopeKind kind,
    uint8_t track,
    uint8_t& out
) {
    uint32_t offset = 0;
    switch (kind) {
        case EnvelopeKind::Pattern:
            if (track != 0U) return false;
            break;
        case EnvelopeKind::ProjectSequencer:
            offset = 9U + static_cast<uint32_t>(track) *
                PROJECT_SEQUENCER_TRACK_PAYLOAD_SIZE;
            break;
        case EnvelopeKind::Set:
            offset = 10U + static_cast<uint32_t>(track) * PATTERN_PAYLOAD_SIZE;
            break;
    }
    if (flat.data == nullptr || offset >= flat.byteSize) return false;
    out = flat.data[offset];
    return true;
}

FLASHMEM bool decodePatternRegions(
    const SectionView& flat,
    const std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>& sections,
    EnvelopeKind kind,
    PatternRegionArray& out
) {
    const uint8_t ownerCount = kind == EnvelopeKind::Pattern
        ? 1U
        : PERSISTED_TRACK_COUNT;
    for (uint8_t track = 0; track < PERSISTED_TRACK_COUNT; ++track) {
        const auto& section = sections[track].patternRegion;
        if (track >= ownerCount) {
            if (section.data != nullptr) return false;
            continue;
        }

        uint8_t contentLength = 0;
        if (!flatPatternContentLength(flat, kind, track, contentLength)) return false;
        if (section.data == nullptr || section.count != 1U ||
            !sectionHasExactRecordShape(section, PATTERN_REGION_RECORD_SIZE)) {
            return false;
        }
        const state::sequencer::SequencerPatternPlaybackRegion region{
            contentLength,
            section.data[0],
            section.data[1],
            section.data[2],
        };
        if (!region.isValid()) return false;
        out[track] = region;
    }
    return true;
}

FLASHMEM void installPatternRegion(
    state::sequencer::SequencerPatternState& target,
    const state::sequencer::SequencerPatternPlaybackRegion& region
) {
    (void)state::sequencer::setPatternPlaybackRegion(target, region);
}

FLASHMEM void installTrackPatternRegions(
    const PatternRegionArray& regions,
    uint8_t activeTrack,
    state::sequencer::SequencerTrackBankState& trackBank,
    state::sequencer::SequencerState& active
) {
    for (uint8_t track = 0; track < PERSISTED_TRACK_COUNT; ++track) {
        installPatternRegion(trackBank.track(track), regions[track]);
    }
    installPatternRegion(active.pattern, regions[activeTrack]);
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
        StepSequencerChordSpec chordSpec{};
        if (!state::sequencer::decodeSequencerGraphChordSpec(record, chordSpec)) {
            return false;
        }
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
            .chordMode =
                state::sequencer::sanitizeSequencerGraphChordMode(record.chordMode),
            .chordSpec = chordSpec,
            .childSequenceId = record.childSequenceId,
            .cycleSetId = record.cycleSetId,
        };
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
    CcLanePtr& out
) {
    out.reset();
    if (sections.ccLaneBank.data == nullptr) return true;
    if (sections.ccLaneBank.count != 1 ||
        !sectionHasExactRecordShape(
            sections.ccLaneBank,
            SEQUENCER_CC_LANE_BANK_RECORD_SIZE
        )) {
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
    std::array<GraphPtr, PERSISTED_TRACK_COUNT>& graphs
) {
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!decodeGraphSections(sections[i], graphs[i])) return false;
    }
    return true;
}

FLASHMEM bool decodeTrackCcLanes(
    const std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>& sections,
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT>& lanes
) {
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!decodeCcLaneSection(sections[i], lanes[i])) return false;
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
        kEnvelopeVersion
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
        !addCcLaneSection(writer, lanes, 0) ||
        !addPatternRegionSection(
            writer,
            state::sequencer::patternPlaybackRegion(source),
            0
        )) {
        return {};
    }
    return writer.finish();
}

FLASHMEM bool applyPatternEnvelope(const uint8_t* data,
                                   uint32_t size,
                                   state::sequencer::SequencerPatternState& target) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> graphs{};
    if (!findSections(
            data,
            size,
            EnvelopeKind::Pattern,
            SectionId::FlatPattern,
            flat,
            &graphs
        )) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, PATTERN_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    PatternRegionArray regions{};
    GraphPtr graph;
    CcLanePtr lanes;
    if (!decodePatternRegions(
            flat,
            graphs,
            EnvelopeKind::Pattern,
            regions
        ) ||
        !decodeGraphSections(graphs[0], graph) ||
        !decodeCcLaneSection(graphs[0], lanes)) {
        return false;
    }
    if (!applyPatternPayload(flat.data, flat.byteSize, target)) return false;
    installPatternRegion(target, regions[0]);
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
    EnvelopeWriter writer(
        out,
        capacity,
        EnvelopeKind::ProjectSequencer,
        kEnvelopeVersion
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
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!addPatternRegionSection(
                writer,
                snapshotPlaybackRegion(source.flat->tracks[i]),
                i
            )) {
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
    if (!findSections(data,
                      size,
                      EnvelopeKind::ProjectSequencer,
                       SectionId::FlatProjectSequencer,
                       flat,
                       &graphs)) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, PROJECT_SEQUENCER_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    std::array<GraphPtr, PERSISTED_TRACK_COUNT> decodedGraphs{};
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT> decodedLanes{};
    PatternRegionArray regions{};
    GraphPtr activeGraph;
    CcLanePtr activeLanes;
    const uint8_t activeTrack = projectActiveTrack(flat);
    if (!decodePatternRegions(
            flat,
            graphs,
            EnvelopeKind::ProjectSequencer,
            regions
        ) ||
        !decodeTrackGraphs(graphs, decodedGraphs) ||
        !decodeTrackCcLanes(graphs, decodedLanes) ||
        !cloneActiveGraph(decodedGraphs, activeTrack, activeGraph) ||
        !cloneActiveCcLanes(decodedLanes, activeTrack, activeLanes)) {
        return false;
    }
    if (!applyProjectSequencerPayload(flat.data, flat.byteSize, trackBank, active)) {
        return false;
    }
    installTrackGraphs(decodedGraphs, std::move(activeGraph), trackBank, active);
    installTrackCcLanes(decodedLanes, std::move(activeLanes), trackBank, active);
    installTrackPatternRegions(regions, activeTrack, trackBank, active);
    return true;
}

FLASHMEM EnvelopeEncodeResult fillSetEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint32_t capacity
) {
    EnvelopeWriter writer(
        out,
        capacity,
        EnvelopeKind::Set,
        kEnvelopeVersion
    );
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
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!addPatternRegionSection(
                writer,
                state::sequencer::patternPlaybackRegion(
                    sourceTrack(trackBank, active, i)
                ),
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
    if (!findSections(
            data,
            size,
            EnvelopeKind::Set,
            SectionId::FlatSet,
            flat,
            &graphs
        )) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, SET_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    std::array<GraphPtr, PERSISTED_TRACK_COUNT> decodedGraphs{};
    std::array<CcLanePtr, PERSISTED_TRACK_COUNT> decodedLanes{};
    PatternRegionArray regions{};
    GraphPtr activeGraph;
    CcLanePtr activeLanes;
    const uint8_t activeTrack = setActiveTrack(flat);
    if (!decodePatternRegions(
            flat,
            graphs,
            EnvelopeKind::Set,
            regions
        ) ||
        !decodeTrackGraphs(graphs, decodedGraphs) ||
        !decodeTrackCcLanes(graphs, decodedLanes) ||
        !cloneActiveGraph(decodedGraphs, activeTrack, activeGraph) ||
        !cloneActiveCcLanes(decodedLanes, activeTrack, activeLanes)) {
        return false;
    }
    if (!applySetPayload(flat.data, flat.byteSize, trackBank, active)) {
        return false;
    }
    installTrackGraphs(decodedGraphs, std::move(activeGraph), trackBank, active);
    installTrackCcLanes(decodedLanes, std::move(activeLanes), trackBank, active);
    installTrackPatternRegions(regions, activeTrack, trackBank, active);
    return true;
}

}  // namespace core::persistence::sequencer_codec

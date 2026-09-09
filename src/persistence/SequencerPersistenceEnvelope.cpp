#include "persistence/SequencerPersistenceEnvelope.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/DrumTrackPersistenceCodec.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/SequencerCcLanePersistenceCodec.hpp"
#include "persistence/SequencerGraphRecordCodec.hpp"
#include "persistence/SequencerPersistenceCodec.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphCanonicalPolicy.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerClipRegionOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::persistence::sequencer_codec {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequenceKind;
namespace binary = core::persistence::binary_codec;
namespace graph_record = core::persistence::sequencer_graph_record_codec;
namespace graph_policy =
    core::state::sequencer::graph_canonical_policy;

constexpr uint32_t kEnvelopeMagic = 0x53514534;  // "SQE4"
constexpr uint8_t kEnvelopeVersion = ENVELOPE_VERSION;
constexpr uint16_t kEnvelopeHeaderSize = 12;
constexpr uint16_t kSectionHeaderSize = 10;
constexpr uint8_t kNoTrack = 0xFF;

enum class EnvelopeKind : uint8_t {
    Pattern = 1,
    ProjectSequencer = 2,
};

enum class SectionId : uint16_t {
    FlatPattern = 1,
    FlatProjectSequencer = 2,
    GraphSequences = 16,
    GraphStepNodes = 17,
    GraphCycleSets = 18,
    CcLaneBank = 19,
    ClipRegion = 20,
    DrumTrack = 21,
    ClipGrid = 22,
    ClipDocument = 23,
    LauncherMetadata = 24,
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

using GraphPtr = core::app::ExtmemUniquePtr<StepSequencerGraph>;
using CcLanePtr = state::sequencer::SequencerCcLaneBankPtr;
using DrumBankPtr = core::app::ExtmemUniquePtr<
    state::sequencer::DrumTrackBankSnapshot>;

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
    SectionView clipRegion{};
    SectionView drumTrack{};
};

struct ClipSectionViews {
    SectionView grid{};
    SectionView launcherMetadata{};
    std::array<
        SectionView,
        state::sequencer::SequencerClipGridState::CELL_COUNT
    > documents{};
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
        if (node.localVariation.pitchSemitones != 0 ||
            node.localVariation.velocity != 0 ||
            node.localVariation.gatePercent != 0 ||
            node.localVariation.nudge != 0) {
            return true;
        }
    }
    return false;
}

FLASHMEM bool graphRecordsAreCanonical(
    const StepSequencerGraph& graph
) {
    if (!graph.enabled ||
        graph.stepNodeCount >
            graph.stepNodes.size() ||
        graph.sequenceCount >
            graph.sequences.size() ||
        graph.cycleSetCount >
            graph.cycleSets.size()) {
        return false;
    }

    const auto* root = graph.sequence(graph.rootSequenceId);
    if (root == nullptr ||
        root->kind != StepSequencerSequenceKind::RootPattern ||
        root->firstStepNode != 0U ||
        root->length !=
            state::sequencer::SequencerPatternState::MAX_STEPS) {
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
        const auto& node = graph.stepNodes[i];
        if (!graph_policy::stepNodeIsCanonical(node)) return false;
        if ((node.flags & STEP_NODE_CHILD_SEQUENCE) != 0 &&
            graph.sequence(node.childSequenceId) == nullptr) {
            return false;
        }
        if ((node.flags & STEP_NODE_CYCLE_SET) != 0 &&
            graph.cycleSet(node.cycleSetId) == nullptr) {
            return false;
        }
    }
    return true;
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

FLASHMEM bool addDrumTrackSection(
    EnvelopeWriter& writer,
    const state::sequencer::DrumTrackState& track,
    uint8_t trackIndex
) {
    uint8_t* data = nullptr;
    if (!writer.reserveSection(
            SectionId::DrumTrack,
            trackIndex,
            DRUM_TRACK_RECORD_SIZE,
            1U,
            DRUM_TRACK_RECORD_SIZE,
            data
        )) {
        return false;
    }
    return encodeDrumTrackRecord(track, data, DRUM_TRACK_RECORD_SIZE);
}

FLASHMEM bool addClipRegionSection(
    EnvelopeWriter& writer,
    const state::sequencer::SequencerClipPlaybackRegion& region,
    uint8_t track
) {
    if (!region.isValid()) return false;

    uint8_t* data = nullptr;
    if (!writer.reserveSection(
            SectionId::ClipRegion,
            track,
            CLIP_REGION_RECORD_SIZE,
            1,
            CLIP_REGION_RECORD_SIZE,
            data
        )) {
        return false;
    }
    data[0] = region.playStart;
    data[1] = region.loopStart;
    data[2] = region.loopEnd;
    return true;
}

FLASHMEM state::sequencer::SequencerClipPlaybackRegion snapshotPlaybackRegion(
    const state::sequencer::SequencerPatternSnapshot& snapshot,
    const state::sequencer::SequencerClipSnapshot& clip
) {
    const uint16_t ticksPerStep = state::sequencer::sequencerTicksPerStep(
        snapshot.stepsPerBeat
    );
    if (ticksPerStep == 0U) {
        return state::sequencer::SequencerClipPlaybackRegion::fullLength(
            std::clamp<uint8_t>(
                snapshot.length,
                state::sequencer::SequencerClipPlaybackRegion::MIN_CONTENT_LENGTH,
                state::sequencer::SequencerClipPlaybackRegion::MAX_CONTENT_LENGTH
            )
        );
    }
    const state::sequencer::SequencerClipPlaybackRegion region{
        snapshot.length,
        static_cast<uint8_t>(clip.playStartTick / ticksPerStep),
        static_cast<uint8_t>(clip.loopStartTick / ticksPerStep),
        static_cast<uint8_t>(clip.loopEndTick / ticksPerStep),
    };
    return region.isValid()
        ? region
        : state::sequencer::SequencerClipPlaybackRegion::fullLength(
              snapshot.length
          );
}

FLASHMEM bool addGraphSections(EnvelopeWriter& writer,
                               const StepSequencerGraph* graph,
                               uint8_t track) {
    if (graph == nullptr) return true;
    if (!graphRecordsAreCanonical(*graph)) return false;
    if (!hasPersistableGraph(graph)) return true;

    const uint16_t sequenceCount = graph->sequenceCount;
    const uint16_t nodeCount = graph->stepNodeCount;
    const uint16_t cycleSetCount = graph->cycleSetCount;

    const uint16_t sequenceBytes = static_cast<uint16_t>(
        sequenceCount * graph_record::SEQUENCE_RECORD_SIZE
    );
    const uint16_t nodeBytes = static_cast<uint16_t>(
        nodeCount * graph_record::STEP_NODE_RECORD_SIZE
    );
    const uint16_t cycleSetBytes = static_cast<uint16_t>(
        cycleSetCount * graph_record::CYCLE_SET_RECORD_SIZE
    );

    uint8_t* sequenceData = nullptr;
    uint8_t* nodeData = nullptr;
    uint8_t* cycleSetData = nullptr;
    if (!writer.reserveSection(SectionId::GraphSequences,
                               track,
                               graph_record::SEQUENCE_RECORD_SIZE,
                               sequenceCount,
                               sequenceBytes,
                               sequenceData) ||
        !writer.reserveSection(SectionId::GraphStepNodes,
                               track,
                               graph_record::STEP_NODE_RECORD_SIZE,
                               nodeCount,
                               nodeBytes,
                               nodeData) ||
        !writer.reserveSection(SectionId::GraphCycleSets,
                               track,
                               graph_record::CYCLE_SET_RECORD_SIZE,
                               cycleSetCount,
                               cycleSetBytes,
                               cycleSetData)) {
        return false;
    }

    for (uint16_t i = 0; i < sequenceCount; ++i) {
        if (!graph_record::encodeSequence(
                graph->sequences[i],
                sequenceData + i * graph_record::SEQUENCE_RECORD_SIZE,
                graph_record::SEQUENCE_RECORD_SIZE
            )) {
            return false;
        }
    }

    for (uint16_t i = 0; i < nodeCount; ++i) {
        if (!graph_record::encodeStepNode(
                graph->stepNodes[i],
                nodeData + i * graph_record::STEP_NODE_RECORD_SIZE,
                graph_record::STEP_NODE_RECORD_SIZE
            )) {
            return false;
        }
    }

    for (uint16_t i = 0; i < cycleSetCount; ++i) {
        if (!graph_record::encodeCycleSet(
                graph->cycleSets[i],
                cycleSetData + i * graph_record::CYCLE_SET_RECORD_SIZE,
                graph_record::CYCLE_SET_RECORD_SIZE
            )) {
            return false;
        }
    }

    return true;
}

FLASHMEM bool measurePatternEnvelope(
    const StepSequencerGraph* graph,
    const state::sequencer::SequencerCcLaneBank* lanes,
    uint16_t& out
) {
    uint32_t size = kEnvelopeHeaderSize + kSectionHeaderSize +
        PATTERN_PAYLOAD_SIZE;
    if (graph != nullptr) {
        if (!graphRecordsAreCanonical(*graph)) return false;
        if (hasPersistableGraph(graph)) {
            size += 3U * kSectionHeaderSize +
                static_cast<uint32_t>(graph->sequenceCount) *
                    graph_record::SEQUENCE_RECORD_SIZE +
                static_cast<uint32_t>(graph->stepNodeCount) *
                    graph_record::STEP_NODE_RECORD_SIZE +
                static_cast<uint32_t>(graph->cycleSetCount) *
                    graph_record::CYCLE_SET_RECORD_SIZE;
        }
    }
    if (hasPersistableCcLanes(lanes)) {
        size += kSectionHeaderSize + SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
    }
    if (size > UINT16_MAX) return false;
    out = static_cast<uint16_t>(size);
    return true;
}

FLASHMEM bool addClipGridSections(
    EnvelopeWriter& writer,
    const state::sequencer::SequencerClipGridSnapshot& clips
) {
    uint8_t* grid = nullptr;
    if (!writer.reserveSection(
            SectionId::ClipGrid,
            kNoTrack,
            1U,
            state::sequencer::SequencerClipGridState::TRACK_COUNT,
            CLIP_GRID_RECORD_SIZE,
            grid
        )) {
        return false;
    }
    std::memcpy(
        grid,
        clips.residentSlots.data(),
        CLIP_GRID_RECORD_SIZE
    );

    uint8_t* metadata = nullptr;
    if (!writer.reserveSection(
            SectionId::LauncherMetadata,
            kNoTrack,
            1U,
            LAUNCHER_METADATA_RECORD_SIZE,
            LAUNCHER_METADATA_RECORD_SIZE,
            metadata
        )) {
        return false;
    }
    binary::Writer metadataWriter(metadata, LAUNCHER_METADATA_RECORD_SIZE);
    for (const uint16_t mask : clips.stopMasks) {
        if (!metadataWriter.writeU16(mask)) return false;
    }
    const auto writeBehavior = [&metadataWriter](
        const state::sequencer::SequencerLauncherBehavior& behavior
    ) {
        return metadataWriter.writeU8(behavior.length) &&
            metadataWriter.writeU8(static_cast<uint8_t>(behavior.follow)) &&
            metadataWriter.writeU8(
                static_cast<uint8_t>(behavior.quantization));
    };
    for (const auto& behavior : clips.clipBehaviors) {
        if (!writeBehavior(behavior)) return false;
    }
    for (const auto& behavior : clips.sceneBehaviors) {
        if (!writeBehavior(behavior)) return false;
    }
    if (!metadataWriter.ok() ||
        metadataWriter.offset() != LAUNCHER_METADATA_RECORD_SIZE) {
        return false;
    }

    for (uint16_t index = 0U;
         index < state::sequencer::SequencerClipGridState::CELL_COUNT;
         ++index) {
        const auto* document = clips.documents[index].get();
        if (document == nullptr) continue;

        uint16_t patternSize = 0U;
        if (!measurePatternEnvelope(
                document->graph.get(),
                document->ccLanes.get(),
                patternSize
            )) {
            return false;
        }
        const bool drum = document->trackKind ==
            state::sequencer::SequencerTrackKind::DRUM;
        if (drum != (document->drum != nullptr) ||
            (drum && document->ccLanes != nullptr)) {
            return false;
        }
        const uint16_t drumSize = drum ? DRUM_TRACK_RECORD_SIZE : 0U;
        const uint32_t byteSizeWide = CLIP_DOCUMENT_HEADER_SIZE +
            static_cast<uint32_t>(patternSize) + drumSize;
        if (byteSizeWide > UINT16_MAX) return false;
        const uint16_t byteSize = static_cast<uint16_t>(byteSizeWide);

        uint8_t* data = nullptr;
        if (!writer.reserveSection(
                SectionId::ClipDocument,
                static_cast<uint8_t>(index),
                0U,
                1U,
                byteSize,
                data
            )) {
            return false;
        }
        binary::Writer record(data, byteSize);
        uint8_t* patternData = nullptr;
        uint8_t* drumData = nullptr;
        if (!record.writeU8(static_cast<uint8_t>(document->trackKind)) ||
            !record.writeU8(0U) ||
            !record.writeU16(patternSize) ||
            !record.writeU16(drumSize) ||
            !record.writeU16(document->clip.playStartTick) ||
            !record.writeU16(document->clip.loopStartTick) ||
            !record.writeU16(document->clip.loopEndTick) ||
            !record.reserveBytes(patternSize, patternData) ||
            !record.reserveBytes(drumSize, drumData) ||
            !record.ok() || record.offset() != byteSize) {
            return false;
        }
        const auto pattern = fillPatternEnvelope(
            document->pattern,
            document->graph.get(),
            document->ccLanes.get(),
            patternData,
            patternSize
        );
        if (!pattern.ok || pattern.size != patternSize ||
            (drum && !encodeDrumTrackRecord(
                *document->drum,
                drumData,
                drumSize
            ))) {
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
                           std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>* graphViews,
                           ClipSectionViews* clipViews) {
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
        } else if (id == SectionId::ClipGrid &&
                   kind == EnvelopeKind::ProjectSequencer &&
                   clipViews != nullptr && section.track == kNoTrack) {
            if (!assignSectionView(clipViews->grid, view)) return false;
        } else if (id == SectionId::ClipDocument &&
                   kind == EnvelopeKind::ProjectSequencer &&
                   clipViews != nullptr &&
                   section.track < clipViews->documents.size()) {
            if (!assignSectionView(clipViews->documents[section.track], view)) {
                return false;
            }
        } else if (id == SectionId::LauncherMetadata &&
                   kind == EnvelopeKind::ProjectSequencer &&
                   clipViews != nullptr && section.track == kNoTrack) {
            if (!assignSectionView(clipViews->launcherMetadata, view)) {
                return false;
            }
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
                case SectionId::ClipRegion:
                    if (!assignSectionView(graph.clipRegion, view)) {
                        return false;
                    }
                    break;
                case SectionId::DrumTrack:
                    if (kind == EnvelopeKind::Pattern ||
                        !assignSectionView(graph.drumTrack, view)) {
                        return false;
                    }
                    break;
                default:
                    return false;
            }
        } else {
            // Current-format envelopes have one exact section vocabulary.
            // Unknown or misplaced data is unsupported and never published.
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

FLASHMEM bool linkSequenceValid(const StepSequencerGraph& graph, uint16_t id) {
    return graph.sequence(id) != nullptr;
}

FLASHMEM bool linkCycleSetValid(const StepSequencerGraph& graph, uint16_t id) {
    return graph.cycleSet(id) != nullptr;
}

FLASHMEM bool graphHasCanonicalPersistedContent(
    const StepSequencerGraph& graph
) {
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

    if (sections.sequences.data == nullptr ||
        sections.stepNodes.data == nullptr ||
        sections.cycleSets.data == nullptr) {
        return false;
    }
    if (!sectionHasExactRecordShape(
            sections.sequences,
            graph_record::SEQUENCE_RECORD_SIZE
        ) ||
        !sectionHasExactRecordShape(
            sections.stepNodes,
            graph_record::STEP_NODE_RECORD_SIZE
        )) {
        return false;
    }
    if (sections.cycleSets.data != nullptr &&
        !sectionHasExactRecordShape(
            sections.cycleSets,
            graph_record::CYCLE_SET_RECORD_SIZE
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
        if (!graph_record::decodeSequence(
                sections.sequences.data + i * graph_record::SEQUENCE_RECORD_SIZE,
                graph_record::SEQUENCE_RECORD_SIZE,
                graph->sequences[i]
            )) {
            return false;
        }
    }

    for (uint16_t i = 0; i < sections.stepNodes.count; ++i) {
        if (!graph_record::decodeStepNode(
                sections.stepNodes.data + i * graph_record::STEP_NODE_RECORD_SIZE,
                graph_record::STEP_NODE_RECORD_SIZE,
                graph->stepNodes[i]
            )) {
            return false;
        }
    }

    for (uint16_t i = 0; i < sections.cycleSets.count; ++i) {
        if (!graph_record::decodeCycleSet(
                sections.cycleSets.data + i * graph_record::CYCLE_SET_RECORD_SIZE,
                graph_record::CYCLE_SET_RECORD_SIZE,
                graph->cycleSets[i]
            )) {
            return false;
        }
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
    for (uint8_t i = 0; i < graph->sequenceCount; ++i) {
        if (graph->sequence(i) == nullptr) return false;
    }
    for (uint8_t i = 0; i < graph->cycleSetCount; ++i) {
        if (graph->cycleSet(i) == nullptr) return false;
    }

    if (!graphRecordsAreCanonical(*graph) ||
        !graphHasCanonicalPersistedContent(*graph)) {
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
    if (state::sequencer::sequencerCcLaneCount(decoded) == 0) return false;
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

FLASHMEM bool decodeTrackDrums(
    const std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>& sections,
    uint16_t enabledMask,
    DrumBankPtr& out
) {
    out.reset();
    uint16_t drumMask = 0U;
    for (uint8_t track = 0U; track < PERSISTED_TRACK_COUNT; ++track) {
        if (sections[track].drumTrack.data != nullptr) {
            drumMask = static_cast<uint16_t>(drumMask | (1U << track));
        }
    }
    if (drumMask == 0U) return true;
    if ((drumMask & static_cast<uint16_t>(~enabledMask)) != 0U) {
        return false;
    }

    auto decoded = core::app::makeExtmemUnique<
        state::sequencer::DrumTrackBankSnapshot>();
    if (!decoded) return false;
    decoded->drumTrackMask = drumMask;
    for (auto& track : decoded->tracks) track.reset();

    for (uint8_t track = 0U; track < PERSISTED_TRACK_COUNT; ++track) {
        const auto& content = sections[track];
        if (content.drumTrack.data == nullptr) continue;
        // A Drum Track reuses its ordinary Pattern Graph for sparse
        // MicroSequence/Cycle content. CC lanes remain instrument-only.
        const bool hasInstrumentOnlyPayload =
            content.ccLaneBank.data != nullptr;
        const bool currentShape = sectionHasExactRecordShape(
            content.drumTrack,
            DRUM_TRACK_RECORD_SIZE
        );
        if (hasInstrumentOnlyPayload || content.drumTrack.count != 1U ||
            !currentShape ||
            !decodeDrumTrackRecord(
                content.drumTrack.data,
                content.drumTrack.byteSize,
                decoded->tracks[track]
            )) {
            return false;
        }
    }
    out = std::move(decoded);
    return true;
}

FLASHMEM bool decodeClipDocument(
    const SectionView& section,
    state::sequencer::SequencerTrackKind expectedKind,
    state::sequencer::SequencerClipDocumentPtr& out
) {
    out.reset();
    if (section.data == nullptr || section.recordSize != 0U ||
        section.count != 1U || section.byteSize < CLIP_DOCUMENT_HEADER_SIZE) {
        return false;
    }

    binary::Reader reader(section.data, section.byteSize);
    uint8_t kindRaw = 0U;
    uint8_t reserved = 0U;
    uint16_t patternSize = 0U;
    uint16_t drumSize = 0U;
    state::sequencer::SequencerClipSnapshot clip{};
    if (!reader.readU8(kindRaw) || !reader.readU8(reserved) ||
        !reader.readU16(patternSize) || !reader.readU16(drumSize) ||
        !reader.readU16(clip.playStartTick) ||
        !reader.readU16(clip.loopStartTick) ||
        !reader.readU16(clip.loopEndTick) || reserved != 0U ||
        kindRaw > static_cast<uint8_t>(state::sequencer::SequencerTrackKind::DRUM) ||
        static_cast<state::sequencer::SequencerTrackKind>(kindRaw) != expectedKind ||
        patternSize < kEnvelopeHeaderSize ||
        static_cast<uint32_t>(patternSize) + drumSize != reader.remaining()) {
        return false;
    }

    auto pattern = core::app::makeExtmemUnique<
        state::sequencer::SequencerPatternState>();
    if (!pattern ||
        !applyPatternEnvelope(reader.current(), patternSize, *pattern) ||
        !reader.skip(patternSize)) {
        return false;
    }

    const bool drum = expectedKind == state::sequencer::SequencerTrackKind::DRUM;
    if ((drum && drumSize != DRUM_TRACK_RECORD_SIZE) ||
        (!drum && drumSize != 0U) ||
        (drum && pattern->ccLanes != nullptr)) {
        return false;
    }

    auto document = core::app::makeExtmemUniqueCold<
        state::sequencer::SequencerClipDocument>();
    if (!document) return false;
    state::sequencer::captureSnapshot(*pattern, document->pattern);
    document->clip = clip;
    document->ccLaneRevision = pattern->ccLaneRevision.get();
    document->trackKind = expectedKind;
    document->graph = std::move(pattern->graph);
    document->ccLanes = std::move(pattern->ccLanes);
    if (drum) {
        document->drum = core::app::makeExtmemUnique<
            state::sequencer::DrumTrackState>();
        if (!document->drum ||
            !decodeDrumTrackRecord(
                reader.current(),
                drumSize,
                *document->drum
            ) ||
            !reader.skip(drumSize)) {
            return false;
        }
    }
    if (!reader.ok() || reader.remaining() != 0U) return false;
    out = std::move(document);
    return true;
}

FLASHMEM bool decodeClipGrid(
    const ClipSectionViews& sections,
    uint16_t enabledTrackMask,
    uint16_t drumTrackMask,
    state::sequencer::SequencerClipGridSnapshot& out
) {
    if (sections.grid.data == nullptr ||
        sections.grid.count != state::sequencer::SequencerClipGridState::TRACK_COUNT ||
        !sectionHasExactRecordShape(sections.grid, 1U)) {
        return false;
    }

    state::sequencer::SequencerClipGridSnapshot decoded;
    std::memcpy(
        decoded.residentSlots.data(),
        sections.grid.data,
        CLIP_GRID_RECORD_SIZE
    );
    if (sections.launcherMetadata.data == nullptr ||
        sections.launcherMetadata.recordSize != 1U ||
        sections.launcherMetadata.count != LAUNCHER_METADATA_RECORD_SIZE ||
        sections.launcherMetadata.byteSize != LAUNCHER_METADATA_RECORD_SIZE) {
        return false;
    }
    binary::Reader metadata(
        sections.launcherMetadata.data,
        sections.launcherMetadata.byteSize
    );
    for (auto& mask : decoded.stopMasks) {
        if (!metadata.readU16(mask)) return false;
    }
    const auto readBehavior = [&metadata](
        state::sequencer::SequencerLauncherBehavior& behavior
    ) {
        uint8_t follow = 0U;
        uint8_t quantization = 0U;
        if (!metadata.readU8(behavior.length) ||
            !metadata.readU8(follow) ||
            !metadata.readU8(quantization) || quantization > 2U) {
            return false;
        }
        behavior.follow = static_cast<
            state::sequencer::SequencerLauncherFollowChoice>(follow);
        behavior.quantization = static_cast<
            state::sequencer::SequencerLauncherFollowQuantization>(
                quantization);
        return true;
    };
    for (auto& behavior : decoded.clipBehaviors) {
        if (!readBehavior(behavior)) return false;
    }
    for (auto& behavior : decoded.sceneBehaviors) {
        if (!readBehavior(behavior)) return false;
    }
    if (!metadata.ok() || metadata.remaining() != 0U) return false;
    for (uint16_t index = 0U;
         index < state::sequencer::SequencerClipGridState::CELL_COUNT;
         ++index) {
        decoded.generations[index] = 1U;
        const auto& section = sections.documents[index];
        if (section.data == nullptr) continue;
        const uint8_t track = static_cast<uint8_t>(
            index / state::sequencer::SequencerClipGridState::SLOT_COUNT
        );
        const auto kind = (drumTrackMask & static_cast<uint16_t>(1U << track)) != 0U
            ? state::sequencer::SequencerTrackKind::DRUM
            : state::sequencer::SequencerTrackKind::INSTRUMENT;
        if (!decodeClipDocument(section, kind, decoded.documents[index])) {
            return false;
        }
    }
    if (!state::sequencer::validSequencerClipGridSnapshot(
            decoded,
            enabledTrackMask,
            drumTrackMask
        )) {
        return false;
    }
    out = std::move(decoded);
    return true;
}

template<typename PatternSource>
FLASHMEM EnvelopeEncodeResult fillPatternEnvelopeSource(
    const PatternSource& source,
    const StepSequencerGraph* graph,
    const state::sequencer::SequencerCcLaneBank* lanes,
    uint8_t* out,
    uint32_t capacity
) {
    EnvelopeWriter writer(
        out,
        capacity,
        EnvelopeKind::Pattern,
        kEnvelopeVersion
    );
    uint8_t* flat = nullptr;
    if (!writer.reserveSection(
            SectionId::FlatPattern,
            kNoTrack,
            PATTERN_PAYLOAD_SIZE,
            1U,
            PATTERN_PAYLOAD_SIZE,
            flat
        ) ||
        !fillPatternPayload(source, flat, PATTERN_PAYLOAD_SIZE) ||
        !addGraphSections(writer, graph, 0U) ||
        !addCcLaneSection(writer, lanes, 0U)) {
        return {};
    }
    return writer.finish();
}

}  // namespace

FLASHMEM EnvelopeEncodeResult fillPatternEnvelope(
    const state::sequencer::SequencerPatternState& source,
    uint8_t* out,
    uint32_t capacity
) {
    return fillPatternEnvelopeSource(
        source,
        state::sequencer::graphView(source),
        state::sequencer::sequencerCcLaneView(source),
        out,
        capacity
    );
}

FLASHMEM EnvelopeEncodeResult fillPatternEnvelope(
    const state::sequencer::SequencerPatternSnapshot& source,
    const StepSequencerGraph* graph,
    const state::sequencer::SequencerCcLaneBank* ccLanes,
    uint8_t* out,
    uint32_t capacity
) {
    return fillPatternEnvelopeSource(
        source,
        graph,
        ccLanes,
        out,
        capacity
    );
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
            &graphs,
            nullptr
        )) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, PATTERN_PAYLOAD_SIZE) || flat.count != 1) {
        return false;
    }

    GraphPtr graph;
    CcLanePtr lanes;
    if (!decodeGraphSections(graphs[0], graph) ||
        !decodeCcLaneSection(graphs[0], lanes)) {
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
    if (source.flat == nullptr || source.clips == nullptr) return {};
    const uint16_t drumMask = source.drums != nullptr
        ? static_cast<uint16_t>(
              source.drums->drumTrackMask & source.flat->enabledMask)
        : 0U;
    if (!state::sequencer::validSequencerClipGridSnapshot(
            *source.clips,
            source.flat->enabledMask,
            drumMask
        )) {
        return {};
    }
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
        const uint16_t trackBit = static_cast<uint16_t>(1U << i);
        const bool drumTrack = (drumMask & trackBit) != 0U;
        if (drumTrack) {
            if (!addDrumTrackSection(writer, source.drums->tracks[i], i) ||
                !addGraphSections(writer, source.graphs[i], i)) {
                return {};
            }
        } else if (!addGraphSections(writer, source.graphs[i], i) ||
                   !addCcLaneSection(writer, source.ccLanes[i], i)) {
            return {};
        }
    }
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        if (!addClipRegionSection(
                writer,
                snapshotPlaybackRegion(
                    source.flat->tracks[i],
                    source.flat->clips[i]
                ),
                i
            )) {
            return {};
        }
    }
    if (!addClipGridSections(writer, *source.clips)) return {};
    return writer.finish();
}

FLASHMEM bool decodeProjectSequencerEnvelope(
    const uint8_t* data,
    uint32_t size,
    state::sequencer::SequencerHistoryTrackBankSnapshot& target,
    state::sequencer::SequencerClipGridSnapshot& clips,
    DrumBankPtr& drums
) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> sections{};
    ClipSectionViews clipSections{};
    if (!findSections(data, size, EnvelopeKind::ProjectSequencer,
                      SectionId::FlatProjectSequencer, flat,
                      &sections, &clipSections) ||
        !sectionHasExactRecordShape(flat, PROJECT_SEQUENCER_PAYLOAD_SIZE) ||
        flat.count != 1U) {
        return false;
    }

    // The caller owns a disposable candidate. Decode directly into its final
    // owners; Project decode publishes it only after every chunk is accepted.
    target.reset();
    if (!decodeProjectSequencerPayload(flat.data, flat.byteSize, target.flat,
                                       target.focusedStep, target.activeStepProperty)) {
        return false;
    }
    for (uint8_t track = 0U; track < PERSISTED_TRACK_COUNT; ++track) {
        const auto& section = sections[track];
        const auto& regionSection = section.clipRegion;
        if (regionSection.data == nullptr || regionSection.count != 1U ||
            !sectionHasExactRecordShape(regionSection, CLIP_REGION_RECORD_SIZE)) {
            return false;
        }
        auto& pattern = target.flat.tracks[track];
        const state::sequencer::SequencerClipPlaybackRegion region{
            pattern.length, regionSection.data[0], regionSection.data[1], regionSection.data[2]
        };
        if (!region.isValid()) return false;
        const uint16_t ticks = state::sequencer::sequencerTicksPerStep(pattern.stepsPerBeat);
        target.flat.clips[track] = {
            static_cast<uint16_t>(region.playStart * ticks),
            static_cast<uint16_t>(region.loopStart * ticks),
            static_cast<uint16_t>(region.loopEnd * ticks),
        };
        auto& graph = target.bankGraphs[track];
        auto& lanes = target.bankCcLanes[track];
        if (!decodeGraphSections(section, graph) || !decodeCcLaneSection(section, lanes)) {
            return false;
        }
    }
    return decodeTrackDrums(sections, target.flat.enabledMask, drums) &&
        decodeClipGrid(clipSections, target.flat.enabledMask,
                       drums ? drums->drumTrackMask : 0U, clips);
}

}  // namespace core::persistence::sequencer_codec

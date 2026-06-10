#include "persistence/SequencerPersistenceEnvelope.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/SequencerPersistenceCodec.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::persistence::sequencer_codec {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;

constexpr uint32_t kEnvelopeMagic = 0x53514534;  // "SQE4"
constexpr uint8_t kEnvelopeVersion = 1;
constexpr uint8_t kNoTrack = 0xFF;
constexpr uint16_t kInvalidId = StepSequencerGraphLimits::INVALID_ID;

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
};

#pragma pack(push, 1)
struct EnvelopeHeader {
    uint32_t magic = kEnvelopeMagic;
    uint8_t version = kEnvelopeVersion;
    uint8_t kind = 0;
    uint16_t headerSize = sizeof(EnvelopeHeader);
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

struct SequenceRecord {
    uint8_t kind = 0;
    uint16_t firstStepNode = kInvalidId;
    uint8_t length = 0;
    int8_t offset = 0;
};

struct StepNodeRecord {
    uint16_t flags = 0;
    int8_t noteOffset = 0;
    int16_t velocityOffset = 0;
    int16_t gateOffset = 0;
    int8_t nudgeOffset = 0;
    int16_t probabilityOffset = 0;
    uint16_t childSequenceId = kInvalidId;
    uint16_t cycleSetId = kInvalidId;
};

struct CycleSetRecord {
    uint16_t firstStateNode = kInvalidId;
    uint8_t length = 0;
};
#pragma pack(pop)

static_assert(sizeof(EnvelopeHeader) == 12, "Unexpected EnvelopeHeader size");
static_assert(sizeof(SectionHeader) == 10, "Unexpected SectionHeader size");
static_assert(sizeof(SequenceRecord) == 5, "Unexpected SequenceRecord size");
static_assert(sizeof(StepNodeRecord) == 14, "Unexpected StepNodeRecord size");
static_assert(sizeof(CycleSetRecord) == 3, "Unexpected CycleSetRecord size");

struct GraphRecordScratch {
    std::array<SequenceRecord, StepSequencerGraphLimits::MAX_SEQUENCES> sequences{};
    std::array<StepNodeRecord, StepSequencerGraphLimits::MAX_STEP_NODES> nodes{};
    std::array<CycleSetRecord, StepSequencerGraphLimits::MAX_CYCLE_SETS> cycleSets{};
};

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
};

class EnvelopeWriter {
public:
    EnvelopeWriter(uint8_t* out, uint16_t capacity, EnvelopeKind kind)
        : out_(out), capacity_(capacity) {
        if (out_ == nullptr || capacity_ < sizeof(EnvelopeHeader)) {
            return;
        }
        EnvelopeHeader header{};
        header.kind = static_cast<uint8_t>(kind);
        appendRaw_(&header, sizeof(header));
    }

    bool ok() const { return ok_; }
    uint16_t size() const { return offset_; }

    bool addSection(SectionId id,
                    uint8_t track,
                    uint16_t recordSize,
                    uint16_t count,
                    const void* data,
                    uint16_t byteSize) {
        if (!ok_) return false;
        if (byteSize > 0 && data == nullptr) {
            ok_ = false;
            return false;
        }

        SectionHeader header{};
        header.id = static_cast<uint16_t>(id);
        header.track = track;
        header.recordSize = recordSize;
        header.count = count;
        header.byteSize = byteSize;
        if (!appendRaw_(&header, sizeof(header))) return false;
        if (byteSize > 0 && !appendRaw_(data, byteSize)) return false;
        ++sectionCount_;
        return true;
    }

    EnvelopeEncodeResult finish() {
        if (!ok_ || out_ == nullptr || offset_ < sizeof(EnvelopeHeader)) {
            return {};
        }

        auto* header = reinterpret_cast<EnvelopeHeader*>(out_);
        header->sectionCount = sectionCount_;
        return {.ok = true, .size = offset_};
    }

private:
    bool appendRaw_(const void* data, uint16_t size) {
        if (data == nullptr || size == 0) return true;
        if (offset_ > capacity_ || size > static_cast<uint16_t>(capacity_ - offset_)) {
            ok_ = false;
            return false;
        }
        std::memcpy(out_ + offset_, data, size);
        offset_ = static_cast<uint16_t>(offset_ + size);
        return true;
    }

    uint8_t* out_ = nullptr;
    uint16_t capacity_ = 0;
    uint16_t offset_ = 0;
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
        if (graph->stepNodes[i].flags != 0) return true;
    }
    return false;
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

    auto scratch = core::app::makeExtmemUnique<GraphRecordScratch>();
    if (!scratch) return false;

    for (uint16_t i = 0; i < sequenceCount; ++i) {
        const auto& source = graph->sequences[i];
        scratch->sequences[i] = SequenceRecord{
            .kind = static_cast<uint8_t>(source.kind),
            .firstStepNode = source.firstStepNode,
            .length = source.length,
            .offset = source.offset,
        };
    }

    for (uint16_t i = 0; i < nodeCount; ++i) {
        const auto& source = graph->stepNodes[i];
        scratch->nodes[i] = StepNodeRecord{
            .flags = source.flags,
            .noteOffset = source.noteOffset,
            .velocityOffset = source.velocityOffset,
            .gateOffset = source.gateOffset,
            .nudgeOffset = source.nudgeOffset,
            .probabilityOffset = source.probabilityOffset,
            .childSequenceId = source.childSequenceId,
            .cycleSetId = source.cycleSetId,
        };
    }

    for (uint16_t i = 0; i < cycleSetCount; ++i) {
        const auto& source = graph->cycleSets[i];
        scratch->cycleSets[i] = CycleSetRecord{
            .firstStateNode = source.firstStateNode,
            .length = source.length,
        };
    }

    return writer.addSection(SectionId::GraphSequences,
                             track,
                             sizeof(SequenceRecord),
                             sequenceCount,
                             scratch->sequences.data(),
                             static_cast<uint16_t>(sequenceCount * sizeof(SequenceRecord))) &&
           writer.addSection(SectionId::GraphStepNodes,
                             track,
                             sizeof(StepNodeRecord),
                             nodeCount,
                             scratch->nodes.data(),
                             static_cast<uint16_t>(nodeCount * sizeof(StepNodeRecord))) &&
           writer.addSection(SectionId::GraphCycleSets,
                             track,
                             sizeof(CycleSetRecord),
                             cycleSetCount,
                             scratch->cycleSets.data(),
                             static_cast<uint16_t>(cycleSetCount * sizeof(CycleSetRecord)));
}

FLASHMEM bool isHeaderValid(const EnvelopeHeader& header, EnvelopeKind kind) {
    return header.magic == kEnvelopeMagic &&
           header.version == kEnvelopeVersion &&
           header.kind == static_cast<uint8_t>(kind) &&
           header.headerSize == sizeof(EnvelopeHeader);
}

FLASHMEM bool readSectionHeader(const uint8_t* data,
                                uint16_t size,
                                uint16_t& offset,
                                SectionHeader& out) {
    if (data == nullptr) return false;
    if (offset > size || sizeof(SectionHeader) > static_cast<uint16_t>(size - offset)) {
        return false;
    }
    std::memcpy(&out, data + offset, sizeof(SectionHeader));
    offset = static_cast<uint16_t>(offset + sizeof(SectionHeader));
    if (out.byteSize > static_cast<uint16_t>(size - offset)) {
        return false;
    }
    return true;
}

FLASHMEM bool findSections(const uint8_t* data,
                           uint16_t size,
                           EnvelopeKind kind,
                           SectionId flatId,
                           SectionView& flat,
                           std::array<GraphSectionViews, PERSISTED_TRACK_COUNT>* graphViews) {
    if (data == nullptr || size < sizeof(EnvelopeHeader)) return false;

    EnvelopeHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (!isHeaderValid(header, kind)) return false;

    uint16_t offset = header.headerSize;
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
            flat = view;
        } else if (graphViews != nullptr && section.track < graphViews->size()) {
            auto& graph = (*graphViews)[section.track];
            switch (id) {
                case SectionId::GraphSequences:
                    graph.sequences = view;
                    break;
                case SectionId::GraphStepNodes:
                    graph.stepNodes = view;
                    break;
                case SectionId::GraphCycleSets:
                    graph.cycleSets = view;
                    break;
                default:
                    break;
            }
        }

        offset = static_cast<uint16_t>(offset + section.byteSize);
    }

    return flat.data != nullptr;
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

FLASHMEM bool graphIsPersistableAfterSanitize(const StepSequencerGraph& graph) {
    return hasPersistableGraph(&graph);
}

FLASHMEM bool applyGraphSections(const GraphSectionViews& sections,
                                 state::sequencer::SequencerPatternState& target) {
    const bool hasAnyGraphSection =
        sections.sequences.data != nullptr ||
        sections.stepNodes.data != nullptr ||
        sections.cycleSets.data != nullptr;
    if (!hasAnyGraphSection) {
        state::sequencer::clearGraph(target);
        return true;
    }

    if (sections.sequences.data == nullptr || sections.stepNodes.data == nullptr) {
        state::sequencer::clearGraph(target);
        return true;
    }
    if (!sectionHasExactRecordShape(sections.sequences, sizeof(SequenceRecord)) ||
        !sectionHasExactRecordShape(sections.stepNodes, sizeof(StepNodeRecord))) {
        state::sequencer::clearGraph(target);
        return true;
    }
    if (sections.cycleSets.data != nullptr &&
        !sectionHasExactRecordShape(sections.cycleSets, sizeof(CycleSetRecord))) {
        state::sequencer::clearGraph(target);
        return true;
    }

    if (sections.sequences.count == 0 ||
        sections.sequences.count > StepSequencerGraphLimits::MAX_SEQUENCES ||
        sections.stepNodes.count < state::sequencer::SequencerPatternState::MAX_STEPS ||
        sections.stepNodes.count > StepSequencerGraphLimits::MAX_STEP_NODES ||
        sections.cycleSets.count > StepSequencerGraphLimits::MAX_CYCLE_SETS) {
        state::sequencer::clearGraph(target);
        return true;
    }

    auto graph = core::app::makeExtmemUnique<StepSequencerGraph>();
    if (!graph) return false;
    graph->reset();
    graph->enabled = true;
    graph->rootSequenceId = 0;
    graph->sequenceCount = static_cast<uint8_t>(sections.sequences.count);
    graph->stepNodeCount = sections.stepNodes.count;
    graph->cycleSetCount = static_cast<uint8_t>(sections.cycleSets.count);

    for (uint16_t i = 0; i < sections.sequences.count; ++i) {
        SequenceRecord record{};
        std::memcpy(&record,
                    sections.sequences.data + i * sizeof(SequenceRecord),
                    sizeof(record));
        graph->sequences[i] = StepSequencerSequence{
            .kind = static_cast<StepSequencerSequenceKind>(record.kind),
            .firstStepNode = record.firstStepNode,
            .length = record.length,
            .offset = record.offset,
        };
    }

    for (uint16_t i = 0; i < sections.stepNodes.count; ++i) {
        StepNodeRecord record{};
        std::memcpy(&record,
                    sections.stepNodes.data + i * sizeof(StepNodeRecord),
                    sizeof(record));
        graph->stepNodes[i] = StepSequencerStepNode{
            .flags = record.flags,
            .noteOffset = record.noteOffset,
            .velocityOffset = record.velocityOffset,
            .gateOffset = record.gateOffset,
            .nudgeOffset = record.nudgeOffset,
            .probabilityOffset = record.probabilityOffset,
            .childSequenceId = record.childSequenceId,
            .cycleSetId = record.cycleSetId,
        };
    }

    for (uint16_t i = 0; i < sections.cycleSets.count; ++i) {
        CycleSetRecord record{};
        std::memcpy(&record,
                    sections.cycleSets.data + i * sizeof(CycleSetRecord),
                    sizeof(record));
        graph->cycleSets[i] = StepSequencerCycleStateSet{
            .firstStateNode = record.firstStateNode,
            .length = record.length,
        };
    }

    const auto* root = graph->sequence(graph->rootSequenceId);
    if (root == nullptr ||
        root->kind != StepSequencerSequenceKind::RootPattern ||
        root->firstStepNode != 0 ||
        root->length != state::sequencer::SequencerPatternState::MAX_STEPS) {
        state::sequencer::clearGraph(target);
        return true;
    }

    for (uint16_t i = 0; i < graph->stepNodeCount; ++i) {
        auto& node = graph->stepNodes[i];
        if ((node.flags & STEP_NODE_CHILD_SEQUENCE) != 0 &&
            !linkSequenceValid(*graph, node.childSequenceId)) {
            node.flags = static_cast<uint16_t>(node.flags & ~STEP_NODE_CHILD_SEQUENCE);
            node.childSequenceId = kInvalidId;
        }
        if ((node.flags & STEP_NODE_CYCLE_SET) != 0 &&
            !linkCycleSetValid(*graph, node.cycleSetId)) {
            node.flags = static_cast<uint16_t>(node.flags & ~STEP_NODE_CYCLE_SET);
            node.cycleSetId = kInvalidId;
        }
    }

    if (!graphIsPersistableAfterSanitize(*graph)) {
        state::sequencer::clearGraph(target);
        return true;
    }

    target.graph = std::move(graph);
    target.bumpGraphRevision();
    return true;
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
    uint16_t capacity
) {
    EnvelopeWriter writer(out, capacity, EnvelopeKind::Pattern);
    PatternPayload flat{};
    fillPatternPayload(source, flat);
    writer.addSection(SectionId::FlatPattern,
                      kNoTrack,
                      sizeof(PatternPayload),
                      1,
                      &flat,
                      sizeof(flat));
    addGraphSections(writer, state::sequencer::graphView(source), 0);
    return writer.finish();
}

FLASHMEM bool applyPatternEnvelope(const uint8_t* data,
                                   uint16_t size,
                                   state::sequencer::SequencerPatternState& target) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> graphs{};
    if (!findSections(data, size, EnvelopeKind::Pattern, SectionId::FlatPattern, flat, &graphs)) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, sizeof(PatternPayload)) || flat.count != 1) {
        return false;
    }

    PatternPayload payload{};
    std::memcpy(&payload, flat.data, sizeof(payload));
    applyPatternPayload(payload, target);
    return applyGraphSections(graphs[0], target);
}

FLASHMEM EnvelopeEncodeResult fillProjectSequencerEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint16_t capacity
) {
    EnvelopeWriter writer(out, capacity, EnvelopeKind::ProjectSequencer);
    ProjectSequencerPayload flat{};
    fillProjectSequencerPayload(trackBank, active, flat);
    writer.addSection(SectionId::FlatProjectSequencer,
                      kNoTrack,
                      sizeof(ProjectSequencerPayload),
                      1,
                      &flat,
                      sizeof(flat));
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        addGraphSections(writer, state::sequencer::graphView(sourceTrack(trackBank, active, i)), i);
    }
    return writer.finish();
}

FLASHMEM bool applyProjectSequencerEnvelope(const uint8_t* data,
                                            uint16_t size,
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
    if (!sectionHasExactRecordShape(flat, sizeof(ProjectSequencerPayload)) || flat.count != 1) {
        return false;
    }

    ProjectSequencerPayload payload{};
    std::memcpy(&payload, flat.data, sizeof(payload));
    applyProjectSequencerPayload(payload, trackBank, active);

    const uint8_t activeTrack =
        state::sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        applyGraphSections(graphs[i], trackBank.track(i));
    }
    applyGraphSections(graphs[activeTrack], active.pattern);
    return true;
}

FLASHMEM EnvelopeEncodeResult fillSetEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint16_t capacity
) {
    EnvelopeWriter writer(out, capacity, EnvelopeKind::Set);
    SetPayload flat{};
    fillSetPayload(trackBank, active, flat);
    writer.addSection(SectionId::FlatSet,
                      kNoTrack,
                      sizeof(SetPayload),
                      1,
                      &flat,
                      sizeof(flat));
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        addGraphSections(writer, state::sequencer::graphView(sourceTrack(trackBank, active, i)), i);
    }
    return writer.finish();
}

FLASHMEM bool applySetEnvelope(const uint8_t* data,
                               uint16_t size,
                               state::sequencer::SequencerTrackBankState& trackBank,
                               state::sequencer::SequencerState& active) {
    SectionView flat{};
    std::array<GraphSectionViews, PERSISTED_TRACK_COUNT> graphs{};
    if (!findSections(data, size, EnvelopeKind::Set, SectionId::FlatSet, flat, &graphs)) {
        return false;
    }
    if (!sectionHasExactRecordShape(flat, sizeof(SetPayload)) || flat.count != 1) {
        return false;
    }

    SetPayload payload{};
    std::memcpy(&payload, flat.data, sizeof(payload));
    applySetPayload(payload, trackBank, active);

    const uint8_t activeTrack =
        state::sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    for (uint8_t i = 0; i < PERSISTED_TRACK_COUNT; ++i) {
        applyGraphSections(graphs[i], trackBank.track(i));
    }
    applyGraphSections(graphs[activeTrack], active.pattern);
    return true;
}

}  // namespace core::persistence::sequencer_codec

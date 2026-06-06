#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../../src/persistence/PersistenceSlotFileStore.hpp"
#include "../../src/persistence/SequencerPersistenceEnvelope.hpp"
#include "../../src/persistence/SequencerPersistence.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/MemoryStorage.hpp"

namespace {
using test_support::MemoryStorage;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;

bool sameScale(const StepSequencerScaleSettings& lhs, const StepSequencerScaleSettings& rhs) {
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

#pragma pack(push, 1)
struct SlotFileHeaderRaw {
    uint32_t magic = 0;
    uint8_t formatVersion = 0;
    uint8_t domainVersion = 0;
    uint16_t slotCount = 0;
    uint16_t slotPayloadSize = 0;
    uint16_t reserved0 = 0;
    uint32_t layoutCrc32 = 0;
    uint32_t reserved1 = 0;
    uint32_t reserved2 = 0;
};

struct EnvelopeHeaderRaw {
    uint32_t magic = 0;
    uint8_t version = 0;
    uint8_t kind = 0;
    uint16_t headerSize = 0;
    uint16_t sectionCount = 0;
    uint16_t reserved0 = 0;
};

struct EnvelopeSectionHeaderRaw {
    uint16_t id = 0;
    uint8_t track = 0;
    uint8_t reserved0 = 0;
    uint16_t recordSize = 0;
    uint16_t count = 0;
    uint16_t byteSize = 0;
};
#pragma pack(pop)

static_assert(sizeof(SlotFileHeaderRaw) == 24, "Unexpected slot file header size");
static_assert(sizeof(EnvelopeHeaderRaw) == 12, "Unexpected envelope header size");
static_assert(sizeof(EnvelopeSectionHeaderRaw) == 10, "Unexpected envelope section header size");

constexpr uint16_t kEnvelopeSectionGraphStepNodes = 17;
constexpr uint16_t kEnvelopeSectionUnknownFuture = 0x7F00;
constexpr uint16_t kStepNodeRecordSize = 14;
constexpr uint16_t kStepNodeChildSequenceOffset = 10;
constexpr uint16_t kStepNodeCycleSetOffset = 12;

uint32_t workspaceSlotPayloadAddress(MemoryStorage& storage, uint16_t slotIndex) {
    SlotFileHeaderRaw header{};
    const size_t readBytes = storage.read(0, reinterpret_cast<uint8_t*>(&header), sizeof(header));
    assert(readBytes == sizeof(header));
    assert(slotIndex < header.slotCount);

    constexpr uint32_t SLOT_HEADER_SIZE = 16;
    const uint32_t slotSize = SLOT_HEADER_SIZE + header.slotPayloadSize;
    return static_cast<uint32_t>(sizeof(header)) +
           static_cast<uint32_t>(slotIndex) * slotSize +
           SLOT_HEADER_SIZE;
}

void configurePattern(core::state::sequencer::SequencerState& sequencer,
                      uint8_t length,
                      uint8_t stepsPerBeat,
                      uint8_t midiChannel,
                      uint8_t focusedStep,
                      core::state::sequencer::StepProperty property) {
    sequencer.reset();
    sequencer.pattern.length.set(length);
    sequencer.pattern.stepsPerBeat.set(stepsPerBeat);
    sequencer.pattern.midiChannel.set(midiChannel);
    sequencer.pattern.enabledMask.set({});

    sequencer.setStepDataAt(0, 60, 110, 95);
    sequencer.setStepDataAt(3, 72, 90, 60);
    sequencer.setStepDataAt(7, 45, 127, 120);
    sequencer.setStepProbabilityAt(0, 100);
    sequencer.setStepProbabilityAt(3, 65);
    sequencer.setStepProbabilityAt(7, 25);

    sequencer.pattern.toggle(0);
    sequencer.pattern.toggle(3);
    sequencer.pattern.toggle(7);

    if (length > 64) {
        const uint8_t lastStep = static_cast<uint8_t>(length - 1);
        sequencer.setStepDataAt(lastStep, 50, 77, 33);
        sequencer.setStepProbabilityAt(lastStep, 88);
        sequencer.pattern.toggle(lastStep);
    }

    sequencer.focusedStep.set(focusedStep);
    sequencer.page.set(sequencer.pageForStep(focusedStep));
    sequencer.activeStepProperty.set(property);
}

void prepareTrackBank(core::state::sequencer::SequencerTrackBankState& trackBank,
                      const core::state::sequencer::SequencerState& active) {
    core::state::sequencer::initializeTrackBankFromActive(trackBank, active);
}

void assertPatternEquals(const core::state::sequencer::SequencerState& sequencer,
                         uint8_t expectedLength,
                         uint8_t expectedSpb,
                         uint8_t expectedChannel) {
    assert(sequencer.pattern.length.get() == expectedLength);
    assert(sequencer.pattern.stepsPerBeat.get() == expectedSpb);
    assert(sequencer.pattern.midiChannel.get() == expectedChannel);

    assert(sequencer.pattern.isEnabled(0));
    assert(sequencer.pattern.isEnabled(3));
    assert(sequencer.pattern.isEnabled(7));

    assert(sequencer.pattern.note[0] == 60);
    assert(sequencer.pattern.velocity[0] == 110);
    assert(sequencer.pattern.gate[0] == 95);
    assert(sequencer.pattern.probability[0] == 100);

    assert(sequencer.pattern.note[3] == 72);
    assert(sequencer.pattern.velocity[3] == 90);
    assert(sequencer.pattern.gate[3] == 60);
    assert(sequencer.pattern.probability[3] == 65);

    assert(sequencer.pattern.note[7] == 45);
    assert(sequencer.pattern.velocity[7] == 127);
    assert(sequencer.pattern.gate[7] == 120);
    assert(sequencer.pattern.probability[7] == 25);

    if (expectedLength > 64) {
        const uint8_t lastStep = static_cast<uint8_t>(expectedLength - 1);
        assert(sequencer.pattern.isEnabled(lastStep));
        assert(sequencer.pattern.note[lastStep] == 50);
        assert(sequencer.pattern.velocity[lastStep] == 77);
        assert(sequencer.pattern.gate[lastStep] == 33);
        assert(sequencer.pattern.probability[lastStep] == 88);
    }
}

void addGraphContent(core::state::sequencer::SequencerPatternState& pattern) {
    using namespace core::state::sequencer;

    const auto micro = createMicroSequence(pattern, rootStepNodeId(2), 3);
    assert(micro.ok);
    const auto* graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    assert(setNodeNoteOffset(pattern, static_cast<uint16_t>(sequence->firstStepNode + 1), 5));

    const auto cycle = createCycleStateSet(pattern, rootStepNodeId(4), 2);
    assert(cycle.ok);
    graph = graphView(pattern);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    assert(setNodeEnabledOverride(pattern, cycleSet->firstStateNode, false));
}

void assertGraphContent(const core::state::sequencer::SequencerPatternState& pattern) {
    using namespace core::state::sequencer;

    const auto* graph = graphView(pattern);
    assert(graph != nullptr);
    assert(graph->enabled);
    assert(graph->rootSequenceId == 0);
    assert(graph->sequenceCount >= 2);
    assert(graph->cycleSetCount >= 1);

    const auto* rootTwo = graph->stepNode(rootStepNodeId(2));
    assert(rootTwo != nullptr);
    assert(rootTwo->has(STEP_NODE_CHILD_SEQUENCE));
    const auto* childSequence = graph->sequence(rootTwo->childSequenceId);
    assert(childSequence != nullptr);
    assert(childSequence->length == 3);
    const auto* childNode = graph->stepNode(static_cast<uint16_t>(childSequence->firstStepNode + 1));
    assert(childNode != nullptr);
    assert(childNode->has(STEP_NODE_NOTE_OFFSET));
    assert(childNode->noteOffset == 5);

    const auto* rootFour = graph->stepNode(rootStepNodeId(4));
    assert(rootFour != nullptr);
    assert(rootFour->has(STEP_NODE_CYCLE_SET));
    const auto* cycleSet = graph->cycleSet(rootFour->cycleSetId);
    assert(cycleSet != nullptr);
    assert(cycleSet->length == 2);
}

const EnvelopeSectionHeaderRaw* findEnvelopeSection(const uint8_t* data,
                                                    uint16_t size,
                                                    uint16_t sectionId,
                                                    uint16_t& sectionOffset) {
    if (data == nullptr || size < sizeof(EnvelopeHeaderRaw)) return nullptr;

    EnvelopeHeaderRaw header{};
    std::memcpy(&header, data, sizeof(header));
    uint16_t offset = header.headerSize;
    for (uint16_t i = 0; i < header.sectionCount; ++i) {
        if (offset > size ||
            sizeof(EnvelopeSectionHeaderRaw) > static_cast<uint16_t>(size - offset)) {
            return nullptr;
        }

        const auto* section =
            reinterpret_cast<const EnvelopeSectionHeaderRaw*>(data + offset);
        const uint16_t payloadOffset =
            static_cast<uint16_t>(offset + sizeof(EnvelopeSectionHeaderRaw));
        if (section->id == sectionId) {
            sectionOffset = offset;
            return section;
        }
        if (section->byteSize > static_cast<uint16_t>(size - payloadOffset)) {
            return nullptr;
        }
        offset = static_cast<uint16_t>(payloadOffset + section->byteSize);
    }

    return nullptr;
}

void test_workspace_roundtrip() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    core::state::sequencer::SequencerTrackBankState sourceTrackBank;
    configurePattern(source, 96, 4, 2, 73, core::state::sequencer::StepProperty::VELOCITY);
    prepareTrackBank(sourceTrackBank, source);
    assert(persistence.saveWorkspace(sourceTrackBank, source));

    core::state::sequencer::SequencerState loaded;
    core::state::sequencer::SequencerTrackBankState loadedTrackBank;
    loaded.reset();
    loadedTrackBank.reset();
    assert(persistence.loadWorkspace(loadedTrackBank, loaded));

    assertPatternEquals(loaded, 96, 4, 2);
    assert(loaded.focusedStep.get() == 73);
    assert(loaded.page.get() == loaded.pageForStep(73));
    assert(loaded.activeStepProperty.get() == core::state::sequencer::StepProperty::VELOCITY);

    std::cout << "[PASS] test_workspace_roundtrip\n";
}

void test_workspace_load_latest_after_multiple_saves() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState first;
    core::state::sequencer::SequencerTrackBankState firstTrackBank;
    configurePattern(first, 8, 2, 1, 3, core::state::sequencer::StepProperty::NOTE);
    prepareTrackBank(firstTrackBank, first);
    assert(persistence.saveWorkspace(firstTrackBank, first));

    core::state::sequencer::SequencerState second;
    core::state::sequencer::SequencerTrackBankState secondTrackBank;
    configurePattern(second, 96, 8, 7, 70, core::state::sequencer::StepProperty::GATE);
    prepareTrackBank(secondTrackBank, second);
    assert(persistence.saveWorkspace(secondTrackBank, second));

    core::state::sequencer::SequencerState loaded;
    core::state::sequencer::SequencerTrackBankState loadedTrackBank;
    loaded.reset();
    loadedTrackBank.reset();
    assert(persistence.loadWorkspace(loadedTrackBank, loaded));

    assertPatternEquals(loaded, 96, 8, 7);
    assert(loaded.focusedStep.get() == 70);
    assert(loaded.activeStepProperty.get() == core::state::sequencer::StepProperty::GATE);

    std::cout << "[PASS] test_workspace_load_latest_after_multiple_saves\n";
}

void test_workspace_falls_back_when_latest_slot_is_corrupted() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState first;
    core::state::sequencer::SequencerTrackBankState firstTrackBank;
    configurePattern(first, 8, 2, 1, 3, core::state::sequencer::StepProperty::NOTE);
    prepareTrackBank(firstTrackBank, first);
    assert(persistence.saveWorkspace(firstTrackBank, first));

    core::state::sequencer::SequencerState second;
    core::state::sequencer::SequencerTrackBankState secondTrackBank;
    configurePattern(second, 96, 8, 7, 70, core::state::sequencer::StepProperty::GATE);
    prepareTrackBank(secondTrackBank, second);
    assert(persistence.saveWorkspace(secondTrackBank, second));

    // Two-slot workspace journal: second save lands in slot 1.
    const uint32_t latestPayloadAddress =
        workspaceSlotPayloadAddress(workspaceStorage, 1) +
        static_cast<uint32_t>(offsetof(
            core::persistence::sequencer_codec::WorkspacePayload,
            tracks
        ));
    const uint8_t badByte = 0x00;
    const size_t written = workspaceStorage.write(latestPayloadAddress, &badByte, 1);
    assert(written == 1);

    core::state::sequencer::SequencerState loaded;
    core::state::sequencer::SequencerTrackBankState loadedTrackBank;
    loaded.reset();
    loadedTrackBank.reset();
    assert(persistence.loadWorkspace(loadedTrackBank, loaded));

    // Must fall back to older valid slot (first save).
    assertPatternEquals(loaded, 8, 2, 1);
    assert(loaded.focusedStep.get() == 3);
    assert(loaded.activeStepProperty.get() == core::state::sequencer::StepProperty::NOTE);

    std::cout << "[PASS] test_workspace_falls_back_when_latest_slot_is_corrupted\n";
}

void test_workspace_masks_enabled_bits_outside_length() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    core::state::sequencer::SequencerTrackBankState sourceTrackBank;
    source.reset();
    source.pattern.length.set(8);
    source.pattern.enabledMask.set(StepBitMask128::fromLower64(
        (1ULL << 0) | (1ULL << 7) | (1ULL << 9) | (1ULL << 15)
    ));

    prepareTrackBank(sourceTrackBank, source);
    assert(persistence.saveWorkspace(sourceTrackBank, source));

    core::state::sequencer::SequencerState loaded;
    core::state::sequencer::SequencerTrackBankState loadedTrackBank;
    loaded.reset();
    loadedTrackBank.reset();
    assert(persistence.loadWorkspace(loadedTrackBank, loaded));

    const uint64_t expectedMask = (1ULL << 0) | (1ULL << 7);
    assert(loaded.pattern.length.get() == 8);
    assert(loaded.pattern.enabledMask.get().lower64() == expectedMask);

    std::cout << "[PASS] test_workspace_masks_enabled_bits_outside_length\n";
}

void test_pattern_library_save_load_erase() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    configurePattern(source, 104, 4, 5, 95, core::state::sequencer::StepProperty::NOTE);
    assert(persistence.savePatternSlot(5, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    const auto status = persistence.loadPatternSlot(5, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assertPatternEquals(loaded, 104, 4, 5);

    assert(persistence.erasePatternSlot(5));
    const auto emptyStatus = persistence.loadPatternSlot(5, loaded);
    assert(emptyStatus == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_pattern_library_save_load_erase\n";
}

void test_pattern_library_graph_roundtrip() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    addGraphContent(source.pattern);
    assert(persistence.savePatternSlot(7, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(persistence.loadPatternSlot(7, loaded) == core::persistence::SlotLoadStatus::OK);
    assertPatternEquals(loaded, 16, 4, 1);
    assertGraphContent(loaded.pattern);

    std::cout << "[PASS] test_pattern_library_graph_roundtrip\n";
}

void test_pattern_library_flat_pattern_does_not_allocate_graph() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    assert(core::state::sequencer::graphView(source.pattern) == nullptr);
    assert(persistence.savePatternSlot(11, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(persistence.loadPatternSlot(11, loaded) == core::persistence::SlotLoadStatus::OK);
    assertPatternEquals(loaded, 16, 4, 1);
    assert(core::state::sequencer::graphView(loaded.pattern) == nullptr);

    std::cout << "[PASS] test_pattern_library_flat_pattern_does_not_allocate_graph\n";
}

void test_pattern_envelope_rejects_incompatible_header() {
    core::state::sequencer::SequencerState source;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);

    core::persistence::sequencer_codec::EnvelopeBuffer buffer{};
    const auto encoded = core::persistence::sequencer_codec::fillPatternEnvelope(
        source.pattern,
        buffer.bytes.data(),
        core::persistence::sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    assert(encoded.ok);

    core::state::sequencer::SequencerState loaded;
    loaded.reset();

    const uint8_t originalMagic = buffer.bytes[0];
    buffer.bytes[0] = 0x00;
    assert(!core::persistence::sequencer_codec::applyPatternEnvelope(
        buffer.bytes.data(),
        encoded.size,
        loaded.pattern
    ));

    buffer.bytes[0] = originalMagic;
    const uint8_t originalVersion = buffer.bytes[4];
    buffer.bytes[4] = static_cast<uint8_t>(originalVersion + 1);
    assert(!core::persistence::sequencer_codec::applyPatternEnvelope(
        buffer.bytes.data(),
        encoded.size,
        loaded.pattern
    ));

    std::cout << "[PASS] test_pattern_envelope_rejects_incompatible_header\n";
}

void test_pattern_envelope_ignores_unknown_future_section() {
    core::state::sequencer::SequencerState source;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    addGraphContent(source.pattern);

    core::persistence::sequencer_codec::EnvelopeBuffer buffer{};
    const auto encoded = core::persistence::sequencer_codec::fillPatternEnvelope(
        source.pattern,
        buffer.bytes.data(),
        core::persistence::sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    assert(encoded.ok);
    assert(static_cast<uint32_t>(encoded.size) + sizeof(EnvelopeSectionHeaderRaw) <=
           core::persistence::sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE);

    EnvelopeSectionHeaderRaw futureSection{};
    futureSection.id = kEnvelopeSectionUnknownFuture;
    futureSection.track = 0;
    futureSection.recordSize = 0;
    futureSection.count = 0;
    futureSection.byteSize = 0;
    std::memcpy(buffer.bytes.data() + encoded.size, &futureSection, sizeof(futureSection));

    auto* header = reinterpret_cast<EnvelopeHeaderRaw*>(buffer.bytes.data());
    header->sectionCount = static_cast<uint16_t>(header->sectionCount + 1);
    const uint16_t extendedSize = static_cast<uint16_t>(encoded.size + sizeof(futureSection));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(core::persistence::sequencer_codec::applyPatternEnvelope(
        buffer.bytes.data(),
        extendedSize,
        loaded.pattern
    ));
    assertPatternEquals(loaded, 16, 4, 1);
    assertGraphContent(loaded.pattern);

    std::cout << "[PASS] test_pattern_envelope_ignores_unknown_future_section\n";
}

void test_pattern_envelope_ignores_invalid_graph_section_but_keeps_flat_pattern() {
    core::state::sequencer::SequencerState source;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    addGraphContent(source.pattern);

    core::persistence::sequencer_codec::EnvelopeBuffer buffer{};
    const auto encoded = core::persistence::sequencer_codec::fillPatternEnvelope(
        source.pattern,
        buffer.bytes.data(),
        core::persistence::sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    assert(encoded.ok);

    constexpr uint16_t envelopeHeaderSize = 12;
    constexpr uint16_t sectionHeaderSize = 10;
    const uint16_t firstGraphSectionOffset = static_cast<uint16_t>(
        envelopeHeaderSize +
        sectionHeaderSize +
        sizeof(core::persistence::sequencer_codec::PatternPayload)
    );
    assert(static_cast<uint16_t>(firstGraphSectionOffset + 5) < encoded.size);

    // Corrupt graph metadata only. The flat pattern should remain loadable.
    buffer.bytes[firstGraphSectionOffset + 4] = 1;
    buffer.bytes[firstGraphSectionOffset + 5] = 0;

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(core::persistence::sequencer_codec::applyPatternEnvelope(
        buffer.bytes.data(),
        encoded.size,
        loaded.pattern
    ));
    assertPatternEquals(loaded, 16, 4, 1);
    assert(core::state::sequencer::graphView(loaded.pattern) == nullptr);

    std::cout << "[PASS] test_pattern_envelope_ignores_invalid_graph_section_but_keeps_flat_pattern\n";
}

void test_pattern_envelope_sanitizes_broken_graph_links() {
    core::state::sequencer::SequencerState source;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    addGraphContent(source.pattern);

    core::persistence::sequencer_codec::EnvelopeBuffer buffer{};
    const auto encoded = core::persistence::sequencer_codec::fillPatternEnvelope(
        source.pattern,
        buffer.bytes.data(),
        core::persistence::sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    assert(encoded.ok);

    uint16_t stepSectionOffset = 0;
    const auto* stepSection = findEnvelopeSection(
        buffer.bytes.data(),
        encoded.size,
        kEnvelopeSectionGraphStepNodes,
        stepSectionOffset
    );
    assert(stepSection != nullptr);
    assert(stepSection->recordSize == kStepNodeRecordSize);
    assert(stepSection->count > 4);

    const uint16_t stepPayloadOffset =
        static_cast<uint16_t>(stepSectionOffset + sizeof(EnvelopeSectionHeaderRaw));
    uint16_t invalidId = 0x7FFF;
    std::memcpy(buffer.bytes.data() +
                    stepPayloadOffset +
                    2 * kStepNodeRecordSize +
                    kStepNodeChildSequenceOffset,
                &invalidId,
                sizeof(invalidId));
    std::memcpy(buffer.bytes.data() +
                    stepPayloadOffset +
                    4 * kStepNodeRecordSize +
                    kStepNodeCycleSetOffset,
                &invalidId,
                sizeof(invalidId));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(core::persistence::sequencer_codec::applyPatternEnvelope(
        buffer.bytes.data(),
        encoded.size,
        loaded.pattern
    ));
    assertPatternEquals(loaded, 16, 4, 1);

    const auto* graph = core::state::sequencer::graphView(loaded.pattern);
    assert(graph != nullptr);
    const auto* rootTwo = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(rootTwo != nullptr);
    assert(!rootTwo->has(STEP_NODE_CHILD_SEQUENCE));
    const auto* rootFour = graph->stepNode(core::state::sequencer::rootStepNodeId(4));
    assert(rootFour != nullptr);
    assert(!rootFour->has(STEP_NODE_CYCLE_SET));

    std::cout << "[PASS] test_pattern_envelope_sanitizes_broken_graph_links\n";
}

void test_pattern_library_masks_enabled_bits_outside_length() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    source.reset();
    source.pattern.length.set(16);
    source.pattern.enabledMask.set(StepBitMask128::fromLower64(
        (1ULL << 0) | (1ULL << 5) | (1ULL << 20) | (1ULL << 63)
    ));

    assert(persistence.savePatternSlot(9, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    const auto status = persistence.loadPatternSlot(9, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);

    const uint64_t expectedMask = (1ULL << 0) | (1ULL << 5);
    assert(loaded.pattern.length.get() == 16);
    assert(loaded.pattern.enabledMask.get().lower64() == expectedMask);

    std::cout << "[PASS] test_pattern_library_masks_enabled_bits_outside_length\n";
}

void test_set_library_save_load_erase() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    core::state::sequencer::SequencerTrackBankState sourceTrackBank;
    configurePattern(source, 88, 2, 9, 65, core::state::sequencer::StepProperty::NOTE);
    prepareTrackBank(sourceTrackBank, source);
    assert(persistence.saveSetSlot(3, sourceTrackBank, source));

    core::state::sequencer::SequencerState loaded;
    core::state::sequencer::SequencerTrackBankState loadedTrackBank;
    loaded.reset();
    loadedTrackBank.reset();
    const auto status = persistence.loadSetSlot(3, loadedTrackBank, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assertPatternEquals(loaded, 88, 2, 9);

    assert(persistence.eraseSetSlot(3));
    const auto emptyStatus = persistence.loadSetSlot(3, loadedTrackBank, loaded);
    assert(emptyStatus == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_set_library_save_load_erase\n";
}

void test_set_library_graph_roundtrip_for_active_and_bank_tracks() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState source;
    core::state::sequencer::SequencerTrackBankState sourceTrackBank;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    prepareTrackBank(sourceTrackBank, source);
    sourceTrackBank.syncSharedTrackState(0x0003, 1);
    addGraphContent(source.pattern);
    addGraphContent(sourceTrackBank.track(0));

    assert(persistence.saveSetSlot(8, sourceTrackBank, source));

    core::state::sequencer::SequencerState loaded;
    core::state::sequencer::SequencerTrackBankState loadedTrackBank;
    loaded.reset();
    loadedTrackBank.reset();
    assert(persistence.loadSetSlot(8, loadedTrackBank, loaded) ==
           core::persistence::SlotLoadStatus::OK);
    assert(loadedTrackBank.activeTrackIndex() == 1);
    assertGraphContent(loaded.pattern);
    assertGraphContent(loadedTrackBank.track(0));

    std::cout << "[PASS] test_set_library_graph_roundtrip_for_active_and_bank_tracks\n";
}

void test_library_bounds() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    sequencer.reset();
    trackBank.reset();

    const uint8_t invalidPatternSlot =
        static_cast<uint8_t>(core::persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT);
    const uint8_t invalidSetSlot =
        static_cast<uint8_t>(core::persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT);

    assert(!persistence.savePatternSlot(invalidPatternSlot, sequencer));
    assert(!persistence.saveSetSlot(invalidSetSlot, trackBank, sequencer));

    assert(persistence.loadPatternSlot(invalidPatternSlot, sequencer) ==
           core::persistence::SlotLoadStatus::OUT_OF_RANGE);
    assert(persistence.loadSetSlot(invalidSetSlot, trackBank, sequencer) ==
           core::persistence::SlotLoadStatus::OUT_OF_RANGE);

    assert(!persistence.erasePatternSlot(invalidPatternSlot));
    assert(!persistence.eraseSetSlot(invalidSetSlot));

    std::cout << "[PASS] test_library_bounds\n";
}

void test_scale_settings_roundtrip_across_workspace_pattern_and_set() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.init());

    const StepSequencerScaleSettings projectScale{
        .root = 3,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    const StepSequencerScaleSettings overrideScale{
        .root = 10,
        .type = StepSequencerScaleType::NaturalMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };

    core::state::sequencer::SequencerState source;
    core::state::sequencer::SequencerTrackBankState sourceTrackBank;
    configurePattern(source, 16, 4, 1, 0, core::state::sequencer::StepProperty::NOTE);
    assert(source.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE
    ));
    assert(source.setPatternScaleOverride(overrideScale));
    assert(source.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES));
    prepareTrackBank(sourceTrackBank, source);
    assert(sourceTrackBank.setProjectScaleSettings(projectScale));

    assert(persistence.saveWorkspace(sourceTrackBank, source));
    assert(persistence.savePatternSlot(2, source));
    assert(persistence.saveSetSlot(3, sourceTrackBank, source));

    core::state::sequencer::SequencerState loadedWorkspace;
    core::state::sequencer::SequencerTrackBankState loadedWorkspaceTrackBank;
    loadedWorkspace.reset();
    loadedWorkspaceTrackBank.reset();
    assert(persistence.loadWorkspace(loadedWorkspaceTrackBank, loadedWorkspace));
    assert(sameScale(loadedWorkspaceTrackBank.projectScaleSettings(), projectScale));
    assert(loadedWorkspace.pattern.scalePolicy == core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    assert(sameScale(loadedWorkspace.pattern.scaleOverride, overrideScale));
    assert(loadedWorkspace.pattern.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);

    core::state::sequencer::SequencerState loadedPattern;
    loadedPattern.reset();
    assert(persistence.loadPatternSlot(2, loadedPattern) == core::persistence::SlotLoadStatus::OK);
    assert(loadedPattern.pattern.scalePolicy == core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    assert(sameScale(loadedPattern.pattern.scaleOverride, overrideScale));
    assert(loadedPattern.pattern.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);

    core::state::sequencer::SequencerState loadedSet;
    core::state::sequencer::SequencerTrackBankState loadedSetTrackBank;
    loadedSet.reset();
    loadedSetTrackBank.reset();
    assert(persistence.loadSetSlot(3, loadedSetTrackBank, loadedSet) ==
           core::persistence::SlotLoadStatus::OK);
    assert(sameScale(loadedSetTrackBank.projectScaleSettings(), projectScale));
    assert(loadedSet.pattern.scalePolicy == core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    assert(sameScale(loadedSet.pattern.scaleOverride, overrideScale));
    assert(loadedSet.pattern.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);

    std::cout << "[PASS] test_scale_settings_roundtrip_across_workspace_pattern_and_set\n";
}

void test_write_status_reports_commit_failure_and_out_of_range() {
    MemoryStorage workspaceStorage;
    MemoryStorage patternStorage;
    MemoryStorage setStorage;
    workspaceStorage.init();
    patternStorage.init();
    setStorage.init();

    core::persistence::SequencerPersistence persistence(workspaceStorage, patternStorage, setStorage);
    assert(persistence.initStatus() == core::persistence::PersistenceWriteStatus::OK);

    core::state::sequencer::SequencerState sequencer;
    sequencer.reset();

    patternStorage.setFaultMode(MemoryStorage::FaultMode::COMMIT_FAIL);
    assert(persistence.savePatternSlotStatus(2, sequencer) ==
           core::persistence::PersistenceWriteStatus::COMMIT_FAILED);

    const uint8_t invalidPatternSlot =
        static_cast<uint8_t>(core::persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT);
    assert(persistence.savePatternSlotStatus(invalidPatternSlot, sequencer) ==
           core::persistence::PersistenceWriteStatus::OUT_OF_RANGE);
    assert(persistence.eraseSetSlotStatus(
               static_cast<uint8_t>(core::persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT)
           ) == core::persistence::PersistenceWriteStatus::OUT_OF_RANGE);

    std::cout << "[PASS] test_write_status_reports_commit_failure_and_out_of_range\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "SequencerPersistence tests\n";
    std::cout << "==============================================\n\n";

    test_workspace_roundtrip();
    test_workspace_load_latest_after_multiple_saves();
    test_workspace_falls_back_when_latest_slot_is_corrupted();
    test_workspace_masks_enabled_bits_outside_length();
    test_pattern_library_save_load_erase();
    test_pattern_library_graph_roundtrip();
    test_pattern_library_flat_pattern_does_not_allocate_graph();
    test_pattern_envelope_rejects_incompatible_header();
    test_pattern_envelope_ignores_unknown_future_section();
    test_pattern_envelope_ignores_invalid_graph_section_but_keeps_flat_pattern();
    test_pattern_envelope_sanitizes_broken_graph_links();
    test_pattern_library_masks_enabled_bits_outside_length();
    test_set_library_save_load_erase();
    test_set_library_graph_roundtrip_for_active_and_bank_tracks();
    test_library_bounds();
    test_scale_settings_roundtrip_across_workspace_pattern_and_set();
    test_write_status_reports_commit_failure_and_out_of_range();
    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

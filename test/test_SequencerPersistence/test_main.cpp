#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/persistence/SequencerPersistence.hpp"

namespace {

class MemoryStorage : public oc::interface::IStorage {
public:
    explicit MemoryStorage(size_t capacity = 64 * 1024)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memcpy(buffer, data_.data() + address, n);
        return n;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memcpy(data_.data() + address, buffer, n);
        return n;
    }

    bool commit() override { return true; }

    bool erase(uint32_t address, size_t size) override {
        if (address >= data_.size()) return false;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memset(data_.data() + address, 0xFF, n);
        return true;
    }

    size_t capacity() const override { return data_.size(); }

private:
    bool initialized_ = false;
    std::vector<uint8_t> data_;
};

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
#pragma pack(pop)

static_assert(sizeof(SlotFileHeaderRaw) == 24, "Unexpected slot file header size");

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
    sequencer.length.set(length);
    sequencer.stepsPerBeat.set(stepsPerBeat);
    sequencer.midiChannel.set(midiChannel);
    sequencer.enabledMask.set(0);

    sequencer.setStepDataAt(0, 60, 110, 95);
    sequencer.setStepDataAt(3, 72, 90, 60);
    sequencer.setStepDataAt(7, 45, 127, 120);
    sequencer.setStepProbabilityAt(0, 100);
    sequencer.setStepProbabilityAt(3, 65);
    sequencer.setStepProbabilityAt(7, 25);

    sequencer.toggle(0);
    sequencer.toggle(3);
    sequencer.toggle(7);

    sequencer.focusedStep.set(focusedStep);
    sequencer.page.set(sequencer.pageForStep(focusedStep));
    sequencer.activeStepProperty.set(property);
}

void assertPatternEquals(const core::state::sequencer::SequencerState& sequencer,
                         uint8_t expectedLength,
                         uint8_t expectedSpb,
                         uint8_t expectedChannel) {
    assert(sequencer.length.get() == expectedLength);
    assert(sequencer.stepsPerBeat.get() == expectedSpb);
    assert(sequencer.midiChannel.get() == expectedChannel);

    assert(sequencer.isEnabled(0));
    assert(sequencer.isEnabled(3));
    assert(sequencer.isEnabled(7));

    assert(sequencer.note[0] == 60);
    assert(sequencer.velocity[0] == 110);
    assert(sequencer.gate[0] == 95);
    assert(sequencer.probability[0] == 100);

    assert(sequencer.note[3] == 72);
    assert(sequencer.velocity[3] == 90);
    assert(sequencer.gate[3] == 60);
    assert(sequencer.probability[3] == 65);

    assert(sequencer.note[7] == 45);
    assert(sequencer.velocity[7] == 127);
    assert(sequencer.gate[7] == 120);
    assert(sequencer.probability[7] == 25);
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
    configurePattern(source, 16, 4, 2, 9, core::state::sequencer::StepProperty::VELOCITY);
    assert(persistence.saveWorkspace(source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(persistence.loadWorkspace(loaded));

    assertPatternEquals(loaded, 16, 4, 2);
    assert(loaded.focusedStep.get() == 9);
    assert(loaded.page.get() == loaded.pageForStep(9));
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
    configurePattern(first, 8, 2, 1, 3, core::state::sequencer::StepProperty::NOTE);
    assert(persistence.saveWorkspace(first));

    core::state::sequencer::SequencerState second;
    configurePattern(second, 32, 8, 7, 12, core::state::sequencer::StepProperty::GATE);
    assert(persistence.saveWorkspace(second));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(persistence.loadWorkspace(loaded));

    assertPatternEquals(loaded, 32, 8, 7);
    assert(loaded.focusedStep.get() == 12);
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
    configurePattern(first, 8, 2, 1, 3, core::state::sequencer::StepProperty::NOTE);
    assert(persistence.saveWorkspace(first));

    core::state::sequencer::SequencerState second;
    configurePattern(second, 32, 8, 7, 12, core::state::sequencer::StepProperty::GATE);
    assert(persistence.saveWorkspace(second));

    // Two-slot workspace journal: second save lands in slot 1.
    const uint32_t latestPayloadAddress = workspaceSlotPayloadAddress(workspaceStorage, 1);
    const uint8_t badByte = 0x00;
    const size_t written = workspaceStorage.write(latestPayloadAddress, &badByte, 1);
    assert(written == 1);

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(persistence.loadWorkspace(loaded));

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
    source.reset();
    source.length.set(8);
    source.enabledMask.set((1ULL << 0) | (1ULL << 7) | (1ULL << 9) | (1ULL << 15));

    assert(persistence.saveWorkspace(source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    assert(persistence.loadWorkspace(loaded));

    const uint64_t expectedMask = (1ULL << 0) | (1ULL << 7);
    assert(loaded.length.get() == 8);
    assert(loaded.enabledMask.get() == expectedMask);

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
    configurePattern(source, 24, 4, 5, 6, core::state::sequencer::StepProperty::NOTE);
    assert(persistence.savePatternSlot(5, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    const auto status = persistence.loadPatternSlot(5, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assertPatternEquals(loaded, 24, 4, 5);

    assert(persistence.erasePatternSlot(5));
    const auto emptyStatus = persistence.loadPatternSlot(5, loaded);
    assert(emptyStatus == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_pattern_library_save_load_erase\n";
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
    source.length.set(16);
    source.enabledMask.set((1ULL << 0) | (1ULL << 5) | (1ULL << 20) | (1ULL << 63));

    assert(persistence.savePatternSlot(9, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    const auto status = persistence.loadPatternSlot(9, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);

    const uint64_t expectedMask = (1ULL << 0) | (1ULL << 5);
    assert(loaded.length.get() == 16);
    assert(loaded.enabledMask.get() == expectedMask);

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
    configurePattern(source, 12, 2, 9, 4, core::state::sequencer::StepProperty::NOTE);
    assert(persistence.saveSetSlot(3, source));

    core::state::sequencer::SequencerState loaded;
    loaded.reset();
    const auto status = persistence.loadSetSlot(3, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assertPatternEquals(loaded, 12, 2, 9);

    assert(persistence.eraseSetSlot(3));
    const auto emptyStatus = persistence.loadSetSlot(3, loaded);
    assert(emptyStatus == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_set_library_save_load_erase\n";
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
    sequencer.reset();

    const uint8_t invalidPatternSlot =
        static_cast<uint8_t>(core::persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT);
    const uint8_t invalidSetSlot =
        static_cast<uint8_t>(core::persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT);

    assert(!persistence.savePatternSlot(invalidPatternSlot, sequencer));
    assert(!persistence.saveSetSlot(invalidSetSlot, sequencer));

    assert(persistence.loadPatternSlot(invalidPatternSlot, sequencer) ==
           core::persistence::SlotLoadStatus::OUT_OF_RANGE);
    assert(persistence.loadSetSlot(invalidSetSlot, sequencer) ==
           core::persistence::SlotLoadStatus::OUT_OF_RANGE);

    assert(!persistence.erasePatternSlot(invalidPatternSlot));
    assert(!persistence.eraseSetSlot(invalidSetSlot));

    std::cout << "[PASS] test_library_bounds\n";
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
    test_pattern_library_masks_enabled_bits_outside_length();
    test_set_library_save_load_erase();
    test_library_bounds();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

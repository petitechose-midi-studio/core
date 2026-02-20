#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::persistence {

class SequencerPersistence {
public:
    static constexpr uint16_t WORKSPACE_SLOT_COUNT = 2;
    static constexpr uint16_t PATTERN_LIBRARY_SLOT_COUNT = 32;
    static constexpr uint16_t SET_LIBRARY_SLOT_COUNT = 16;

    static constexpr uint32_t WORKSPACE_MAGIC = 0x5357534B;  // "SWSK"
    static constexpr uint32_t PATTERN_LIBRARY_MAGIC = 0x53504C42;  // "SPLB"
    static constexpr uint32_t SET_LIBRARY_MAGIC = 0x53534554;  // "SSET"
    static constexpr uint8_t DATA_VERSION = 1;

    explicit SequencerPersistence(oc::interface::IStorage& workspaceStorage,
                                  oc::interface::IStorage& patternLibraryStorage,
                                  oc::interface::IStorage& setLibraryStorage)
        : workspace_store_(workspaceStorage,
                           {.fileMagic = WORKSPACE_MAGIC,
                            .domainVersion = DATA_VERSION,
                            .slotCount = WORKSPACE_SLOT_COUNT,
                            .slotPayloadSize = WORKSPACE_PAYLOAD_SIZE})
        , pattern_library_store_(patternLibraryStorage,
                                 {.fileMagic = PATTERN_LIBRARY_MAGIC,
                                  .domainVersion = DATA_VERSION,
                                  .slotCount = PATTERN_LIBRARY_SLOT_COUNT,
                                  .slotPayloadSize = PATTERN_PAYLOAD_SIZE})
        , set_library_store_(setLibraryStorage,
                             {.fileMagic = SET_LIBRARY_MAGIC,
                              .domainVersion = DATA_VERSION,
                              .slotCount = SET_LIBRARY_SLOT_COUNT,
                              .slotPayloadSize = SET_PAYLOAD_SIZE}) {}

    bool init() {
        if (!workspace_store_.init()) return false;
        if (!pattern_library_store_.init()) return false;
        if (!set_library_store_.init()) return false;

        uint8_t payload[WORKSPACE_PAYLOAD_SIZE] = {};
        const auto latest = workspace_store_.loadLatest(payload, sizeof(payload));
        if (latest.status == SlotLoadStatus::OK) {
            next_workspace_counter_ = latest.metadata.saveCounter + 1;
            next_workspace_slot_ = static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        } else {
            next_workspace_counter_ = 1;
            next_workspace_slot_ = 0;
        }

        return true;
    }

    bool loadWorkspace(state::sequencer::SequencerState& sequencer) {
        uint8_t payload[WORKSPACE_PAYLOAD_SIZE] = {};
        const auto latest = workspace_store_.loadLatest(payload, sizeof(payload));
        if (latest.status != SlotLoadStatus::OK) {
            return false;
        }

        WorkspacePayloadV1 snapshot{};
        std::memcpy(&snapshot, payload, sizeof(snapshot));
        applyWorkspacePayload_(snapshot, sequencer);

        next_workspace_counter_ = latest.metadata.saveCounter + 1;
        next_workspace_slot_ = static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        return true;
    }

    bool saveWorkspace(const state::sequencer::SequencerState& sequencer) {
        WorkspacePayloadV1 snapshot{};
        fillWorkspacePayload_(sequencer, snapshot);

        uint8_t payload[WORKSPACE_PAYLOAD_SIZE] = {};
        std::memcpy(payload, &snapshot, sizeof(snapshot));

        if (!workspace_store_.saveSlot(
                next_workspace_slot_,
                payload,
                sizeof(snapshot),
                next_workspace_counter_)) {
            return false;
        }

        next_workspace_counter_ += 1;
        next_workspace_slot_ = static_cast<uint16_t>((next_workspace_slot_ + 1) % WORKSPACE_SLOT_COUNT);
        return true;
    }

    bool savePatternSlot(uint8_t slotIndex, const state::sequencer::SequencerState& sequencer) {
        if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return false;

        PatternPayloadV1 payloadData{};
        fillPatternPayload_(sequencer, payloadData);

        uint8_t payload[PATTERN_PAYLOAD_SIZE] = {};
        std::memcpy(payload, &payloadData, sizeof(payloadData));

        const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
        return pattern_library_store_.saveSlot(slotIndex, payload, sizeof(payloadData), counter);
    }

    SlotLoadStatus loadPatternSlot(uint8_t slotIndex, state::sequencer::SequencerState& sequencer) {
        if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

        uint8_t payload[PATTERN_PAYLOAD_SIZE] = {};
        SlotMetadata metadata{};
        const SlotLoadStatus status =
            pattern_library_store_.loadSlot(slotIndex, payload, sizeof(payload), &metadata);
        if (status != SlotLoadStatus::OK) {
            return status;
        }

        if (metadata.payloadSize != sizeof(PatternPayloadV1)) {
            return SlotLoadStatus::HEADER_MISMATCH;
        }

        PatternPayloadV1 data{};
        std::memcpy(&data, payload, sizeof(data));
        applyPatternPayload_(data, sequencer);
        return SlotLoadStatus::OK;
    }

    bool erasePatternSlot(uint8_t slotIndex) {
        if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return false;
        return pattern_library_store_.eraseSlot(slotIndex);
    }

    bool saveSetSlot(uint8_t slotIndex, const state::sequencer::SequencerState& sequencer) {
        if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return false;

        SetPayloadV1 payloadData{};
        fillSetPayload_(sequencer, payloadData);

        uint8_t payload[SET_PAYLOAD_SIZE] = {};
        std::memcpy(payload, &payloadData, sizeof(payloadData));

        const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
        return set_library_store_.saveSlot(slotIndex, payload, sizeof(payloadData), counter);
    }

    SlotLoadStatus loadSetSlot(uint8_t slotIndex, state::sequencer::SequencerState& sequencer) {
        if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

        uint8_t payload[SET_PAYLOAD_SIZE] = {};
        SlotMetadata metadata{};
        const SlotLoadStatus status =
            set_library_store_.loadSlot(slotIndex, payload, sizeof(payload), &metadata);
        if (status != SlotLoadStatus::OK) {
            return status;
        }

        if (metadata.payloadSize != sizeof(SetPayloadV1)) {
            return SlotLoadStatus::HEADER_MISMATCH;
        }

        SetPayloadV1 data{};
        std::memcpy(&data, payload, sizeof(data));
        applySetPayload_(data, sequencer);
        return SlotLoadStatus::OK;
    }

    bool eraseSetSlot(uint8_t slotIndex) {
        if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return false;
        return set_library_store_.eraseSlot(slotIndex);
    }

private:
#pragma pack(push, 1)
    struct PatternPayloadV1 {
        uint8_t length = oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
        uint8_t stepsPerBeat = oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
        uint8_t midiChannel = oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED;
        uint8_t reserved0 = 0;
        uint64_t enabledMask = 0;
        std::array<uint8_t, state::sequencer::SequencerState::MAX_STEPS> note{};
        std::array<uint8_t, state::sequencer::SequencerState::MAX_STEPS> velocity{};
        std::array<uint16_t, state::sequencer::SequencerState::MAX_STEPS> gate{};
        std::array<int8_t, state::sequencer::SequencerState::MAX_STEPS> nudge{};
    };

    struct WorkspacePayloadV1 {
        PatternPayloadV1 pattern{};
        uint8_t page = 0;
        uint8_t focusedStep = 0;
        uint8_t activeStepProperty = static_cast<uint8_t>(state::sequencer::StepProperty::NOTE);
        uint8_t reserved0 = 0;
    };

    struct SetPayloadV1 {
        uint8_t trackCount = 1;
        uint8_t activeTrack = 0;
        uint16_t reserved0 = 0;
        PatternPayloadV1 track0{};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable<PatternPayloadV1>::value,
                  "PatternPayloadV1 must be trivially copyable");
    static_assert(std::is_trivially_copyable<WorkspacePayloadV1>::value,
                  "WorkspacePayloadV1 must be trivially copyable");
    static_assert(std::is_trivially_copyable<SetPayloadV1>::value,
                  "SetPayloadV1 must be trivially copyable");

    static constexpr uint16_t PATTERN_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(PatternPayloadV1));
    static constexpr uint16_t WORKSPACE_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(WorkspacePayloadV1));
    static constexpr uint16_t SET_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(SetPayloadV1));

    static uint8_t sanitizeLength_(uint8_t length) {
        if (length == 0 || length > state::sequencer::SequencerState::MAX_STEPS) {
            return oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
        }
        return length;
    }

    static uint64_t lengthMask_(uint8_t length) {
        if (length == 0) return 0;
        if (length >= state::sequencer::SequencerState::MAX_STEPS) return ~uint64_t{0};
        return (uint64_t{1} << length) - uint64_t{1};
    }

    static uint8_t sanitizeStepsPerBeat_(uint8_t spb) {
        if (spb == 0) {
            return oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
        }
        return spb;
    }

    static uint8_t sanitizeMidiChannel_(uint8_t channel) {
        return (channel > 15U)
                   ? oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED
                   : channel;
    }

    static uint8_t sanitizeMidi7_(uint8_t value) {
        return (value > 127U) ? 127U : value;
    }

    static uint16_t sanitizeGate_(uint16_t value) {
        return state::sequencer::SequencerState::clampGatePercent(value);
    }

    static state::sequencer::StepProperty sanitizeStepProperty_(uint8_t value) {
        if (value > static_cast<uint8_t>(state::sequencer::StepProperty::GATE)) {
            return state::sequencer::StepProperty::NOTE;
        }
        return static_cast<state::sequencer::StepProperty>(value);
    }

    static uint8_t sanitizeFocusedStep_(uint8_t focused, uint8_t length) {
        if (length == 0) return 0;
        return (focused >= length) ? static_cast<uint8_t>(length - 1) : focused;
    }

    static void fillPatternPayload_(const state::sequencer::SequencerState& source,
                                    PatternPayloadV1& out) {
        const uint8_t length = sanitizeLength_(source.length.get());
        out.length = length;
        out.stepsPerBeat = sanitizeStepsPerBeat_(source.stepsPerBeat.get());
        out.midiChannel = sanitizeMidiChannel_(source.midiChannel.get());
        out.enabledMask = source.enabledMask.get() & lengthMask_(length);

        for (uint8_t i = 0; i < state::sequencer::SequencerState::MAX_STEPS; ++i) {
            out.note[i] = sanitizeMidi7_(source.note[i]);
            out.velocity[i] = sanitizeMidi7_(source.velocity[i]);
            out.gate[i] = sanitizeGate_(source.gate[i]);
            out.nudge[i] = source.nudge[i];
        }
    }

    static void applyPatternPayload_(const PatternPayloadV1& payload,
                                     state::sequencer::SequencerState& target) {
        const uint8_t length = sanitizeLength_(payload.length);
        target.length.set(length);
        target.stepsPerBeat.set(sanitizeStepsPerBeat_(payload.stepsPerBeat));
        target.midiChannel.set(sanitizeMidiChannel_(payload.midiChannel));
        target.enabledMask.set(payload.enabledMask & lengthMask_(length));

        for (uint8_t i = 0; i < state::sequencer::SequencerState::MAX_STEPS; ++i) {
            target.note[i] = sanitizeMidi7_(payload.note[i]);
            target.velocity[i] = sanitizeMidi7_(payload.velocity[i]);
            target.gate[i] = sanitizeGate_(payload.gate[i]);
            target.nudge[i] = payload.nudge[i];
        }

        const uint8_t focused = sanitizeFocusedStep_(target.focusedStep.get(), length);
        target.focusedStep.set(focused);
        target.page.set(target.pageForStep(focused));
        target.bumpStepDataRevision();
    }

    static void fillWorkspacePayload_(const state::sequencer::SequencerState& source,
                                      WorkspacePayloadV1& out) {
        fillPatternPayload_(source, out.pattern);
        out.focusedStep = sanitizeFocusedStep_(source.focusedStep.get(), out.pattern.length);
        out.page = source.page.get();
        out.activeStepProperty = static_cast<uint8_t>(source.activeStepProperty.get());
    }

    static void applyWorkspacePayload_(const WorkspacePayloadV1& payload,
                                       state::sequencer::SequencerState& target) {
        applyPatternPayload_(payload.pattern, target);

        const uint8_t focused = sanitizeFocusedStep_(payload.focusedStep, target.length.get());
        target.focusedStep.set(focused);

        const uint8_t page_count = target.activePageCount();
        const uint8_t safe_page =
            (page_count == 0) ? 0 : static_cast<uint8_t>(payload.page % page_count);
        target.page.set(safe_page);
        target.activeStepProperty.set(sanitizeStepProperty_(payload.activeStepProperty));
    }

    static void fillSetPayload_(const state::sequencer::SequencerState& source,
                                SetPayloadV1& out) {
        out.trackCount = 1;
        out.activeTrack = 0;
        fillPatternPayload_(source, out.track0);
    }

    static void applySetPayload_(const SetPayloadV1& payload,
                                 state::sequencer::SequencerState& target) {
        if (payload.trackCount == 0) {
            target.reset();
            return;
        }

        applyPatternPayload_(payload.track0, target);
    }

    PersistenceSlotFileStore workspace_store_;
    PersistenceSlotFileStore pattern_library_store_;
    PersistenceSlotFileStore set_library_store_;
    uint32_t next_workspace_counter_ = 1;
    uint16_t next_workspace_slot_ = 0;
};

}  // namespace core::persistence

#pragma once

#include <algorithm>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "persistence/SequencerPersistenceCodec.hpp"
#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence {

/**
 * Persists sequencer workspace, pattern library, and set library data.
 *
 * The workspace journal rotates through latest-valid slots. Pattern and set
 * library slots are addressed directly and serialized through
 * sequencer_codec payload helpers.
 */
class SequencerPersistence {
public:
    static constexpr uint16_t WORKSPACE_SLOT_COUNT = 2;
    static constexpr uint16_t PATTERN_LIBRARY_SLOT_COUNT = 32;
    static constexpr uint16_t SET_LIBRARY_SLOT_COUNT = 16;

    static constexpr uint32_t WORKSPACE_MAGIC = 0x5357534B;         // "SWSK"
    static constexpr uint32_t PATTERN_LIBRARY_MAGIC = 0x53504C42;   // "SPLB"
    static constexpr uint32_t SET_LIBRARY_MAGIC = 0x53534554;       // "SSET"
    static constexpr uint8_t WORKSPACE_DATA_VERSION = 3;
    static constexpr uint8_t LIBRARY_DATA_VERSION = 2;

    explicit SequencerPersistence(oc::interface::IStorage& workspaceStorage,
                                  oc::interface::IStorage& patternLibraryStorage,
                                  oc::interface::IStorage& setLibraryStorage)
        : workspace_store_(workspaceStorage,
                           {.fileMagic = WORKSPACE_MAGIC,
                            .domainVersion = WORKSPACE_DATA_VERSION,
                            .slotCount = WORKSPACE_SLOT_COUNT,
                            .slotPayloadSize = sequencer_codec::WORKSPACE_PAYLOAD_SIZE})
        , pattern_library_store_(patternLibraryStorage,
                                 {.fileMagic = PATTERN_LIBRARY_MAGIC,
                                  .domainVersion = LIBRARY_DATA_VERSION,
                                  .slotCount = PATTERN_LIBRARY_SLOT_COUNT,
                                  .slotPayloadSize = sequencer_codec::PATTERN_PAYLOAD_SIZE})
        , set_library_store_(setLibraryStorage,
                             {.fileMagic = SET_LIBRARY_MAGIC,
                              .domainVersion = LIBRARY_DATA_VERSION,
                              .slotCount = SET_LIBRARY_SLOT_COUNT,
                              .slotPayloadSize = sequencer_codec::SET_PAYLOAD_SIZE}) {}

    bool init() {
        return initStatus() == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus initStatus() {
        if (!workspace_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
        if (!pattern_library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
        if (!set_library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
        return syncWorkspaceJournal_();
    }

    bool loadWorkspace(state::sequencer::SequencerTrackBankState& trackBank,
                       state::sequencer::SequencerState& sequencer) {
        sequencer_codec::WorkspacePayload snapshot{};
        const auto latest = workspace_store_.loadLatest(
            reinterpret_cast<uint8_t*>(&snapshot),
            sizeof(snapshot)
        );
        if (latest.status != SlotLoadStatus::OK) {
            return false;
        }

        sequencer_codec::applyWorkspacePayload(snapshot, trackBank, sequencer);

        next_workspace_counter_ = latest.metadata.saveCounter + 1;
        next_workspace_slot_ =
            static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        return true;
    }

    bool saveWorkspace(const state::sequencer::SequencerTrackBankState& trackBank,
                       const state::sequencer::SequencerState& sequencer) {
        return saveWorkspaceStatus(trackBank, sequencer) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus saveWorkspaceStatus(
        const state::sequencer::SequencerTrackBankState& trackBank,
        const state::sequencer::SequencerState& sequencer
    ) {
        sequencer_codec::WorkspacePayload snapshot{};
        sequencer_codec::fillWorkspacePayload(trackBank, sequencer, snapshot);

        const auto status = workspace_store_.saveSlotStatus(
            next_workspace_slot_,
            reinterpret_cast<const uint8_t*>(&snapshot),
            sizeof(snapshot),
            next_workspace_counter_
        );
        if (status != PersistenceWriteStatus::OK) return status;

        next_workspace_counter_ += 1;
        next_workspace_slot_ =
            static_cast<uint16_t>((next_workspace_slot_ + 1) % WORKSPACE_SLOT_COUNT);
        return PersistenceWriteStatus::OK;
    }

    bool savePatternSlot(uint8_t slotIndex, const state::sequencer::SequencerState& sequencer) {
        return savePatternSlotStatus(slotIndex, sequencer) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus savePatternSlotStatus(
        uint8_t slotIndex,
        const state::sequencer::SequencerState& sequencer
    ) {
        if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;

        sequencer_codec::PatternPayload payload{};
        sequencer_codec::fillPatternPayload(sequencer.pattern, payload);

        const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
        return pattern_library_store_.saveSlotStatus(
            slotIndex,
            reinterpret_cast<const uint8_t*>(&payload),
            sizeof(payload),
            counter
        );
    }

    SlotLoadStatus loadPatternSlot(uint8_t slotIndex, state::sequencer::SequencerState& sequencer) {
        if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

        sequencer_codec::PatternPayload payload{};
        SlotMetadata metadata{};
        const SlotLoadStatus status = pattern_library_store_.loadSlot(
            slotIndex,
            reinterpret_cast<uint8_t*>(&payload),
            sizeof(payload),
            &metadata
        );
        if (status != SlotLoadStatus::OK) {
            return status;
        }

        if (metadata.payloadSize != sizeof(payload)) {
            return SlotLoadStatus::HEADER_MISMATCH;
        }

        sequencer_codec::applyPatternPayload(payload, sequencer.pattern);
        const uint8_t len = sequencer.pattern.length.get();
        const uint8_t focused =
            (len == 0)
                ? 0
                : static_cast<uint8_t>(std::min<uint16_t>(sequencer.focusedStep.get(), len - 1U));
        sequencer.focusedStep.set(focused);
        sequencer.page.set(sequencer.pageForStep(sequencer.focusedStep.get()));
        return SlotLoadStatus::OK;
    }

    bool erasePatternSlot(uint8_t slotIndex) {
        return erasePatternSlotStatus(slotIndex) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus erasePatternSlotStatus(uint8_t slotIndex) {
        if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;
        return pattern_library_store_.eraseSlotStatus(slotIndex);
    }

    bool saveSetSlot(uint8_t slotIndex,
                     const state::sequencer::SequencerTrackBankState& trackBank,
                     const state::sequencer::SequencerState& sequencer) {
        return saveSetSlotStatus(slotIndex, trackBank, sequencer) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus saveSetSlotStatus(
        uint8_t slotIndex,
        const state::sequencer::SequencerTrackBankState& trackBank,
        const state::sequencer::SequencerState& sequencer
    ) {
        if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;

        sequencer_codec::SetPayload payload{};
        sequencer_codec::fillSetPayload(trackBank, sequencer, payload);

        const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
        return set_library_store_.saveSlotStatus(
            slotIndex,
            reinterpret_cast<const uint8_t*>(&payload),
            sizeof(payload),
            counter
        );
    }

    SlotLoadStatus loadSetSlot(uint8_t slotIndex,
                               state::sequencer::SequencerTrackBankState& trackBank,
                               state::sequencer::SequencerState& sequencer) {
        if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

        sequencer_codec::SetPayload payload{};
        SlotMetadata metadata{};
        const SlotLoadStatus status = set_library_store_.loadSlot(
            slotIndex,
            reinterpret_cast<uint8_t*>(&payload),
            sizeof(payload),
            &metadata
        );
        if (status != SlotLoadStatus::OK) {
            return status;
        }

        if (metadata.payloadSize != sizeof(payload)) {
            return SlotLoadStatus::HEADER_MISMATCH;
        }

        sequencer_codec::applySetPayload(payload, trackBank, sequencer);
        return SlotLoadStatus::OK;
    }

    bool eraseSetSlot(uint8_t slotIndex) {
        return eraseSetSlotStatus(slotIndex) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus eraseSetSlotStatus(uint8_t slotIndex) {
        if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;
        return set_library_store_.eraseSlotStatus(slotIndex);
    }

private:
    PersistenceWriteStatus syncWorkspaceJournal_() {
        sequencer_codec::WorkspacePayload snapshot{};
        const auto latest = workspace_store_.loadLatest(
            reinterpret_cast<uint8_t*>(&snapshot),
            sizeof(snapshot)
        );
        if (latest.status == SlotLoadStatus::OK) {
            next_workspace_counter_ = latest.metadata.saveCounter + 1;
            next_workspace_slot_ =
                static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        } else {
            next_workspace_counter_ = 1;
            next_workspace_slot_ = 0;
        }
        return PersistenceWriteStatus::OK;
    }

    PersistenceSlotFileStore workspace_store_;
    PersistenceSlotFileStore pattern_library_store_;
    PersistenceSlotFileStore set_library_store_;
    uint32_t next_workspace_counter_ = 1;
    uint16_t next_workspace_slot_ = 0;
};

}  // namespace core::persistence

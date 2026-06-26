#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence {

/**
 * Persists explicit sequencer pattern and set library data.
 *
 * Pattern and set library slots are addressed directly and serialized through
 * sequencer_codec payload helpers.
 */
class SequencerPersistence {
public:
    static constexpr uint16_t PATTERN_LIBRARY_SLOT_COUNT = 32;
    static constexpr uint16_t SET_LIBRARY_SLOT_COUNT = 16;

    static constexpr uint32_t PATTERN_LIBRARY_MAGIC = 0x53504C42;   // "SPLB"
    static constexpr uint32_t SET_LIBRARY_MAGIC = 0x53534554;       // "SSET"
    static constexpr uint8_t LIBRARY_DATA_VERSION = 4;
    static constexpr size_t PATTERN_LIBRARY_STORAGE_CAPACITY =
        PersistenceSlotFileStore::requiredCapacity(
            PATTERN_LIBRARY_SLOT_COUNT,
            sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
        );
    static constexpr size_t SET_LIBRARY_STORAGE_CAPACITY =
        PersistenceSlotFileStore::requiredCapacity(
            SET_LIBRARY_SLOT_COUNT,
            sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
        );

    explicit SequencerPersistence(oc::interface::IStorage& patternLibraryStorage,
                                  oc::interface::IStorage& setLibraryStorage);

    bool init();
    PersistenceWriteStatus initStatus();

    bool savePatternSlot(uint8_t slotIndex, const state::sequencer::SequencerState& sequencer);
    PersistenceWriteStatus savePatternSlotStatus(
        uint8_t slotIndex,
        const state::sequencer::SequencerState& sequencer
    );
    SlotLoadStatus loadPatternSlot(uint8_t slotIndex,
                                   state::sequencer::SequencerState& sequencer);
    bool erasePatternSlot(uint8_t slotIndex);
    PersistenceWriteStatus erasePatternSlotStatus(uint8_t slotIndex);

    bool saveSetSlot(uint8_t slotIndex,
                     const state::sequencer::SequencerTrackBankState& trackBank,
                     const state::sequencer::SequencerState& sequencer);
    PersistenceWriteStatus saveSetSlotStatus(
        uint8_t slotIndex,
        const state::sequencer::SequencerTrackBankState& trackBank,
        const state::sequencer::SequencerState& sequencer
    );
    SlotLoadStatus loadSetSlot(uint8_t slotIndex,
                               state::sequencer::SequencerTrackBankState& trackBank,
                               state::sequencer::SequencerState& sequencer);
    bool eraseSetSlot(uint8_t slotIndex);
    PersistenceWriteStatus eraseSetSlotStatus(uint8_t slotIndex);

private:
    PersistenceSlotFileStore pattern_library_store_;
    PersistenceSlotFileStore set_library_store_;
};

}  // namespace core::persistence

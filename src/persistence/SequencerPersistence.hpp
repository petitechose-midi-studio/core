#pragma once

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
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
    static constexpr uint8_t WORKSPACE_DATA_VERSION = 4;
    static constexpr uint8_t LIBRARY_DATA_VERSION = 3;

    explicit SequencerPersistence(oc::interface::IStorage& workspaceStorage,
                                  oc::interface::IStorage& patternLibraryStorage,
                                  oc::interface::IStorage& setLibraryStorage);

    bool init();
    PersistenceWriteStatus initStatus();

    bool loadWorkspace(state::sequencer::SequencerTrackBankState& trackBank,
                       state::sequencer::SequencerState& sequencer);
    bool saveWorkspace(const state::sequencer::SequencerTrackBankState& trackBank,
                       const state::sequencer::SequencerState& sequencer);
    PersistenceWriteStatus saveWorkspaceStatus(
        const state::sequencer::SequencerTrackBankState& trackBank,
        const state::sequencer::SequencerState& sequencer
    );

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
    PersistenceWriteStatus syncWorkspaceJournal_();

    PersistenceSlotFileStore workspace_store_;
    PersistenceSlotFileStore pattern_library_store_;
    PersistenceSlotFileStore set_library_store_;
    uint32_t next_workspace_counter_ = 1;
    uint16_t next_workspace_slot_ = 0;
};

}  // namespace core::persistence

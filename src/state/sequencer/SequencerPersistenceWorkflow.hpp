#pragma once

#include <cstdint>

#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::sequencer {

/**
 * CoreState-facing sequencer persistence workflow.
 *
 * Loads while playing are staged through CoreState pending-apply snapshots.
 * Stopped loads apply immediately, sync the shared track state, and mark the
 * current project as mutated.
 */
struct SequencerPersistenceWorkflow {
    static bool savePatternSlot(CoreState& state, uint8_t slotIndex);
    static persistence::SlotLoadStatus loadPatternSlot(CoreState& state, uint8_t slotIndex);
    static bool erasePatternSlot(CoreState& state, uint8_t slotIndex);

    static bool saveSetSlot(CoreState& state, uint8_t slotIndex);
    static persistence::SlotLoadStatus loadSetSlot(
        CoreState& state,
        uint8_t slotIndex,
        bool merge = false
    );
    static bool eraseSetSlot(CoreState& state, uint8_t slotIndex);
};

}  // namespace core::state::sequencer

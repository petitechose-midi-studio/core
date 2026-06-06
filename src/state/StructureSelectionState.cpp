#include "state/StructureSelectionState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM StructureSelectionState::StructureSelectionState() = default;

FLASHMEM void StructureSelectionState::reset(StructureSelectionScope focus, uint8_t cursor) {
    active.set(false);
    scope.set(focus);
    cursorIndex.set(cursor);
    selectedMask.set(0);
}

FLASHMEM bool StructureHoldState::active() const {
    return action.get() != StructureHoldAction::NONE;
}

FLASHMEM void StructureHoldState::begin(StructureHoldAction nextAction, uint32_t nowMs) {
    action.set(nextAction);
    startedAtMs.set(nowMs);
}

FLASHMEM void StructureHoldState::clear() {
    action.set(StructureHoldAction::NONE);
    startedAtMs.set(0);
}

}  // namespace core::state

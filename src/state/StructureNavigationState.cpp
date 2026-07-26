#include "state/StructureNavigationState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM void StructureSelectionState::reset(
    StructureSelectionScope nextScope,
    uint8_t cursor
) {
    active.set(false);
    scope.set(nextScope);
    cursorIndex.set(cursor);
    selectedMask.set(0U);
}

FLASHMEM bool StructureHoldState::active() const {
    return action.get() != StructureHoldAction::NONE;
}

FLASHMEM void StructureHoldState::begin(
    StructureHoldAction nextAction,
    uint32_t nowMs
) {
    action.set(nextAction);
    startedAtMs.set(nowMs);
}

FLASHMEM void StructureHoldState::clear() {
    action.set(StructureHoldAction::NONE);
    startedAtMs.set(0);
}

}  // namespace core::state

#include "state/StructureNavigationState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM void StructureSelectionState::reset(
    StructureSelectionScope nextScope,
    uint8_t cursor
) {
    active.set(false);
    placing.set(false);
    scope.set(nextScope);
    cursorIndex.set(cursor);
    selectedMask.set(0U);
    destinationMask.set(0U);
    overwriteMask.set(0U);
    pasteBlocked.set(false);
    clipboardRevision.set(0U);
}

FLASHMEM void StructureSelectionState::clearCurrent() {
    placing.set(false);
    selectedMask.set(0U);
    destinationMask.set(0U);
    overwriteMask.set(0U);
    pasteBlocked.set(false);
    clipboardRevision.set(0U);
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

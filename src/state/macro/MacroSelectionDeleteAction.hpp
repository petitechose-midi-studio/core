#pragma once

#include <cstdint>

#include "state/StructureSelectionState.hpp"
#include "state/contextual/ContextActionSpec.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"

namespace core::state::macro {

struct MacroSelectionDeleteSource {
    bool active = false;
    core::state::StructureSelectionScope scope =
        core::state::StructureSelectionScope::PAGE;
    uint16_t selectedMask = 0;
    uint16_t enabledMask = 0;
    uint8_t currentIndex = 0;
    uint8_t activeTrack = 0;
    uint8_t activePage = 0;
};

enum class MacroSelectionDeletePresentationState : uint8_t {
    DISABLED = 0,
    AVAILABLE,
    PRESSED,
    ARMED,
    CANCELLED,
    APPLIED,
};

core::state::contextual::ContextActionSpec buildMacroSelectionDeleteActionSpec(
    const MacroSelectionDeleteSource& source
);

MacroSelectionDeletePresentationState macroSelectionDeletePresentationState(
    const core::state::contextual::ContextActionSpec& action,
    const core::state::contextual::GuardedActionState& guard,
    const core::state::contextual::OperationFeedbackState& feedback
);

}  // namespace core::state::macro

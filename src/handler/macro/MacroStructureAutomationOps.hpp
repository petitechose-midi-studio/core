#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroAutomationState.hpp"

namespace core::handler::macro_structure_automation_ops {

bool duplicatePage(core::state::macro::MacroAutomationBankState& bank,
                   uint8_t sourceTrack,
                   uint8_t sourcePage,
                   uint8_t destTrack,
                   uint8_t destPage);

bool duplicateTrack(core::state::macro::MacroAutomationBankState& bank,
                    uint8_t sourceTrack,
                    uint8_t destTrack);

bool replacePageFromClipboard(
    core::state::macro::MacroAutomationBankState& bank,
    uint8_t destTrack,
    uint8_t destPage,
    const core::state::MacroAutomationClipboard* clipboard
);

bool replaceTrackFromClipboard(
    core::state::macro::MacroAutomationBankState& bank,
    uint8_t destTrack,
    const core::state::MacroAutomationClipboard* clipboard
);

}  // namespace core::handler::macro_structure_automation_ops

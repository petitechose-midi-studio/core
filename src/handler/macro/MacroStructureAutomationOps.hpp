#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::handler::macro_structure_automation_ops {

struct ProjectControlPageCopy {
    uint8_t sourceTrack = 0;
    uint8_t sourcePage = 0;
    uint8_t destTrack = 0;
    uint8_t destPage = 0;
};

struct ProjectControlTrackCopy {
    uint8_t sourceTrack = 0;
    uint8_t destTrack = 0;
};

/** Cold, atomic graph transactions used by Macro structural editing. */
bool clearPages(
    core::state::modulation::ProjectControlState& control,
    uint8_t track,
    uint16_t pageMask
);
bool clearTracks(
    core::state::modulation::ProjectControlState& control,
    uint16_t trackMask
);
bool clearMacroSlot(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
);
bool duplicatePages(
    core::state::modulation::ProjectControlState& control,
    const ProjectControlPageCopy* copies,
    uint8_t copyCount
);
bool duplicateTracks(
    core::state::modulation::ProjectControlState& control,
    const ProjectControlTrackCopy* copies,
    uint8_t copyCount
);

bool replacePageFromClipboard(
    core::state::modulation::ProjectControlState& control,
    uint8_t destTrack,
    uint8_t destPage,
    const core::state::MacroAutomationClipboard* clipboard
);

bool replaceTrackFromClipboard(
    core::state::modulation::ProjectControlState& control,
    uint8_t destTrack,
    const core::state::MacroAutomationClipboard* clipboard
);

}  // namespace core::handler::macro_structure_automation_ops

#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::handler::macro_structure_automation_ops {

/** Cold, atomic graph transactions used by Macro structural editing. */
bool clearPages(
    core::state::modulation::ProjectControlState& control,
    uint8_t track,
    uint16_t pageMask
);
bool clearPagesInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    uint8_t track,
    uint16_t pageMask
);
/**
 * Clears every Page outside retainedPageMask and compacts retained Project
 * destinations to the zero-based rank of their previous Page address.
 */
bool compactPages(
    core::state::modulation::ProjectControlState& control,
    uint8_t track,
    uint16_t retainedPageMask
);
bool clearTracks(
    core::state::modulation::ProjectControlState& control,
    uint16_t trackMask
);
/** Detached-domain variant for an enclosing prepared Track transaction. */
bool clearTracksInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    uint16_t trackMask
);
bool clearMacroSlot(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
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
/** Allocation-free source validation used before a prepared Track boundary. */
[[nodiscard]] bool trackClipboardValid(
    const core::state::MacroAutomationClipboard* clipboard
);
/** Detached-domain variant; partial failure is discard-only for the caller. */
bool replaceTrackFromClipboardInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    uint8_t destTrack,
    const core::state::MacroAutomationClipboard* clipboard
);

}  // namespace core::handler::macro_structure_automation_ops

#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::handler::macro_structure_automation_ops {

/** Detached mutations: the enclosing transaction validates and publishes the domain. */
bool clearPagesInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    uint8_t track,
    uint16_t pageMask
);
/** Detached-domain variant for an enclosing prepared Track transaction. */
bool clearTracksInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    uint16_t trackMask
);
bool replacePageFromClipboardInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    uint8_t destTrack,
    uint8_t destPage,
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

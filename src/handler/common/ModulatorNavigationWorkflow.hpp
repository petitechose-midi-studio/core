#pragma once

#include <cstdint>

#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "state/MacroEditState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::handler::modulator_navigation {

struct StateRefs {
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    oc::state::Signal<core::ui::ViewType, 8>& activeView;
    core::state::project::ProjectNavigationState& projectNavigation;
    core::state::MacroEditState& macroEdit;
    core::state::macro::MacroPagesState& pages;
    const core::state::project::ProjectTrackState& projectTracks;
};

/** Opens the source owning one exact Macro modulation assignment. */
[[nodiscard]] bool openSourceFromMacro(
    StateRefs state,
    uint8_t macroIndex,
    core::state::modulation::ModulationBindingId bindingId,
    uint8_t focusedRow
);

/** Opens the real Project source workspace for one provisional new source. */
[[nodiscard]] bool openAuditionSourceFromMacro(
    StateRefs state,
    uint8_t macroIndex
);

[[nodiscard]] bool macroAuditionReturnPending(
    const core::state::project::ProjectNavigationState& navigation
);

[[nodiscard]] bool macroReturnPending(
    const core::state::project::ProjectNavigationState& navigation
);

/** True when Back should leave Project instead of popping another child. */
[[nodiscard]] bool shouldReturnToMacroOnBack(
    const core::state::project::ProjectNavigationState& navigation
);

/** Restores the exact Macro assignment, or the nearest deterministic fallback. */
[[nodiscard]] bool returnToMacro(StateRefs state, uint32_t nowMs);

/** Returns from the provisional source workspace after Apply or Cancel. */
[[nodiscard]] bool returnToMacroFromAudition(
    StateRefs state,
    bool committed,
    uint32_t nowMs
);

}  // namespace core::handler::modulator_navigation

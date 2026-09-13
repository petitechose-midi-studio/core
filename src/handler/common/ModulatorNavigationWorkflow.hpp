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
#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::handler::modulator_navigation {

struct StateRefs {
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    oc::state::Signal<core::ui::ViewType, 8>& activeView;
    core::state::project::ProjectNavigationState& projectNavigation;
    core::state::MacroEditState& macroEdit;
    core::state::macro::MacroPagesState& pages;
    const core::state::project::ProjectTrackState& projectTracks;
};

/**
 * Resume a validated Macro target without materializing its hidden parent.
 * prepare runs synchronously after old visibility is cleared and config is
 * loaded, before either the target view or its detail overlay is published.
 * The caller retains the policy for panel, focus and feedback; Back restores
 * the parent, and the existing handler restores OPT on phase/view entry.
 */
template <typename Prepare>
void resumeMacroEditor(StateRefs state, uint8_t macroIndex, Prepare&& prepare) {
    state.overlays.hideAll();
    state.macroEdit.loadActiveConfig(
        macroIndex,
        core::state::project::projectTrackMidiChannel(
            state.projectTracks, state.pages.currentActiveTrack()),
        state.pages.activeConfigs[macroIndex].cc
    );
    prepare();
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.show(core::ui::OverlayType::MACRO_AUTOMATION, false);
}

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

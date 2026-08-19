#pragma once

#include <type_traits>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::modulation {

/**
 * Cold, Project-owned authored control state.
 *
 * Automation and Modulation keep independent directories while sharing the
 * immutable curve arena. MacroPagesState owns the complete control aggregate in
 * EXTMEM; this domain is the sole writable musical authority.
 */
struct ProjectControlDomainState {
    ProjectAutomationCurveDirectory automation{};
    ProjectModulationState modulation{};
    ProjectCurveArena curves{};

    void clear() {
        automation.clear();
        modulation.clear();
        curves.clear();
    }
};

static_assert(sizeof(ProjectControlDomainState) == 159516U);
static_assert(std::is_trivially_copyable_v<ProjectControlDomainState>);

}  // namespace core::state::modulation

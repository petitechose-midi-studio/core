#pragma once

#include <type_traits>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::modulation {

/**
 * Cold, Project-owned authored control state.
 *
 * Automation and Modulation keep independent directories while sharing the
 * immutable curve arena. CoreState and ProjectSnapshot will own this object in
 * EXTMEM once the Gate 3 runtime boundary replaces the legacy writable bank.
 */
struct ProjectControlDomainState {
    ProjectAutomationCurveDirectory automation{};
    ProjectModulationState modulation{};
    ProjectCurveArena curves{};

    void clear() {
        automation = {};
        modulation = {};
        curves = {};
    }
};

static_assert(sizeof(ProjectControlDomainState) == 159516U);
static_assert(std::is_trivially_copyable_v<ProjectControlDomainState>);

}  // namespace core::state::modulation

#include "handler/project/ProjectLifecycleDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

namespace {

FLASHMEM bool resetMusicalProjectFromCoreState(void* context) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    state->resetMusicalProject();
    return true;
}

}  // namespace

FLASHMEM ProjectLifecycleDomainServices::ProjectLifecycleDomainServices(Operations operations)
    : operations_(operations) {}

FLASHMEM ProjectLifecycleDomainServices ProjectLifecycleDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return ProjectLifecycleDomainServices{
        Operations{
            &state,
            resetMusicalProjectFromCoreState,
        }
    };
}

FLASHMEM bool ProjectLifecycleDomainServices::resetMusicalProject() const {
    return operations_.resetMusicalProject != nullptr &&
           operations_.resetMusicalProject(operations_.context);
}

}  // namespace core::handler

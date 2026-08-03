#include "persistence/ProjectSessionRestoreService.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProjectSessionStore.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

FLASHMEM ProjectSessionRestoreService::ProjectSessionRestoreService(ProjectSessionStore& store)
    : store_(store) {}

FLASHMEM ProjectSessionRestoreService::Result ProjectSessionRestoreService::restore(
    core::state::CoreState& state,
    core::persistence::project_file::LoadReport* report
) {
    return restore_(state, nullptr, report);
}

FLASHMEM ProjectSessionRestoreService::Result ProjectSessionRestoreService::restore(
    core::state::CoreState& state,
    const ProductMutationLease& recoveryLease,
    core::persistence::project_file::LoadReport* report
) {
    return restore_(state, &recoveryLease, report);
}

FLASHMEM ProjectSessionRestoreService::Result ProjectSessionRestoreService::restore_(
    core::state::CoreState& state,
    const ProductMutationLease* recoveryLease,
    core::persistence::project_file::LoadReport* report
) {
    auto snapshot = core::state::project::makeProjectSnapshot();
    if (!snapshot) {
        return Result{
            .status = Status::DEGRADED,
        };
    }
    core::persistence::project_file::LoadReport localReport{};
    auto loaded = recoveryLease != nullptr
        ? store_.loadCurrent(
              *snapshot,
              *recoveryLease,
              report != nullptr ? report : &localReport
          )
        : store_.loadCurrent(
              *snapshot,
              report != nullptr ? report : &localReport
          );
    if (!loaded) {
        const auto code = loaded.error().code;
        return Result{
            .status = code == oc::type::ErrorCode::RESOURCE_NOT_FOUND
                ? Status::MISSING
                : Status::DEGRADED,
        };
    }

    if (!core::state::project::applyProjectSnapshot(state, *snapshot)) {
        return Result{
            .status = Status::APPLY_FAILED,
            .bytes = loaded.value().bytesRead,
        };
    }

    return Result{
        .status = Status::RESTORED,
        .bytes = loaded.value().bytesRead,
    };
}

}  // namespace core::persistence

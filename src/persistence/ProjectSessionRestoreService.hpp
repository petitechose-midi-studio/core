#pragma once

#include <cstdint>

#include "persistence/ProjectLoadReport.hpp"

namespace core::state {
struct CoreState;
}

namespace core::persistence {

class ProjectSessionStore;
class ProductMutationLease;

class ProjectSessionRestoreService {
public:
    enum class Status : uint8_t {
        MISSING = 0,
        RESTORED,
        DEGRADED,
        APPLY_FAILED,
    };

    struct Result {
        Status status = Status::MISSING;
        uint32_t bytes = 0;

        bool restored() const {
            return status == Status::RESTORED;
        }
    };

    explicit ProjectSessionRestoreService(ProjectSessionStore& store);

    Result restore(core::state::CoreState& state,
                   core::persistence::project_file::LoadReport* report = nullptr);
    Result restore(
        core::state::CoreState& state,
        const ProductMutationLease& recoveryLease,
        core::persistence::project_file::LoadReport* report = nullptr
    );

private:
    Result restore_(
        core::state::CoreState& state,
        const ProductMutationLease* recoveryLease,
        core::persistence::project_file::LoadReport* report
    );

    ProjectSessionStore& store_;
};

}  // namespace core::persistence

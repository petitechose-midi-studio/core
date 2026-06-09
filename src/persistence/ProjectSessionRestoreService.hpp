#pragma once

#include <cstdint>

#include "persistence/ProjectLoadReport.hpp"

namespace core::state {
struct CoreState;
}

namespace core::persistence {

class ProductFileService;

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
        project_file::LoadStatus loadStatus = project_file::LoadStatus::OK;
        bool overwriteSafe = true;

        bool restored() const {
            return status == Status::RESTORED;
        }
    };

    explicit ProjectSessionRestoreService(ProductFileService& files);

    Result restore(core::state::CoreState& state,
                   core::persistence::project_file::LoadReport* report = nullptr);

private:
    ProductFileService& files_;
};

}  // namespace core::persistence

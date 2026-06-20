#pragma once

#include <cstdint>

#include "persistence/ProjectFileContainer.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence::project_file_migration {

enum class Status : uint8_t {
    CURRENT = 0,
    MIGRATED,
    PARTIAL,
    FAILED,
};

struct Options {
    bool allowPartialOutput = false;
};

struct Result {
    Status status = Status::CURRENT;
    core::persistence::project_file::Status containerStatus =
        core::persistence::project_file::Status::OK;
    core::persistence::project_file::LoadStatus loadStatus =
        core::persistence::project_file::LoadStatus::OK;
    bool overwriteSafe = true;
    uint32_t bytesWritten = 0;
};

Result inspectProjectBytes(
    const uint8_t* data,
    uint32_t size,
    core::persistence::project_file::LoadReport* report = nullptr
);

Result decodeProjectBytesToSnapshot(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report = nullptr
);

Result migrateProjectBytesToCurrent(
    const uint8_t* data,
    uint32_t size,
    uint8_t* out,
    uint32_t outCapacity,
    core::persistence::project_file::LoadReport* report = nullptr,
    Options options = {}
);

const char* statusName(Status status);

}  // namespace core::persistence::project_file_migration

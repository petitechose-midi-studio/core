#pragma once

#include <cstdint>

#include "persistence/ProjectFileContainer.hpp"
#include "persistence/ProjectSnapshotPersistencePayloads.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence::project_snapshot_codec {

struct DecodeResult {
    bool ok = false;
    core::persistence::project_file::Status containerStatus =
        core::persistence::project_file::Status::OK;
    core::persistence::project_file::LoadStatus loadStatus =
        core::persistence::project_file::LoadStatus::OK;
    bool overwriteSafe = true;
};

core::persistence::project_file::EncodeResult encodeProjectSnapshot(
    const core::state::project::ProjectSnapshot& snapshot,
    uint8_t* out,
    uint32_t outCapacity
);

DecodeResult decodeProjectSnapshot(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report = nullptr
);

}  // namespace core::persistence::project_snapshot_codec

#pragma once

#include <cstdint>

#include "app/ExtmemAllocator.hpp"
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

/**
 * Reusable PSRAM-owned scratch for project snapshot encoding.
 *
 * File stores retain one instance so autosave never allocates large payload or
 * envelope buffers in the interaction path. The convenience encoder still
 * creates a temporary workspace for cold host-tool and migration calls.
 */
class ProjectSnapshotCodecWorkspace {
public:
    ProjectSnapshotCodecWorkspace();
    ~ProjectSnapshotCodecWorkspace();

    ProjectSnapshotCodecWorkspace(const ProjectSnapshotCodecWorkspace&) = delete;
    ProjectSnapshotCodecWorkspace& operator=(const ProjectSnapshotCodecWorkspace&) = delete;
    ProjectSnapshotCodecWorkspace(ProjectSnapshotCodecWorkspace&&) noexcept;
    ProjectSnapshotCodecWorkspace& operator=(ProjectSnapshotCodecWorkspace&&) noexcept;

    bool prepare();

private:
    struct Storage;
    core::app::ExtmemUniquePtr<Storage> storage_;

    friend core::persistence::project_file::EncodeResult encodeProjectSnapshot(
        const core::state::project::ProjectSnapshot& snapshot,
        uint8_t* out,
        uint32_t outCapacity,
        ProjectSnapshotCodecWorkspace& workspace
    );
};

core::persistence::project_file::EncodeResult encodeProjectSnapshot(
    const core::state::project::ProjectSnapshot& snapshot,
    uint8_t* out,
    uint32_t outCapacity,
    ProjectSnapshotCodecWorkspace& workspace
);

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

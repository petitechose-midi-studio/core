#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence {

using ProjectFileBuffer = std::array<uint8_t, PROJECT_FILE_MAX_SIZE>;

class ProjectFileWorkspace {
public:
    bool prepare() {
        if (!buffer_) {
            buffer_ = core::app::makeExtmemUniqueForOverwrite<ProjectFileBuffer>();
        }
        return static_cast<bool>(buffer_) && codec_workspace_.prepare();
    }

    uint8_t* data() {
        return buffer_ ? buffer_->data() : nullptr;
    }

    static constexpr uint32_t capacity() {
        return PROJECT_FILE_MAX_SIZE;
    }

    project_snapshot_codec::ProjectSnapshotCodecWorkspace& codecWorkspace() {
        return codec_workspace_;
    }

private:
    core::app::ExtmemUniquePtr<ProjectFileBuffer> buffer_;
    project_snapshot_codec::ProjectSnapshotCodecWorkspace codec_workspace_;
};

}  // namespace core::persistence

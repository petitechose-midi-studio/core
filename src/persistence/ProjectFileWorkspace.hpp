#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProductFileCommitPlan.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence {

/**
 * One PSRAM allocation owns both the encoded Project bytes and the cold
 * durable-promotion continuation. Raw storage keeps the large allocation
 * eligible for makeExtmemUniqueForOverwrite(); the continuation is placement
 * constructed only when a save reaches COMMIT.
 */
struct ProjectFileBuffer {
    std::array<uint8_t, PROJECT_FILE_MAX_SIZE> bytes;
    alignas(ProductFileCommitPlan)
        std::byte commitPlanStorage[sizeof(ProductFileCommitPlan)];
};

static_assert(std::is_trivially_default_constructible_v<ProjectFileBuffer>);
static_assert(std::is_trivially_destructible_v<ProjectFileBuffer>);

class ProjectFileReadWorkspace {
public:
    bool prepare() {
        if (!buffer_) {
            buffer_ = core::app::makeExtmemUniqueForOverwrite<ProjectFileBuffer>();
        }
        return static_cast<bool>(buffer_);
    }

    uint8_t* data() {
        return buffer_ ? buffer_->bytes.data() : nullptr;
    }

    static constexpr uint32_t capacity() {
        return PROJECT_FILE_MAX_SIZE;
    }

protected:
    ProjectFileBuffer& buffer() {
        return *buffer_;
    }

private:
    core::app::ExtmemUniquePtr<ProjectFileBuffer> buffer_;
};

class ProjectFileWriteWorkspace final : public ProjectFileReadWorkspace {
public:
    bool prepare() {
        return ProjectFileReadWorkspace::prepare() && codec_workspace_.prepare();
    }

    project_snapshot_codec::ProjectSnapshotCodecWorkspace& codecWorkspace() {
        return codec_workspace_;
    }

    ProductFileCommitPlan& resetCommitPlan() {
        return *new (buffer().commitPlanStorage) ProductFileCommitPlan{};
    }

    ProductFileCommitPlan& commitPlan() {
        return *reinterpret_cast<ProductFileCommitPlan*>(
            buffer().commitPlanStorage
        );
    }

private:
    project_snapshot_codec::ProjectSnapshotCodecWorkspace codec_workspace_;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProjectFileBuffer) == 526176U, "project file buffer ABI drift");
static_assert(alignof(ProjectFileBuffer) == 8U, "project file buffer alignment drift");
static_assert(sizeof(ProjectFileReadWorkspace) == 4U, "project read workspace ABI drift");
static_assert(sizeof(ProjectFileWriteWorkspace) == 8U, "project write workspace ABI drift");
#endif

}  // namespace core::persistence

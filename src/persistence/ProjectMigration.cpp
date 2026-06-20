#include "persistence/ProjectMigration.hpp"

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence::project_file_migration {

namespace {

namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

FLASHMEM Status classify(const snapshot_codec::DecodeResult& result) {
    if (!result.ok || result.loadStatus == project_file::LoadStatus::FAILED) {
        return Status::FAILED;
    }
    if (result.loadStatus == project_file::LoadStatus::PARTIAL) {
        return Status::PARTIAL;
    }
    if (result.loadStatus == project_file::LoadStatus::MIGRATED) {
        return Status::MIGRATED;
    }
    return Status::CURRENT;
}

FLASHMEM Result resultFromDecode(const snapshot_codec::DecodeResult& decodeResult,
                                 uint32_t bytesWritten = 0) {
    return {
        .status = classify(decodeResult),
        .containerStatus = decodeResult.containerStatus,
        .loadStatus = decodeResult.loadStatus,
        .overwriteSafe = decodeResult.overwriteSafe,
        .bytesWritten = bytesWritten,
    };
}

FLASHMEM Result allocationFailed() {
    return {
        .status = Status::FAILED,
        .containerStatus = project_file::Status::SCRATCH_ALLOCATION_FAILED,
        .loadStatus = project_file::LoadStatus::FAILED,
        .overwriteSafe = false,
        .bytesWritten = 0,
    };
}

}  // namespace

FLASHMEM Result inspectProjectBytes(const uint8_t* data,
                                    uint32_t size,
                                    project_file::LoadReport* report) {
    auto snapshot = core::app::makeExtmemUnique<core::state::project::ProjectSnapshot>();
    if (!snapshot) return allocationFailed();
    return decodeProjectBytesToSnapshot(data, size, *snapshot, report);
}

FLASHMEM Result decodeProjectBytesToSnapshot(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectSnapshot& out,
    project_file::LoadReport* report
) {
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(data, size, out, report);
    return resultFromDecode(decodeResult);
}

FLASHMEM Result migrateProjectBytesToCurrent(const uint8_t* data,
                                             uint32_t size,
                                             uint8_t* out,
                                             uint32_t outCapacity,
                                             project_file::LoadReport* report,
                                             Options options) {
    if (out == nullptr || outCapacity == 0) {
        return {
            .status = Status::FAILED,
            .containerStatus = project_file::Status::INVALID_ARGUMENT,
            .loadStatus = project_file::LoadStatus::FAILED,
            .overwriteSafe = false,
            .bytesWritten = 0,
        };
    }

    auto snapshot = core::app::makeExtmemUnique<core::state::project::ProjectSnapshot>();
    if (!snapshot) return allocationFailed();

    auto decodeResult = snapshot_codec::decodeProjectSnapshot(data, size, *snapshot, report);
    auto migrationResult = resultFromDecode(decodeResult);
    if (migrationResult.status == Status::FAILED ||
        (migrationResult.status == Status::PARTIAL && !options.allowPartialOutput)) {
        return migrationResult;
    }

    const auto encodeResult =
        snapshot_codec::encodeProjectSnapshot(*snapshot, out, outCapacity);
    if (encodeResult.status != project_file::Status::OK) {
        return {
            .status = Status::FAILED,
            .containerStatus = encodeResult.status,
            .loadStatus = migrationResult.loadStatus,
            .overwriteSafe = false,
            .bytesWritten = encodeResult.bytesWritten,
        };
    }

    migrationResult.bytesWritten = encodeResult.bytesWritten;
    return migrationResult;
}

FLASHMEM const char* statusName(Status status) {
    switch (status) {
        case Status::CURRENT:
            return "current";
        case Status::MIGRATED:
            return "migrated";
        case Status::PARTIAL:
            return "partial";
        case Status::FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

}  // namespace core::persistence::project_file_migration

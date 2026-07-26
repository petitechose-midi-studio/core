#include "persistence/ProjectFileInspection.hpp"

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence::project_file_inspection {

namespace {

namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

FLASHMEM Status classify(const snapshot_codec::DecodeResult& result) {
    if (!result.ok || result.loadStatus == project_file::LoadStatus::FAILED) {
        return Status::FAILED;
    }
    return result.loadStatus == project_file::LoadStatus::PARTIAL
        ? Status::PARTIAL
        : Status::CURRENT;
}

FLASHMEM Result resultFromDecode(
    const snapshot_codec::DecodeResult& decoded,
    uint32_t bytesWritten = 0U
) {
    return {
        .status = classify(decoded),
        .containerStatus = decoded.containerStatus,
        .loadStatus = decoded.loadStatus,
        .overwriteSafe = decoded.overwriteSafe,
        .bytesWritten = bytesWritten,
    };
}

FLASHMEM Result allocationFailed() {
    return {
        .status = Status::FAILED,
        .containerStatus = project_file::Status::SCRATCH_ALLOCATION_FAILED,
        .loadStatus = project_file::LoadStatus::FAILED,
        .overwriteSafe = false,
    };
}

}  // namespace

FLASHMEM Result inspectProjectBytes(
    const uint8_t* data,
    uint32_t size,
    project_file::LoadReport* report
) {
    auto snapshot = core::state::project::makeProjectSnapshot();
    if (!snapshot) return allocationFailed();
    return decodeProjectBytes(data, size, *snapshot, report);
}

FLASHMEM Result decodeProjectBytes(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectSnapshot& out,
    project_file::LoadReport* report
) {
    return resultFromDecode(
        snapshot_codec::decodeProjectSnapshot(data, size, out, report)
    );
}

FLASHMEM Result rewriteProjectBytes(
    const uint8_t* data,
    uint32_t size,
    uint8_t* out,
    uint32_t outCapacity,
    project_file::LoadReport* report,
    RewriteOptions options
) {
    if (out == nullptr || outCapacity == 0U) {
        return {
            .status = Status::FAILED,
            .containerStatus = project_file::Status::INVALID_ARGUMENT,
            .loadStatus = project_file::LoadStatus::FAILED,
            .overwriteSafe = false,
        };
    }

    auto snapshot = core::state::project::makeProjectSnapshot();
    if (!snapshot) return allocationFailed();
    const auto decoded = snapshot_codec::decodeProjectSnapshot(
        data,
        size,
        *snapshot,
        report
    );
    auto result = resultFromDecode(decoded);
    if (result.status == Status::FAILED ||
        (result.status == Status::PARTIAL && !options.allowPartialOutput)) {
        return result;
    }

    const auto encoded = snapshot_codec::encodeProjectSnapshot(
        *snapshot,
        out,
        outCapacity
    );
    if (encoded.status != project_file::Status::OK) {
        return {
            .status = Status::FAILED,
            .containerStatus = encoded.status,
            .loadStatus = result.loadStatus,
            .overwriteSafe = false,
            .bytesWritten = encoded.bytesWritten,
        };
    }
    result.bytesWritten = encoded.bytesWritten;
    return result;
}

FLASHMEM const char* statusName(Status status) {
    switch (status) {
        case Status::CURRENT:
            return "current";
        case Status::PARTIAL:
            return "partial";
        case Status::FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

}  // namespace core::persistence::project_file_inspection

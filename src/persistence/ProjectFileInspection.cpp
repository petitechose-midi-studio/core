#include "persistence/ProjectFileInspection.hpp"

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence::project_file_inspection {

namespace {

namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

FLASHMEM bool reportContainsOnlyUnsupportedFormat(
    const project_file::LoadReport& report
) {
    bool foundUnsupported = false;
    for (uint8_t index = 0U; index < report.itemCount; ++index) {
        const auto& item = report.items[index];
        if (item.severity == project_file::LoadSeverity::INFO) continue;
        if (item.code !=
                project_file::LoadCode::UNSUPPORTED_CONTAINER_VERSION &&
            item.code != project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION &&
            item.code != project_file::LoadCode::UNKNOWN_CHUNK &&
            item.code != project_file::LoadCode::MISSING_REQUIRED_CHUNK &&
            item.code != project_file::LoadCode::UNEXPECTED_CHUNK &&
            item.code != project_file::LoadCode::UNSUPPORTED_CHUNK_FLAGS) {
            return false;
        }
        foundUnsupported = true;
    }
    return foundUnsupported;
}

FLASHMEM Result resultFromDecode(
    const snapshot_codec::DecodeResult& decoded,
    uint32_t bytesWritten = 0U
) {
    return {
        .status = decoded.ok ? Status::CURRENT : Status::FAILED,
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
    project_file::LoadReport localReport{};
    auto* effectiveReport = report != nullptr ? report : &localReport;
    project_file::DecodedChunkView chunks[project_file::MAX_CHUNKS]{};
    const auto scanned = project_file::scan(
        data,
        size,
        chunks,
        project_file::MAX_CHUNKS,
        effectiveReport
    );
    if (scanned.status != project_file::Status::OK) {
        return {
            .status = Status::FAILED,
            .containerStatus = scanned.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }
    if (effectiveReport->hasIssues()) {
        if (reportContainsOnlyUnsupportedFormat(*effectiveReport)) {
            return {
                .status = Status::UNSUPPORTED,
                .containerStatus = scanned.status,
                .loadStatus =
                    project_file::LoadStatus::INSPECTION_ISSUES,
                .overwriteSafe = false,
            };
        }
        effectiveReport->markRejected();
        return {
            .status = Status::FAILED,
            .containerStatus = scanned.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }

    auto snapshot = core::state::project::makeProjectSnapshot();
    if (!snapshot) return allocationFailed();
    const auto decoded = snapshot_codec::decodeProjectSnapshot(
        data,
        size,
        *snapshot,
        effectiveReport
    );
    if (!decoded.ok &&
        reportContainsOnlyUnsupportedFormat(*effectiveReport)) {
        effectiveReport->status =
            project_file::LoadStatus::INSPECTION_ISSUES;
        effectiveReport->overwriteSafe = false;
        return {
            .status = Status::UNSUPPORTED,
            .containerStatus = decoded.containerStatus,
            .loadStatus =
                project_file::LoadStatus::INSPECTION_ISSUES,
            .overwriteSafe = false,
        };
    }
    return resultFromDecode(decoded);
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
    project_file::LoadReport* report
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
    if (result.status != Status::CURRENT) return result;

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
        case Status::UNSUPPORTED:
            return "unsupported";
        case Status::FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

}  // namespace core::persistence::project_file_inspection

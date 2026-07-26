#pragma once

#include <array>
#include <cstdint>

namespace core::persistence::project_file {

enum class LoadSeverity : uint8_t {
    INFO = 0,
    WARNING,
    ERROR,
    FATAL,
};

enum class LoadCode : uint8_t {
    OK = 0,
    BUFFER_TOO_SMALL,
    INVALID_MAGIC,
    INVALID_HEADER,
    UNSUPPORTED_CONTAINER_VERSION,
    TOO_MANY_CHUNKS,
    CHUNK_DIRECTORY_INVALID,
    CHUNK_OUT_OF_BOUNDS,
    CHUNK_CRC_MISMATCH,
    CHUNK_PAYLOAD_INVALID,
    UNKNOWN_CHUNK,
    DUPLICATE_CHUNK,
    OUTPUT_CAPACITY_EXCEEDED,
    MISSING_OPTIONAL_CHUNK,
    DEFAULTED_CHUNK,
    UNSUPPORTED_CHUNK_VERSION,
};

enum class LoadStatus : uint8_t {
    OK = 0,
    PARTIAL,
    FAILED,
};

struct LoadReportItem {
    LoadSeverity severity = LoadSeverity::INFO;
    LoadCode code = LoadCode::OK;
    uint32_t chunkId = 0;
    uint8_t sourceMajor = 0;
    uint8_t sourceMinor = 0;
    uint8_t targetMajor = 0;
    uint8_t targetMinor = 0;
};

struct LoadReport {
    static constexpr uint8_t MAX_ITEMS = 24;

    LoadStatus status = LoadStatus::OK;
    bool overwriteSafe = true;
    bool hasUnknownUnsupportedData = false;
    std::array<LoadReportItem, MAX_ITEMS> items{};
    uint8_t itemCount = 0;

    void reset();
    void add(LoadSeverity severity,
             LoadCode code,
             uint32_t chunkId = 0,
             uint8_t sourceMajor = 0,
             uint8_t sourceMinor = 0,
             uint8_t targetMajor = 0,
             uint8_t targetMinor = 0);

    bool ok() const { return status == LoadStatus::OK; }
    bool hasIssues() const { return status != LoadStatus::OK; }
    bool failed() const { return status == LoadStatus::FAILED; }
};

}  // namespace core::persistence::project_file

#pragma once

#include <cstdint>

namespace core::persistence {

enum class PersistenceWriteStatus : uint8_t {
    OK = 0,
    INVALID_CONFIG,
    STORAGE_UNAVAILABLE,
    OUT_OF_RANGE,
    PAYLOAD_TOO_LARGE,
    IO_ERROR,
    ERASE_FAILED,
    COMMIT_FAILED,
};

inline const char* persistenceWriteStatusLabel(PersistenceWriteStatus status) {
    switch (status) {
        case PersistenceWriteStatus::OK: return "OK";
        case PersistenceWriteStatus::INVALID_CONFIG: return "INVALID_CONFIG";
        case PersistenceWriteStatus::STORAGE_UNAVAILABLE: return "STORAGE_UNAVAILABLE";
        case PersistenceWriteStatus::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case PersistenceWriteStatus::PAYLOAD_TOO_LARGE: return "PAYLOAD_TOO_LARGE";
        case PersistenceWriteStatus::IO_ERROR: return "IO_ERROR";
        case PersistenceWriteStatus::ERASE_FAILED: return "ERASE_FAILED";
        case PersistenceWriteStatus::COMMIT_FAILED: return "COMMIT_FAILED";
        default: return "UNKNOWN";
    }
}

}  // namespace core::persistence

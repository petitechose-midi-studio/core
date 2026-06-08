#pragma once

#include <cstdint>

#include "persistence/ProjectFileContainer.hpp"

namespace core::persistence::project_migration {

enum class Status : uint8_t {
    NOT_NEEDED = 0,
    MIGRATED,
    UNSUPPORTED,
    INVALID_PAYLOAD,
    OUTPUT_TOO_SMALL,
};

struct Result {
    Status status = Status::NOT_NEEDED;
    uint32_t bytesWritten = 0;
};

Result migrateToCurrent(const core::persistence::project_file::DecodedChunkView& chunk,
                        uint8_t* out,
                        uint32_t outCapacity);

}  // namespace core::persistence::project_migration

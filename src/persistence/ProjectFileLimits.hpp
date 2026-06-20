#pragma once

#include <cstdint>

namespace core::persistence {

inline constexpr uint32_t PROJECT_FILE_MAX_SIZE = 98304;
inline constexpr uint32_t PROJECT_FILE_WRITE_CHUNK_SIZE = 4096;

}  // namespace core::persistence

#pragma once

#include <cstddef>
#include <cstdint>

namespace core::persistence::checksum {

inline constexpr uint32_t CRC32_INITIAL_STATE = 0xFFFFFFFFU;

uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t size);

constexpr uint32_t crc32Finish(uint32_t state) {
    return ~state;
}

uint32_t crc32(const uint8_t* data, size_t size);

}  // namespace core::persistence::checksum

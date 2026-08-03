#include "persistence/PersistenceChecksum.hpp"

#include <config/PlatformCompat.hpp>

namespace core::persistence::checksum {

FLASHMEM uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        state ^= static_cast<uint32_t>(data[i]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(-(static_cast<int32_t>(state & 1U)));
            state = (state >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return state;
}

FLASHMEM uint32_t crc32(const uint8_t* data, size_t size) {
    return crc32Finish(crc32Update(CRC32_INITIAL_STATE, data, size));
}

}  // namespace core::persistence::checksum

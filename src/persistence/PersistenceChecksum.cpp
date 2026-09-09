#include "persistence/PersistenceChecksum.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

namespace core::persistence::checksum {

namespace {
// Four bits per lookup: 64 bytes in Flash, no RAM table or runtime setup.
constexpr auto nibbleTable PROGMEM = [] {
    std::array<uint32_t, 16> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t state = i;
        for (unsigned bit = 0; bit < 4; ++bit) {
            state = (state >> 1U) ^ ((state & 1U) ? 0xEDB88320U : 0U);
        }
        table[i] = state;
    }
    return table;
}();
}

FLASHMEM uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        state ^= static_cast<uint32_t>(data[i]);
        state = (state >> 4U) ^ nibbleTable[state & 0xFU];
        state = (state >> 4U) ^ nibbleTable[state & 0xFU];
    }
    return state;
}

FLASHMEM uint32_t crc32(const uint8_t* data, size_t size) {
    return crc32Finish(crc32Update(CRC32_INITIAL_STATE, data, size));
}

}  // namespace core::persistence::checksum

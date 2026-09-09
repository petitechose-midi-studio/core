#include "persistence/PersistenceChecksum.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace {
uint32_t referenceUpdate(uint32_t state, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        state ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            state = (state >> 1U) ^ ((state & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return state;
}
}

int main() {
    namespace crc = core::persistence::checksum;
    constexpr uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(crc::crc32(check, sizeof(check)) == 0xCBF43926U);
    assert(crc::crc32(nullptr, 0) == 0U);
    std::array<uint8_t, 65537> bytes{};
    uint32_t random = 0x12345678U;
    for (auto& byte : bytes) {
        random = random * 1664525U + 1013904223U;
        byte = static_cast<uint8_t>(random >> 24U);
    }
    for (const uint32_t seed : {0U, 1U, 0x12345678U, crc::CRC32_INITIAL_STATE}) {
        for (size_t length = 0; length <= 1024; ++length) {
            const auto* data = bytes.data() + (length % 4);
            const uint32_t expected = referenceUpdate(seed, data, length);
            assert(crc::crc32Update(seed, data, length) == expected);
            const size_t split = length / 3;
            const uint32_t partial = crc::crc32Update(seed, data, split);
            assert(crc::crc32Update(partial, data + split, length - split) == expected);
        }
        assert(crc::crc32Update(seed, nullptr, 0) == seed);
    }
    for (const size_t length : {8U, 448U, 4096U, 65536U}) {
        const size_t repeats = 8U * 1024U * 1024U / length;
        uint32_t state = crc::CRC32_INITIAL_STATE;
        const auto started = std::chrono::steady_clock::now();
        for (size_t i = 0; i < repeats; ++i) {
            state = crc::crc32Update(state, bytes.data() + 1, length);
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;
        // Keep the measured result observable; no machine-dependent timing assertion.
        std::cout << "[MEASURE] crc length=" << length << " repeats=" << repeats
                  << " total_us=" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
                  << " state=" << state << '\n';
        assert(crc::crc32(bytes.data() + 1, length) ==
               crc::crc32Finish(referenceUpdate(crc::CRC32_INITIAL_STATE, bytes.data() + 1, length)));
    }
    std::cout << "[PASS] checksum known answer, unaligned, arbitrary state and streaming\n";
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/MidiSyncState.hpp"

namespace core::state::midi_sync_policy {

inline constexpr std::array<MidiSyncMode, 3> MODES = {
    MidiSyncMode::MASTER,
    MidiSyncMode::SLAVE,
    MidiSyncMode::AUTO,
};

inline constexpr std::array<bool, 2> FOLLOW_TRANSPORT = {
    false,
    true,
};

inline constexpr std::array<uint16_t, 7> AUTO_FALLBACK_MS = {
    150,
    250,
    500,
    750,
    1000,
    1500,
    2000,
};

inline constexpr std::array<uint8_t, 8> AUTO_LOCK_CLOCKS = {
    1,
    2,
    3,
    4,
    6,
    8,
    12,
    24,
};

template <typename T, size_t N>
constexpr bool contains(const std::array<T, N>& values, const T& candidate) {
    for (const auto& value : values) {
        if (value == candidate) return true;
    }
    return false;
}

constexpr bool validMode(MidiSyncMode mode) {
    return contains(MODES, mode);
}

constexpr bool validAutoFallbackMs(uint16_t fallbackMs) {
    return contains(AUTO_FALLBACK_MS, fallbackMs);
}

constexpr bool validAutoLockClockCount(uint8_t lockClocks) {
    return contains(AUTO_LOCK_CLOCKS, lockClocks);
}

}  // namespace core::state::midi_sync_policy

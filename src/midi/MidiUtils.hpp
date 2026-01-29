#pragma once

#include <cstdint>

namespace core::midi {

static constexpr float CC_MAX = 127.0f;

// Convert normalized [0.0, 1.0] to MIDI CC [0, 127] with rounding.
inline uint8_t toCC(float normalized) {
    if (normalized <= 0.0f) return 0;
    if (normalized >= 1.0f) return 127;
    return static_cast<uint8_t>(normalized * CC_MAX + 0.5f);
}

// Convert MIDI CC [0, 127] to normalized [0.0, 1.0].
inline float fromCC(uint8_t cc) {
    return static_cast<float>(cc) / CC_MAX;
}

}  // namespace core::midi

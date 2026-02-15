#pragma once

/**
 * @file SequencerInputUtils.hpp
 * @brief Shared helpers for sequencer input value conversions.
 */

#include <algorithm>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::input_utils {

inline float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline int normalizedToInclusiveInt(float normalized, int maxInclusive) {
    if (maxInclusive <= 0) return 0;

    const float value = clampNormalized(normalized);
    const int rounded = static_cast<int>(value * static_cast<float>(maxInclusive) + 0.5f);
    return std::clamp(rounded, 0, maxInclusive);
}

inline int normalizedToIndex(float normalized, int itemCount) {
    if (itemCount <= 1) return 0;
    return normalizedToInclusiveInt(normalized, itemCount - 1);
}

inline float indexToNormalized(int index, int itemCount) {
    if (itemCount <= 1) return 0.0f;

    const int clamped = std::clamp(index, 0, itemCount - 1);
    return static_cast<float>(clamped) / static_cast<float>(itemCount - 1);
}

inline uint8_t normalizedToMidi7(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, 127));
}

inline uint16_t normalizedToGatePercent(float normalized) {
    return static_cast<uint16_t>(
        normalizedToInclusiveInt(normalized, core::state::sequencer::SequencerState::MAX_GATE_PERCENT)
    );
}

inline float gatePercentToNormalized(uint16_t gatePercent) {
    return indexToNormalized(
        gatePercent,
        static_cast<int>(core::state::sequencer::SequencerState::MAX_GATE_PERCENT) + 1
    );
}

}  // namespace core::handler::sequencer::input_utils

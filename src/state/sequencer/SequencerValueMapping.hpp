#pragma once

#include "state/shared/NormalizedValue.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer::value_mapping {

inline constexpr float GATE_UNIT_NORMALIZED_POINT = 0.5f;
inline constexpr int PROBABILITY_MAX = 100;
inline constexpr int NUDGE_MIN = -50;
inline constexpr int NUDGE_MAX = 50;

inline uint8_t normalizedToMidi7(float normalized) {
    return static_cast<uint8_t>(normalized::normalizedToInclusiveInt(normalized, 127));
}

inline uint16_t normalizedToGatePercent(float normalized) {
    const float value = normalized::clampNormalized(normalized);
    constexpr uint16_t unitGate = SequencerPatternState::DEFAULT_GATE_PERCENT;
    constexpr uint16_t maxGate = SequencerPatternState::MAX_GATE_PERCENT;
    if constexpr (maxGate <= unitGate) {
        return static_cast<uint16_t>(normalized::normalizedToInclusiveInt(value, maxGate));
    }

    if (value <= GATE_UNIT_NORMALIZED_POINT) {
        const float scaled = value / GATE_UNIT_NORMALIZED_POINT;
        return static_cast<uint16_t>(normalized::normalizedToInclusiveInt(scaled, unitGate));
    }

    const float scaled =
        (value - GATE_UNIT_NORMALIZED_POINT) / (1.0f - GATE_UNIT_NORMALIZED_POINT);
    const int extended =
        static_cast<int>(unitGate) +
        normalized::normalizedToInclusiveInt(scaled, static_cast<int>(maxGate - unitGate));
    return static_cast<uint16_t>(std::clamp(extended, 0, static_cast<int>(maxGate)));
}

inline uint8_t normalizedToProbability(float normalized) {
    return static_cast<uint8_t>(normalized::normalizedToInclusiveInt(normalized, PROBABILITY_MAX));
}

inline float gatePercentToNormalized(int gatePercent) {
    constexpr uint16_t unitGate = SequencerPatternState::DEFAULT_GATE_PERCENT;
    constexpr uint16_t maxGate = SequencerPatternState::MAX_GATE_PERCENT;
    const int clamped = std::clamp(gatePercent, 0, static_cast<int>(maxGate));
    if constexpr (maxGate <= unitGate) {
        return normalized::indexToNormalized(clamped, static_cast<int>(maxGate) + 1);
    }
    if (clamped <= unitGate) {
        return (static_cast<float>(clamped) / static_cast<float>(unitGate)) *
               GATE_UNIT_NORMALIZED_POINT;
    }
    const float extended =
        static_cast<float>(clamped - unitGate) / static_cast<float>(maxGate - unitGate);
    return GATE_UNIT_NORMALIZED_POINT +
           extended * (1.0f - GATE_UNIT_NORMALIZED_POINT);
}

inline float probabilityToNormalized(int probability) {
    return normalized::indexToNormalized(
        std::clamp(probability, 0, PROBABILITY_MAX),
        PROBABILITY_MAX + 1
    );
}

inline int8_t normalizedToNudge(float normalized) {
    const int index = normalized::normalizedToInclusiveInt(normalized, NUDGE_MAX - NUDGE_MIN);
    return static_cast<int8_t>(NUDGE_MIN + index);
}

inline float nudgeToNormalized(int nudge) {
    return normalized::indexToNormalized(std::clamp(nudge, NUDGE_MIN, NUDGE_MAX) - NUDGE_MIN,
                                         (NUDGE_MAX - NUDGE_MIN) + 1);
}

}  // namespace core::state::sequencer::value_mapping

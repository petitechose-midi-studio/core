#pragma once

#include <cstdint>

namespace core::state::sequencer {

enum class StepProperty : uint8_t {
    NOTE = 0,
    VELOCITY = 1,
    GATE = 2,
    NUDGE = 3,
    PROBABILITY = 4,
};

inline constexpr bool stepPropertySupportsLocalVariation(StepProperty property) {
    return property != StepProperty::PROBABILITY;
}

}  // namespace core::state::sequencer

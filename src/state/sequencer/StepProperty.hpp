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

}  // namespace core::state::sequencer

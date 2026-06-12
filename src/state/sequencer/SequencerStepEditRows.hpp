#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::state::sequencer::step_edit_rows {

inline constexpr std::array<StepProperty, 5> PROPERTIES = {
    StepProperty::NOTE,
    StepProperty::VELOCITY,
    StepProperty::GATE,
    StepProperty::NUDGE,
    StepProperty::PROBABILITY,
};

inline constexpr std::array<const char*, PROPERTIES.size()> KEYS = {
    "Note",
    "Velocity",
    "Gate",
    "Nudge",
    "Probability",
};

inline constexpr uint8_t ACTIVATED = 0;
inline constexpr uint8_t PROPERTY_OFFSET = 1;
inline constexpr uint8_t MICRO_SEQUENCE = PROPERTY_OFFSET + static_cast<uint8_t>(PROPERTIES.size());
inline constexpr uint8_t CYCLE_STATES = MICRO_SEQUENCE + 1U;
inline constexpr uint8_t COUNT = CYCLE_STATES + 1U;

inline bool isActivated(uint8_t row) {
    return row == ACTIVATED;
}

inline bool isProperty(uint8_t row) {
    return row >= PROPERTY_OFFSET && row < MICRO_SEQUENCE;
}

inline bool isContext(uint8_t row) {
    return row == MICRO_SEQUENCE || row == CYCLE_STATES;
}

inline StepProperty propertyForRow(uint8_t row) {
    return PROPERTIES[static_cast<size_t>(row - PROPERTY_OFFSET)];
}

inline StepContentChildKind childKindForContextRow(uint8_t row) {
    return row == MICRO_SEQUENCE
        ? StepContentChildKind::MICRO_SEQUENCE
        : StepContentChildKind::CYCLE_STATES;
}

}  // namespace core::state::sequencer::step_edit_rows

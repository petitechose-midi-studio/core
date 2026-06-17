#pragma once

#include <array>
#include <cstddef>
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
    "Pitch",
    "Velocity",
    "Gate",
    "Nudge",
    "Chance",
};

inline constexpr uint8_t ACTIVATED = 0;
inline constexpr uint8_t PROPERTY_OFFSET = 1;
inline constexpr uint8_t MICRO_SEQUENCE = PROPERTY_OFFSET + static_cast<uint8_t>(PROPERTIES.size());
inline constexpr uint8_t CYCLE_STATES = MICRO_SEQUENCE + 1U;
inline constexpr uint8_t COUNT = CYCLE_STATES + 1U;

inline constexpr std::array<uint8_t, COUNT> NAVIGATION_ORDER = {
    ACTIVATED,
    static_cast<uint8_t>(PROPERTY_OFFSET + 4U),
    static_cast<uint8_t>(PROPERTY_OFFSET + 0U),
    static_cast<uint8_t>(PROPERTY_OFFSET + 1U),
    static_cast<uint8_t>(PROPERTY_OFFSET + 2U),
    static_cast<uint8_t>(PROPERTY_OFFSET + 3U),
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

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

inline int navigationIndexForRow(uint8_t row) {
    for (size_t i = 0; i < NAVIGATION_ORDER.size(); ++i) {
        if (NAVIGATION_ORDER[i] == row) return static_cast<int>(i);
    }
    return 0;
}

inline uint8_t rowForNavigationIndex(int index) {
    const int count = static_cast<int>(NAVIGATION_ORDER.size());
    const int wrapped = ((index % count) + count) % count;
    return NAVIGATION_ORDER[static_cast<size_t>(wrapped)];
}

}  // namespace core::state::sequencer::step_edit_rows

#pragma once

#include <cstdint>

#include "state/sequencer/StepProperty.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer::semantic {

enum class Tone : uint8_t {
    STATE,
    CHANCE,
    PITCH,
    VELOCITY,
    GATE,
    NUDGE,
    MICRO_SEQUENCE,
    CYCLE_STATE,
};

inline constexpr uint32_t color(Tone tone) {
    switch (tone) {
        case Tone::STATE:
            return standalone::theme::color::STEP_STATE;
        case Tone::CHANCE:
            return standalone::theme::color::STEP_CHANCE;
        case Tone::PITCH:
            return standalone::theme::color::STEP_PITCH;
        case Tone::VELOCITY:
            return standalone::theme::color::STEP_VELOCITY;
        case Tone::GATE:
            return standalone::theme::color::STEP_GATE;
        case Tone::NUDGE:
            return standalone::theme::color::STEP_NUDGE;
        case Tone::MICRO_SEQUENCE:
            return standalone::theme::color::STEP_MICRO_SEQUENCE;
        case Tone::CYCLE_STATE:
            return standalone::theme::color::STEP_CYCLE_STATE;
    }
    return 0xB8C4D1;
}

inline constexpr const char* label(Tone tone) {
    switch (tone) {
        case Tone::STATE:
            return "State";
        case Tone::CHANCE:
            return "Chance";
        case Tone::PITCH:
            return "Pitch";
        case Tone::VELOCITY:
            return "Velocity";
        case Tone::GATE:
            return "Gate";
        case Tone::NUDGE:
            return "Nudge";
        case Tone::MICRO_SEQUENCE:
            return "Micro sequence";
        case Tone::CYCLE_STATE:
            return "Cycle state";
    }
    return "Step";
}

inline constexpr Tone toneForProperty(core::state::sequencer::StepProperty property) {
    using core::state::sequencer::StepProperty;
    switch (property) {
        case StepProperty::NOTE:
            return Tone::PITCH;
        case StepProperty::VELOCITY:
            return Tone::VELOCITY;
        case StepProperty::GATE:
            return Tone::GATE;
        case StepProperty::NUDGE:
            return Tone::NUDGE;
        case StepProperty::PROBABILITY:
            return Tone::CHANCE;
    }
    return Tone::PITCH;
}

inline constexpr uint32_t colorForProperty(core::state::sequencer::StepProperty property) {
    return color(toneForProperty(property));
}

inline constexpr const char* labelForProperty(core::state::sequencer::StepProperty property) {
    return label(toneForProperty(property));
}

}  // namespace core::ui::sequencer::semantic

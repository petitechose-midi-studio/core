#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "midi/MidiUtils.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

inline const char* stepPropertyName(StepProperty property) {
    switch (property) {
        case StepProperty::NOTE:
            return "Note";
        case StepProperty::VELOCITY:
            return "Velocity";
        case StepProperty::GATE:
            return "Gate";
        case StepProperty::NUDGE:
            return "Nudge";
        case StepProperty::PROBABILITY:
            return "Probability";
    }
    return "Note";
}

inline void formatStepPropertyValue(
    char* buffer,
    size_t bufferSize,
    StepProperty property,
    uint8_t note,
    uint8_t velocity,
    uint16_t gate,
    int8_t nudge = 0,
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY
) {
    if (!buffer || bufferSize == 0) return;

    switch (property) {
        case StepProperty::NOTE:
            core::midi::formatNoteName(buffer, bufferSize, note);
            return;
        case StepProperty::VELOCITY:
            std::snprintf(buffer, bufferSize, "%u", static_cast<unsigned>(velocity));
            return;
        case StepProperty::NUDGE:
            std::snprintf(buffer, bufferSize, "%+d%%", static_cast<int>(nudge));
            return;
        case StepProperty::PROBABILITY:
            std::snprintf(buffer, bufferSize, "%u%%", static_cast<unsigned>(probability));
            return;
        case StepProperty::GATE:
        default:
            break;
    }

    const uint16_t safeGate = std::min<uint16_t>(gate, SequencerState::MAX_GATE_PERCENT);
    std::snprintf(buffer, bufferSize, "%u%%", static_cast<unsigned>(safeGate));
}

}  // namespace core::state::sequencer

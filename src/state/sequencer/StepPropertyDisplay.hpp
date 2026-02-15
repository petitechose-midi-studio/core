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
    }
    return "Note";
}

inline void formatStepPropertyValue(
    char* buffer,
    size_t bufferSize,
    StepProperty property,
    uint8_t note,
    uint8_t velocity,
    uint16_t gate
) {
    if (!buffer || bufferSize == 0) return;

    if (property == StepProperty::NOTE) {
        core::midi::formatNoteName(buffer, bufferSize, note);
        return;
    }

    if (property == StepProperty::VELOCITY) {
        std::snprintf(buffer, bufferSize, "%u", static_cast<unsigned>(velocity));
        return;
    }

    const uint16_t safeGate = std::min<uint16_t>(gate, SequencerState::MAX_GATE_PERCENT);
    std::snprintf(buffer, bufferSize, "%u%%", static_cast<unsigned>(safeGate));
}

}  // namespace core::state::sequencer

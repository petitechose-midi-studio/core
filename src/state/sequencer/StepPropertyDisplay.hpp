#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <oc/type/TextFormat.hpp>

#include "midi/MidiUtils.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

inline const char* stepPropertyName(StepProperty property) {
    switch (property) {
        case StepProperty::NOTE:
            return "Pitch";
        case StepProperty::VELOCITY:
            return "Velocity";
        case StepProperty::GATE:
            return "Gate";
        case StepProperty::NUDGE:
            return "Nudge";
        case StepProperty::PROBABILITY:
            return "Chance";
    }
    return "Pitch";
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
            oc::type::text::formatUnsigned(buffer, bufferSize, static_cast<unsigned>(velocity));
            return;
        case StepProperty::NUDGE:
            oc::type::text::formatSignedPercent(buffer, bufferSize, static_cast<int>(nudge));
            return;
        case StepProperty::PROBABILITY:
            oc::type::text::formatUnsignedPercent(buffer, bufferSize, static_cast<unsigned>(probability));
            return;
        case StepProperty::GATE:
        default:
            break;
    }

    const uint16_t safeGate = std::min<uint16_t>(gate, SequencerState::MAX_GATE_PERCENT);
    if (safeGate <= SequencerState::DEFAULT_GATE_PERCENT) {
        oc::type::text::formatUnsignedPercent(buffer, bufferSize, static_cast<unsigned>(safeGate));
        return;
    }

    if ((safeGate % 100U) == 0U) {
        std::snprintf(buffer, bufferSize, "%ux", static_cast<unsigned>(safeGate / 100U));
        return;
    }
    if ((safeGate % 10U) == 0U) {
        std::snprintf(
            buffer,
            bufferSize,
            "%u.%ux",
            static_cast<unsigned>(safeGate / 100U),
            static_cast<unsigned>((safeGate % 100U) / 10U)
        );
        return;
    }
    std::snprintf(
        buffer,
        bufferSize,
        "%u.%02ux",
        static_cast<unsigned>(safeGate / 100U),
        static_cast<unsigned>(safeGate % 100U)
    );
}

inline void formatStepPropertyResolvedOffsetValue(
    char* buffer,
    size_t bufferSize,
    StepProperty property,
    uint8_t note,
    uint8_t velocity,
    uint16_t gate,
    int8_t nudge,
    uint8_t probability,
    int16_t offset,
    bool noteOffsetUsesScaleDegrees
) {
    if (!buffer || bufferSize == 0) return;

    char value[8] = {};
    formatStepPropertyValue(value, sizeof(value), property, note, velocity, gate, nudge, probability);

    const char sign = offset >= 0 ? '+' : '-';
    const int magnitude = offset >= 0 ? offset : -offset;
    const char* unit = (property == StepProperty::NOTE && noteOffsetUsesScaleDegrees) ? "d" : "";
    std::snprintf(buffer, bufferSize, "%s %c%d%s", value, sign, magnitude, unit);
}

}  // namespace core::state::sequencer

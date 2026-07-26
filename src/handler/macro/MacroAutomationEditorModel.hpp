#pragma once

#include <cstdint>

#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::handler {

struct MacroAutomationEditRange {
    uint8_t minBeat = 0;
    uint8_t beatStep = 1;
    uint8_t stepCount = 1;
};

constexpr uint8_t MACRO_AUTOMATION_EDITOR_MIN_DURATION_BEATS = 1;
constexpr uint8_t MACRO_AUTOMATION_EDITOR_MAX_DURATION_BEATS = 64;
constexpr uint8_t MACRO_AUTOMATION_EDITOR_COARSE_BEAT_STEP = 4;

MacroAutomationEditRange macroAutomationLengthEditRange(bool coarse);
MacroAutomationEditRange macroAutomationOffsetEditRange(
    const core::state::modulation::ProjectControlCurveView* automation,
    bool coarse);

float macroAutomationEncoderPositionToBeat(float normalized,
                                           const MacroAutomationEditRange& range);
float macroAutomationTicksToEncoderPosition(uint16_t ticks,
                                            const MacroAutomationEditRange& range);

}  // namespace core::handler

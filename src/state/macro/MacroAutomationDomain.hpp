#pragma once

#include <array>
#include <cstdint>

namespace core::state::macro {

static constexpr uint8_t MACRO_AUTOMATION_MAX_POINTS = 32;

enum class MacroAutomationInterpolation : uint8_t {
    LINEAR = 0,
};

enum class MacroAutomationConversionPolicy : uint8_t {
    MEAN = 0,
    FIRST = 1,
    MIN = 2,
};

struct MacroCurvePoint {
    float beat = 0.0f;
    float value = 0.0f;
};

// Absolute macro automation recorded in musical time. Points are stored in
// chronological order and values are normalized to 0..1 at the domain boundary.
struct MacroAutomationLane {
    bool active = false;
    float durationBeats = 1.0f;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
    uint8_t pointCount = 0;
    std::array<MacroCurvePoint, MACRO_AUTOMATION_MAX_POINTS> points{};
};

// Relative modulation shape converted from automation or authored directly.
// Values are signed offsets and are scaled by MacroAutomationSlotState depth.
struct MacroModulationShape {
    bool active = false;
    float durationBeats = 1.0f;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
    uint8_t pointCount = 0;
    std::array<MacroCurvePoint, MACRO_AUTOMATION_MAX_POINTS> points{};
};

struct MacroAutomationSlotState {
    MacroAutomationLane automation;
    MacroModulationShape modulation;
    float modulationDepth = 0.0f;
};

struct MacroResolvedValue {
    float base = 0.0f;
    float modulation = 0.0f;
    float resolved = 0.0f;
    bool automationActive = false;
    bool modulationActive = false;
};

float macroAutomationClamp01(float value);
float macroAutomationClampSigned(float value);
float macroAutomationQuantizeDurationBeats(float rawDurationBeats);

bool macroAutomationAppendPoint(MacroAutomationLane& lane, float beat, float value);
bool macroModulationAppendPoint(MacroModulationShape& shape, float beat, float value);

void macroAutomationFinalizeRecording(MacroAutomationLane& lane, float rawDurationBeats);

float macroAutomationEvaluate(const MacroAutomationLane& lane, float beat, float fallbackValue);
float macroModulationEvaluate(const MacroModulationShape& shape, float beat);

bool macroAutomationConvertToModulation(const MacroAutomationLane& automation,
                                        MacroAutomationConversionPolicy policy,
                                        MacroModulationShape& outShape);

MacroResolvedValue macroResolveValue(float staticValue,
                                     const MacroAutomationSlotState& slot,
                                     float beat);

}  // namespace core::state::macro

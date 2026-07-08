#pragma once

#include <array>
#include <cstdint>

namespace core::state::macro {

static constexpr uint16_t MACRO_AUTOMATION_TICKS_PER_BEAT = 192;
static constexpr uint16_t MACRO_AUTOMATION_RECORDING_MAX_POINTS = 2048;
static constexpr uint16_t MACRO_AUTOMATION_POINT_POOL_CAPACITY = 32768;

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

struct MacroPackedCurvePoint {
    uint16_t tick = 0;
    int16_t value = 0;
};

struct MacroAutomationPointPool {
    uint16_t used = 0;
    std::array<MacroPackedCurvePoint, MACRO_AUTOMATION_POINT_POOL_CAPACITY> points{};
};

struct MacroAutomationCurveRef {
    bool active = false;
    uint16_t pointOffset = 0;
    uint16_t pointCount = 0;
    uint16_t sourceDurationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t durationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t windowOffsetTicks = 0;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
};

// Temporary absolute lane used while recording or authoring. Durable project
// storage uses MacroAutomationCurveRef + MacroAutomationPointPool instead.
struct MacroAutomationLane {
    bool active = false;
    float durationBeats = 1.0f;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
    uint16_t pointCount = 0;
    std::array<MacroCurvePoint, MACRO_AUTOMATION_RECORDING_MAX_POINTS> points{};
};

// Temporary relative modulation shape. Values are signed offsets and are
// scaled by MacroAutomationSlotState depth when persisted into the point pool.
struct MacroModulationShape {
    bool active = false;
    float durationBeats = 1.0f;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
    uint16_t pointCount = 0;
    std::array<MacroCurvePoint, MACRO_AUTOMATION_RECORDING_MAX_POINTS> points{};
};

struct MacroAutomationSlotState {
    MacroAutomationCurveRef automation;
    MacroAutomationCurveRef modulation;
    float modulationDepth = 0.0f;
};

struct MacroResolvedValue {
    float base = 0.0f;
    float modulation = 0.0f;
    float resolved = 0.0f;
    bool automationActive = false;
    bool modulationActive = false;
};

struct MacroAutomationCurveWindowSummary {
    bool active = false;
    uint16_t sourceDurationTicks = 0;
    uint16_t durationTicks = 0;
    uint16_t windowOffsetTicks = 0;
    uint16_t firstPointTick = 0;
    uint16_t lastPointTick = 0;
    uint16_t pointCount = 0;
    bool wraps = false;
};

float macroAutomationClamp01(float value);
float macroAutomationClampSigned(float value);
float macroAutomationElapsedBeats(uint32_t startedAtMs, uint32_t nowMs, float tempoBpm);
float macroAutomationQuantizeDurationBeats(float rawDurationBeats);
float macroAutomationBeatsFromTicks(uint16_t ticks);
uint16_t macroAutomationTicksFromBeats(float beats);
int16_t macroAutomationPackValue(float value, bool signedInput);
float macroAutomationUnpackValue(int16_t packed, bool signedOutput);

bool macroAutomationAppendPoint(MacroAutomationLane& lane, float beat, float value);
bool macroModulationAppendPoint(MacroModulationShape& shape, float beat, float value);

void macroAutomationFinalizeRecording(MacroAutomationLane& lane, float rawDurationBeats);
void macroAutomationFinalizeRecordingWithDuration(MacroAutomationLane& lane,
                                                  float rawDurationBeats,
                                                  float targetDurationBeats);

float macroAutomationEvaluate(const MacroAutomationLane& lane, float beat, float fallbackValue);
float macroModulationEvaluate(const MacroModulationShape& shape, float beat);
float macroAutomationEvaluate(const MacroAutomationCurveRef& lane,
                              const MacroAutomationPointPool& pool,
                              float beat,
                              float fallbackValue);
float macroModulationEvaluate(const MacroAutomationCurveRef& shape,
                              const MacroAutomationPointPool& pool,
                              float beat);
bool macroAutomationReadPoint(const MacroAutomationCurveRef& lane,
                              const MacroAutomationPointPool& pool,
                              uint16_t index,
                              bool signedOutput,
                              MacroCurvePoint& out);
bool macroAutomationResizeCurveDuration(MacroAutomationCurveRef& lane,
                                        MacroAutomationPointPool& pool,
                                        float targetDurationBeats);
bool macroAutomationSetCurveWindowOffset(MacroAutomationCurveRef& lane,
                                         const MacroAutomationPointPool& pool,
                                         float targetOffsetBeats);
MacroAutomationCurveWindowSummary macroAutomationCurveWindowSummary(
    const MacroAutomationCurveRef& lane,
    const MacroAutomationPointPool& pool);

bool macroAutomationConvertToModulation(const MacroAutomationLane& automation,
                                        MacroAutomationConversionPolicy policy,
                                        MacroModulationShape& outShape);

MacroResolvedValue macroResolveValue(float staticValue,
                                     const MacroAutomationSlotState& slot,
                                     const MacroAutomationPointPool& pool,
                                     float beat);

}  // namespace core::state::macro

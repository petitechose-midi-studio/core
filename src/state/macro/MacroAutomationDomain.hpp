#pragma once

#include <array>
#include <cstdint>

namespace core::state::macro {

static constexpr uint16_t MACRO_AUTOMATION_TICKS_PER_BEAT = 192;
static constexpr uint16_t MACRO_AUTOMATION_RECORDING_MAX_POINTS = 2048;

enum class MacroAutomationInterpolation : uint8_t {
    LINEAR = 0,
};

struct MacroCurvePoint {
    float beat = 0.0f;
    float value = 0.0f;
};

// Temporary absolute lane used while recording or authoring. Durable Project
// storage uses the graph-native shared curve arena.
struct MacroAutomationLane {
    bool active = false;
    float durationBeats = 1.0f;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
    uint16_t pointCount = 0;
    std::array<MacroCurvePoint, MACRO_AUTOMATION_RECORDING_MAX_POINTS> points{};
};

// This authoring scratch is intentionally cold and must never be embedded in
// a hot/live owner. Durable Project storage uses packed curve points instead.
static_assert(sizeof(MacroAutomationLane) == 16396U);

struct MacroResolvedValue {
    float base = 0.0f;
    float modulation = 0.0f;
    float resolved = 0.0f;
    bool automationStored = false;
    bool modulationStored = false;
    bool automationActive = false;
    bool modulationActive = false;
    bool modulationPausedDepthZero = false;
};

float macroAutomationClamp01(float value);
float macroAutomationClampSigned(float value);
float macroAutomationElapsedBeats(uint32_t startedAtMs, uint32_t nowMs, float tempoBpm);
float macroAutomationQuantizeDurationBeats(float rawDurationBeats);
float macroAutomationBeatsFromTicks(uint16_t ticks);
uint16_t macroAutomationTicksFromBeats(float beats);
int16_t macroAutomationPackValue(float value, bool signedInput);
float macroAutomationUnpackValue(int16_t packed, bool signedOutput);

bool macroAutomationAppendPoint(MacroAutomationLane& lane,
                                float beat,
                                float value,
                                bool* reduced = nullptr);

void macroAutomationFinalizeRecording(MacroAutomationLane& lane, float rawDurationBeats);
void macroAutomationFinalizeRecordingWithDuration(MacroAutomationLane& lane,
                                                  float rawDurationBeats,
                                                  float targetDurationBeats);

float macroAutomationEvaluate(const MacroAutomationLane& lane, float beat, float fallbackValue);

}  // namespace core::state::macro

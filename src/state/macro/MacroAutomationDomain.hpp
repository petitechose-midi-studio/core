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

/**
 * Persisted permission for a stored curve to participate in playback.
 *
 * ACTIVE intentionally remains zero: MAUT 1.4 files wrote zero in the byte
 * now used for this field, so existing stored curves migrate as audible.
 * `active` on MacroAutomationCurveRef continues to mean "data is stored".
 */
enum class MacroCurvePlaybackState : uint8_t {
    ACTIVE = 0,
    OFF = 1,
    // Read-only compatibility value. New interactions never create it;
    // persisted Modulation is normalized to ACTIVE on load.
    SUSPENDED_AFTER_RECORD = 2,
};

/** Provenance retained for semantic conversion feedback and inspection. */
enum class MacroModulationOrigin : uint8_t {
    NATIVE = 0,
    CONVERTED_MEAN = 1,
    CONVERTED_FIRST = 2,
    CONVERTED_MIN = 3,
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
    MacroCurvePlaybackState playbackState = MacroCurvePlaybackState::ACTIVE;
    uint16_t pointOffset = 0;
    uint16_t pointCount = 0;
    uint16_t sourceDurationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t durationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t windowOffsetTicks = 0;
    MacroAutomationInterpolation interpolation = MacroAutomationInterpolation::LINEAR;
    MacroModulationOrigin modulationOrigin = MacroModulationOrigin::NATIVE;
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

// This authoring scratch is intentionally cold and must never be embedded in
// a hot/live owner. Durable Project storage uses packed curve points instead.
static_assert(sizeof(MacroAutomationLane) == 16396U);

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
    bool automationStored = false;
    bool modulationStored = false;
    bool automationActive = false;
    bool modulationActive = false;
    bool modulationPausedDepthZero = false;
    bool modulationSuspended = false;
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
bool macroCurvePlaybackStateValid(MacroCurvePlaybackState state);
bool macroModulationOriginValid(MacroModulationOrigin origin);
bool macroAutomationCurveLifecycleValid(const MacroAutomationCurveRef& curve);
bool macroModulationCurveLifecycleValid(const MacroAutomationCurveRef& curve);
bool macroCurveStored(const MacroAutomationCurveRef& curve);
bool macroCurvePlaybackActive(const MacroAutomationCurveRef& curve);
bool macroCurveSuspendedAfterRecord(const MacroAutomationCurveRef& curve);
MacroModulationOrigin macroModulationOriginForConversion(
    MacroAutomationConversionPolicy policy);
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

MacroResolvedValue macroResolveValue(float staticValue,
                                     const MacroAutomationSlotState& slot,
                                     const MacroAutomationPointPool& pool,
                                     float beat,
                                     bool automationPlaybackEnabled = true);

}  // namespace core::state::macro

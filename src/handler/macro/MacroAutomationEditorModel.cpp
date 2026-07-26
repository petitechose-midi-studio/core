#include "handler/macro/MacroAutomationEditorModel.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

namespace core::handler {

namespace {

FLASHMEM uint8_t sanitizedBeatStep(uint8_t beatStep) {
    return std::max<uint8_t>(1, beatStep);
}

FLASHMEM uint8_t sourceDurationBeatCount(
    const core::state::modulation::ProjectControlCurveView* automation
) {
    if (automation == nullptr || !automation->stored()) return 1;
    const uint16_t sourceTicks = std::max<uint16_t>(
        automation->spec.sourceDurationTicks,
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    const uint16_t sourceBeats = static_cast<uint16_t>(
        (sourceTicks + core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT - 1U) /
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    return static_cast<uint8_t>(std::clamp<uint16_t>(
        sourceBeats,
        1,
        MACRO_AUTOMATION_EDITOR_MAX_DURATION_BEATS
    ));
}

FLASHMEM uint8_t editRangeMaxBeat(const MacroAutomationEditRange& range) {
    if (range.stepCount <= 1U) return range.minBeat;
    return static_cast<uint8_t>(
        range.minBeat +
        ((range.stepCount - 1U) * sanitizedBeatStep(range.beatStep))
    );
}

FLASHMEM uint8_t beatFromTicks(uint16_t ticks, const MacroAutomationEditRange& range) {
    const float beats = core::state::macro::macroAutomationBeatsFromTicks(ticks);
    return static_cast<uint8_t>(std::clamp(
        static_cast<int>(std::lround(beats)),
        static_cast<int>(range.minBeat),
        static_cast<int>(editRangeMaxBeat(range))
    ));
}

}  // namespace

FLASHMEM MacroAutomationEditRange macroAutomationLengthEditRange(bool coarse) {
    const uint8_t step = coarse ? MACRO_AUTOMATION_EDITOR_COARSE_BEAT_STEP : 1;
    const uint8_t minBeat =
        coarse ? MACRO_AUTOMATION_EDITOR_COARSE_BEAT_STEP
               : MACRO_AUTOMATION_EDITOR_MIN_DURATION_BEATS;
    return MacroAutomationEditRange{
        .minBeat = minBeat,
        .beatStep = step,
        .stepCount = static_cast<uint8_t>(
            ((MACRO_AUTOMATION_EDITOR_MAX_DURATION_BEATS - minBeat) / step) + 1U
        ),
    };
}

FLASHMEM MacroAutomationEditRange macroAutomationOffsetEditRange(
    const core::state::modulation::ProjectControlCurveView* automation,
    bool coarse
) {
    const uint8_t step = coarse ? MACRO_AUTOMATION_EDITOR_COARSE_BEAT_STEP : 1;
    const uint8_t sourceBeats = sourceDurationBeatCount(automation);
    return MacroAutomationEditRange{
        .minBeat = 0,
        .beatStep = step,
        .stepCount = static_cast<uint8_t>(((sourceBeats - 1U) / step) + 1U),
    };
}

FLASHMEM float macroAutomationEncoderPositionToBeat(
    float normalized,
    const MacroAutomationEditRange& range
) {
    if (range.stepCount <= 1U) return static_cast<float>(range.minBeat);
    const uint8_t stepSize = sanitizedBeatStep(range.beatStep);
    const int step = static_cast<int>(
        std::clamp(normalized, 0.0f, 1.0f) *
        static_cast<float>(range.stepCount - 1U) +
        0.5f
    );
    return static_cast<float>(
        range.minBeat +
        (std::clamp(step, 0, static_cast<int>(range.stepCount - 1U)) * stepSize)
    );
}

FLASHMEM float macroAutomationTicksToEncoderPosition(
    uint16_t ticks,
    const MacroAutomationEditRange& range
) {
    if (range.stepCount <= 1U) return 0.0f;
    const uint8_t stepSize = sanitizedBeatStep(range.beatStep);
    const uint8_t beat = beatFromTicks(ticks, range);
    const int rawStep = static_cast<int>(std::lround(
        static_cast<float>(static_cast<int>(beat) - static_cast<int>(range.minBeat)) /
        static_cast<float>(stepSize)
    ));
    const int clampedStep = std::clamp(rawStep, 0, static_cast<int>(range.stepCount - 1U));
    return static_cast<float>(clampedStep) / static_cast<float>(range.stepCount - 1U);
}

}  // namespace core::handler

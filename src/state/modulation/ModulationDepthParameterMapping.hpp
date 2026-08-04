#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation::depth {

enum class Scale : uint8_t {
    STANDARD = 0,
    RECORDED_SHAPE_BIPOLAR,
};

inline constexpr int16_t maximumPercent(Scale scale) {
    return scale == Scale::RECORDED_SHAPE_BIPOLAR ? 200 : 100;
}

inline constexpr int stepCount(Scale scale) {
    return static_cast<int>(maximumPercent(scale)) * 2 + 1;
}

inline Scale scaleFor(
    const ModulatorSourceState& source,
    const ProjectCurveArena& curves
) {
    if (source.kind != ModulatorKind::RECORDED_SHAPE) return Scale::STANDARD;
    const auto* curve = findProjectCurve(
        curves,
        source.parameters.recordedCurveId
    );
    return curve != nullptr &&
            curve->valueDomain == ProjectCurveValueDomain::BIPOLAR
        ? Scale::RECORDED_SHAPE_BIPOLAR
        : Scale::STANDARD;
}

inline Scale scaleFor(
    const ProjectModulationState& graph,
    const ProjectCurveArena& curves,
    const ModulationBindingState& binding
) {
    const auto* source = findProjectModulator(graph, binding.sourceId);
    return source != nullptr ? scaleFor(*source, curves) : Scale::STANDARD;
}

inline int16_t amountQ15ToPercent(int16_t amountQ15, Scale scale) {
    const int32_t maximum = maximumPercent(scale);
    const int32_t scaled = static_cast<int32_t>(amountQ15) * maximum;
    return static_cast<int16_t>(
        scaled >= 0 ? (scaled + 16383) / 32767
                    : -((-scaled + 16383) / 32767)
    );
}

inline int16_t percentToAmountQ15(int16_t percent, Scale scale) {
    const int32_t maximum = maximumPercent(scale);
    const int32_t clamped = std::clamp<int32_t>(percent, -maximum, maximum);
    const int32_t scaled = clamped * 32767;
    return static_cast<int16_t>(
        scaled >= 0 ? (scaled + maximum / 2) / maximum
                    : -((-scaled + maximum / 2) / maximum)
    );
}

inline float normalizedPosition(int16_t amountQ15) {
    return std::clamp(
        (static_cast<float>(amountQ15) / 32767.0f + 1.0f) * 0.5f,
        0.0f,
        1.0f
    );
}

inline int16_t percentAtNormalized(float normalized, Scale scale) {
    const int maximum = maximumPercent(scale);
    return static_cast<int16_t>(std::lround(
        std::clamp(normalized, 0.0f, 1.0f) *
            static_cast<float>(maximum * 2) -
        static_cast<float>(maximum)
    ));
}

inline int16_t amountQ15AtNormalized(float normalized, Scale scale) {
    return percentToAmountQ15(percentAtNormalized(normalized, scale), scale);
}

}  // namespace core::state::modulation::depth

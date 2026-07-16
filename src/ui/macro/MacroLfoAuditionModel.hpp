#pragma once

#include <array>
#include <cstdint>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationState.hpp"

namespace core::ui::macro::lfo_audition {

inline constexpr uint8_t SHAPE_COUNT = 5;
inline constexpr uint8_t RATE_COUNT = 6;
inline constexpr uint16_t DEPTH_STEP_COUNT = 201;

inline constexpr std::array<uint32_t, RATE_COUNT> RATE_PERIOD_TICKS{{
    core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT / 4U,
    core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT / 2U,
    core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT,
    core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT * 2U,
    core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT * 4U,
    core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT * 8U,
}};

inline const char* shapeLabel(
    core::state::modulation::ModulatorLfoShape shape
) {
    using Shape = core::state::modulation::ModulatorLfoShape;
    switch (shape) {
        case Shape::TRIANGLE: return "Triangle";
        case Shape::SAW_UP: return "Saw Up";
        case Shape::SAW_DOWN: return "Saw Down";
        case Shape::SQUARE: return "Square";
        case Shape::SINE:
        default: return "Sine";
    }
}

inline const char* rateLabel(uint8_t index) {
    static constexpr std::array<const char*, RATE_COUNT> LABELS{{
        "1/16 Sync",
        "1/8 Sync",
        "1/4 Sync",
        "1/2 Sync",
        "1 Bar Sync",
        "2 Bars Sync",
    }};
    return LABELS[index < RATE_COUNT ? index : 2U];
}

inline uint8_t rateIndex(uint32_t periodTicks) {
    for (uint8_t index = 0; index < RATE_COUNT; ++index) {
        if (RATE_PERIOD_TICKS[index] == periodTicks) return index;
    }
    return 2U;
}

inline int16_t depthPercentToQ15(int16_t percent) {
    const int32_t clamped = percent < -100
        ? -100
        : (percent > 100 ? 100 : percent);
    const int32_t scaled = clamped * 32767;
    return static_cast<int16_t>(
        scaled >= 0 ? (scaled + 50) / 100 : -((-scaled + 50) / 100)
    );
}

inline int16_t depthQ15ToPercent(int16_t depthQ15) {
    const int32_t scaled = static_cast<int32_t>(depthQ15) * 100;
    return static_cast<int16_t>(
        scaled >= 0 ? (scaled + 16383) / 32767
                    : -((-scaled + 16383) / 32767)
    );
}

}  // namespace core::ui::macro::lfo_audition

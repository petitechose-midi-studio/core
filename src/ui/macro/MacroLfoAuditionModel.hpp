#pragma once

#include <cstdint>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationState.hpp"

namespace core::ui::macro::lfo_audition {

inline constexpr uint8_t SHAPE_COUNT = 5;
inline constexpr uint8_t RATE_COUNT = 12;
inline constexpr uint16_t DEPTH_STEP_COUNT = 201;

/** 1/64 note through 32 bars, without a runtime pointer/data table. */
inline constexpr uint32_t ratePeriodTicks(uint8_t index) {
    constexpr uint32_t beat =
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    if (index == 0U) return beat / 16U;
    if (index == 1U) return beat / 8U;
    if (index == 2U) return beat / 4U;
    if (index == 3U) return beat / 2U;
    if (index == 4U) return beat;
    if (index == 5U) return beat * 2U;
    if (index == 6U) return beat * 4U;
    if (index == 7U) return beat * 8U;
    if (index == 8U) return beat * 16U;
    if (index == 9U) return beat * 32U;
    if (index == 10U) return beat * 64U;
    return beat * 128U;
}

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
    if (index == 0U) return "1/64 Sync";
    if (index == 1U) return "1/32 Sync";
    if (index == 2U) return "1/16 Sync";
    if (index == 3U) return "1/8 Sync";
    if (index == 4U) return "1/4 Sync";
    if (index == 5U) return "1/2 Sync";
    if (index == 6U) return "1 Bar Sync";
    if (index == 7U) return "2 Bars Sync";
    if (index == 8U) return "4 Bars Sync";
    if (index == 9U) return "8 Bars Sync";
    if (index == 10U) return "16 Bars Sync";
    return "32 Bars Sync";
}

inline const char* rateCompactLabel(uint8_t index) {
    if (index == 0U) return "1/64";
    if (index == 1U) return "1/32";
    if (index == 2U) return "1/16";
    if (index == 3U) return "1/8";
    if (index == 4U) return "1/4";
    if (index == 5U) return "1/2";
    if (index == 6U) return "1B";
    if (index == 7U) return "2B";
    if (index == 8U) return "4B";
    if (index == 9U) return "8B";
    if (index == 10U) return "16B";
    return "32B";
}

inline uint8_t rateIndex(uint32_t periodTicks) {
    for (uint8_t index = 0; index < RATE_COUNT; ++index) {
        if (ratePeriodTicks(index) == periodTicks) return index;
    }
    return 4U;
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

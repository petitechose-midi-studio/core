#pragma once

#include <cstdint>

#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::state::modulation::lfo {

inline constexpr uint8_t SHAPE_COUNT = 5U;
inline constexpr uint8_t RATE_COUNT = 12U;
inline constexpr uint16_t DEPTH_STEP_COUNT = 201U;

/** 1/64 note through 32 bars, without a runtime pointer/data table. */
inline constexpr uint32_t ratePeriodTicks(uint8_t index) {
    constexpr uint32_t beat = PROJECT_CONTROL_TICKS_PER_BEAT;
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

inline uint8_t rateIndex(uint32_t periodTicks) {
    for (uint8_t index = 0U; index < RATE_COUNT; ++index) {
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

}  // namespace core::state::modulation::lfo

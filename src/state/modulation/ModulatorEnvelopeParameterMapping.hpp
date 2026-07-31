#pragma once

#include <algorithm>
#include <cstdint>

#include "state/modulation/ModulatorEnvelopeTiming.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::state::modulation::envelope {

inline constexpr uint16_t FREE_DURATION_STEP_COUNT =
    MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT;
inline constexpr uint8_t SUSTAIN_STEP_COUNT = 101U;

[[nodiscard]] inline uint16_t durationCount(
    ModulatorTimingMode timing,
    ModulatorEnvelopeTimeParameter parameter
) {
    if (timing == ModulatorTimingMode::FREE) {
        return FREE_DURATION_STEP_COUNT;
    }
    uint16_t count = 0U;
    const uint16_t maximum = maximumModulatorEnvelopeSyncBaseTicks(parameter);
    for (uint16_t value : MODULATOR_ENVELOPE_SYNC_BASE_TICKS) {
        if (value > maximum) break;
        ++count;
    }
    return std::max<uint16_t>(count, 1U);
}

[[nodiscard]] inline uint16_t durationIndex(
    uint16_t duration,
    ModulatorTimingMode timing,
    ModulatorEnvelopeTimeParameter parameter
) {
    if (timing == ModulatorTimingMode::FREE) {
        return modulatorEnvelopeFreeDurationIndex(duration, parameter);
    }
    uint16_t nearest = 0U;
    uint32_t nearestDistance = UINT32_MAX;
    const uint16_t count = durationCount(timing, parameter);
    for (uint16_t index = 0U; index < count; ++index) {
        const uint32_t value = MODULATOR_ENVELOPE_SYNC_BASE_TICKS[index];
        const uint32_t distance = value > duration
            ? value - duration
            : duration - value;
        if (distance < nearestDistance) {
            nearest = index;
            nearestDistance = distance;
        }
    }
    return nearest;
}

[[nodiscard]] inline uint16_t durationAt(
    uint16_t index,
    ModulatorTimingMode timing,
    ModulatorEnvelopeTimeParameter parameter
) {
    const uint16_t count = durationCount(timing, parameter);
    index = std::min<uint16_t>(index, static_cast<uint16_t>(count - 1U));
    if (timing == ModulatorTimingMode::SYNC) {
        return MODULATOR_ENVELOPE_SYNC_BASE_TICKS[index];
    }
    return modulatorEnvelopeFreeDurationAt(index, parameter);
}

inline uint8_t sustainQ15ToPercent(uint16_t sustainQ15) {
    return static_cast<uint8_t>(std::min<uint32_t>(
        100U,
        (static_cast<uint32_t>(sustainQ15) * 100U + 16384U) /
            PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
    ));
}

inline uint16_t sustainPercentToQ15(uint8_t percent) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(std::min<uint8_t>(percent, 100U)) *
         PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15 + 50U) /
        100U
    );
}

}  // namespace core::state::modulation::envelope

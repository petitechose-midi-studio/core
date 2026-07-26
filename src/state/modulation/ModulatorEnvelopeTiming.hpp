#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::modulation {

inline constexpr uint32_t MODULATOR_ENVELOPE_TICKS_PER_BEAT = 192U;
inline constexpr uint16_t MODULATOR_ENVELOPE_FREE_MAX_MS = 30000U;
inline constexpr uint16_t MODULATOR_ENVELOPE_SMOOTH_FREE_MAX_MS = 500U;
inline constexpr uint16_t MODULATOR_ENVELOPE_SYNC_MAX_BASE_TICKS = 12288U;
inline constexpr uint16_t MODULATOR_ENVELOPE_SMOOTH_SYNC_MAX_BASE_TICKS = 384U;
inline constexpr uint16_t MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT = 256U;

inline constexpr std::array<uint16_t, 12U>
    MODULATOR_ENVELOPE_SYNC_BASE_TICKS{
        0U,
        12U,
        24U,
        48U,
        96U,
        192U,
        384U,
        768U,
        1536U,
        3072U,
        6144U,
        12288U,
    };

namespace detail {

using ModulatorEnvelopeDurationMember = uint16_t ModulatorAdsrParameters::*;

inline constexpr std::array<ModulatorEnvelopeDurationMember, 6U>
    MODULATOR_ENVELOPE_DURATION_MEMBERS{
        &ModulatorAdsrParameters::delay,
        &ModulatorAdsrParameters::attack,
        &ModulatorAdsrParameters::hold,
        &ModulatorAdsrParameters::decay,
        &ModulatorAdsrParameters::release,
        &ModulatorAdsrParameters::smooth,
    };

// 2^(1/2), 2^(1/4), ... in unsigned Q16. Keeping them as immediates avoids
// both a mutable table and the roughly 4 KiB newlib log/exp tables that would
// otherwise be copied into RAM1.
[[nodiscard]] constexpr uint32_t exp2FactorQ16(uint8_t bit) {
    switch (bit) {
        case 0U: return 92682U;
        case 1U: return 77936U;
        case 2U: return 71468U;
        case 3U: return 68438U;
        case 4U: return 66971U;
        case 5U: return 66250U;
        case 6U: return 65892U;
        case 7U: return 65714U;
        case 8U: return 65625U;
        case 9U: return 65580U;
        case 10U: return 65558U;
        case 11U: return 65547U;
        case 12U: return 65542U;
        case 13U: return 65539U;
        case 14U: return 65537U;
        case 15U: return 65537U;
        default: return 65536U;
    }
}

[[nodiscard]] constexpr uint32_t exp2FractionQ16(uint16_t fractionQ16) {
    uint64_t resultQ16 = 65536U;
    for (uint8_t bit = 0U; bit < 16U; ++bit) {
        if ((fractionQ16 & (0x8000U >> bit)) == 0U) continue;
        resultQ16 = (
            resultQ16 * exp2FactorQ16(bit) + 32768U
        ) >> 16U;
    }
    return static_cast<uint32_t>(resultQ16);
}

[[nodiscard]] constexpr uint32_t freeMaximumLog2Q16(
    ModulatorEnvelopeTimeParameter parameter
) {
    // round(log2(30000) * 65536), round(log2(500) * 65536)
    return parameter == ModulatorEnvelopeTimeParameter::SMOOTH
        ? 587582U
        : 974696U;
}

static_assert(
    static_cast<std::size_t>(ModulatorEnvelopeTimeParameter::SMOOTH) + 1U ==
    MODULATOR_ENVELOPE_DURATION_MEMBERS.size()
);

[[nodiscard]] constexpr ModulatorEnvelopeDurationMember
modulatorEnvelopeDurationMember(ModulatorEnvelopeTimeParameter parameter) {
    const std::size_t index = static_cast<std::size_t>(parameter);
    return index < MODULATOR_ENVELOPE_DURATION_MEMBERS.size()
        ? MODULATOR_ENVELOPE_DURATION_MEMBERS[index]
        : nullptr;
}

}  // namespace detail

[[nodiscard]] constexpr bool isValidModulatorEnvelopeTimeParameter(
    ModulatorEnvelopeTimeParameter parameter
) {
    return detail::modulatorEnvelopeDurationMember(parameter) != nullptr;
}

[[nodiscard]] constexpr bool isValidModulatorEnvelopeTimingMode(
    ModulatorTimingMode timing
) {
    return timing == ModulatorTimingMode::FREE ||
           timing == ModulatorTimingMode::SYNC;
}

[[nodiscard]] constexpr bool isValidModulatorEnvelopeFeel(
    ModulatorEnvelopeFeel feel
) {
    return feel == ModulatorEnvelopeFeel::STRAIGHT ||
           feel == ModulatorEnvelopeFeel::TRIPLET ||
           feel == ModulatorEnvelopeFeel::DOTTED;
}

/**
 * Returns one raw stored duration without normalizing or clamping it.
 * An invalid parameter identifier returns zero.
 */
[[nodiscard]] constexpr uint16_t modulatorEnvelopeDuration(
    const ModulatorAdsrParameters& parameters,
    ModulatorEnvelopeTimeParameter parameter
) {
    const auto member = detail::modulatorEnvelopeDurationMember(parameter);
    return member == nullptr ? 0U : parameters.*member;
}

/**
 * Replaces one raw duration without applying authoring validation. Returns
 * false only for an invalid parameter identifier.
 */
constexpr bool setModulatorEnvelopeDuration(
    ModulatorAdsrParameters& parameters,
    ModulatorEnvelopeTimeParameter parameter,
    uint16_t duration
) {
    const auto member = detail::modulatorEnvelopeDurationMember(parameter);
    if (member == nullptr) return false;
    parameters.*member = duration;
    return true;
}

[[nodiscard]] constexpr uint16_t maximumModulatorEnvelopeFreeMilliseconds(
    ModulatorEnvelopeTimeParameter parameter
) {
    return parameter == ModulatorEnvelopeTimeParameter::SMOOTH
        ? MODULATOR_ENVELOPE_SMOOTH_FREE_MAX_MS
        : MODULATOR_ENVELOPE_FREE_MAX_MS;
}

[[nodiscard]] constexpr uint16_t maximumModulatorEnvelopeSyncBaseTicks(
    ModulatorEnvelopeTimeParameter parameter
) {
    return parameter == ModulatorEnvelopeTimeParameter::SMOOTH
        ? MODULATOR_ENVELOPE_SMOOTH_SYNC_MAX_BASE_TICKS
        : MODULATOR_ENVELOPE_SYNC_MAX_BASE_TICKS;
}

/**
 * Maps one of 256 authored encoder positions to the logarithmic Free duration
 * law. Zero is explicit; positions 1..255 cover 1 ms through the parameter
 * maximum. The fixed-point approximation differs by at most one millisecond
 * from the former double-precision law at the tested grid points.
 */
[[nodiscard]] constexpr uint16_t modulatorEnvelopeFreeDurationAt(
    uint16_t index,
    ModulatorEnvelopeTimeParameter parameter
) {
    const uint16_t maximum = maximumModulatorEnvelopeFreeMilliseconds(
        parameter
    );
    index = std::min<uint16_t>(
        index,
        MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT - 1U
    );
    if (index == 0U) return 0U;
    if (index == MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT - 1U) {
        return maximum;
    }
    const uint32_t exponentQ16 = static_cast<uint32_t>(
        (static_cast<uint64_t>(detail::freeMaximumLog2Q16(parameter)) *
             (index - 1U) +
         (MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT - 2U) / 2U) /
        (MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT - 2U)
    );
    const uint8_t whole = static_cast<uint8_t>(exponentQ16 >> 16U);
    const uint32_t fractionValueQ16 = detail::exp2FractionQ16(
        static_cast<uint16_t>(exponentQ16)
    );
    const uint32_t rounded = static_cast<uint32_t>(
        ((static_cast<uint64_t>(fractionValueQ16) << whole) + 32768U) >> 16U
    );
    return static_cast<uint16_t>(std::clamp<uint32_t>(rounded, 1U, maximum));
}

[[nodiscard]] constexpr uint16_t modulatorEnvelopeFreeDurationIndex(
    uint16_t duration,
    ModulatorEnvelopeTimeParameter parameter
) {
    const uint16_t maximum = maximumModulatorEnvelopeFreeMilliseconds(
        parameter
    );
    if (duration == 0U) return 0U;
    duration = std::min(duration, maximum);
    uint16_t low = 1U;
    uint16_t high = MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT - 1U;
    while (low < high) {
        const uint16_t middle = static_cast<uint16_t>(
            low + (high - low) / 2U
        );
        if (modulatorEnvelopeFreeDurationAt(middle, parameter) < duration) {
            low = static_cast<uint16_t>(middle + 1U);
        } else {
            high = middle;
        }
    }
    if (low <= 1U) return low;
    const uint16_t lower = static_cast<uint16_t>(low - 1U);
    const uint16_t lowerValue = modulatorEnvelopeFreeDurationAt(
        lower,
        parameter
    );
    const uint16_t upperValue = modulatorEnvelopeFreeDurationAt(
        low,
        parameter
    );
    const uint16_t lowerDistance = static_cast<uint16_t>(
        duration - lowerValue
    );
    const uint16_t upperDistance = static_cast<uint16_t>(
        upperValue - duration
    );
    return upperDistance < lowerDistance ? low : lower;
}

[[nodiscard]] constexpr bool isCanonicalModulatorEnvelopeSyncBaseTicks(
    uint16_t baseTicks
) {
    for (const uint16_t candidate : MODULATOR_ENVELOPE_SYNC_BASE_TICKS) {
        if (candidate == baseTicks) return true;
    }
    return false;
}

/**
 * Validation for newly authored values. FREE values are milliseconds; SYNC
 * values are unmodified base ticks selected from the canonical grid.
 */
[[nodiscard]] constexpr bool isValidModulatorEnvelopeAuthoringDuration(
    ModulatorEnvelopeTimeParameter parameter,
    ModulatorTimingMode timing,
    uint16_t duration
) {
    if (!isValidModulatorEnvelopeTimeParameter(parameter) ||
        !isValidModulatorEnvelopeTimingMode(timing)) {
        return false;
    }
    if (timing == ModulatorTimingMode::FREE) {
        return duration <= maximumModulatorEnvelopeFreeMilliseconds(parameter);
    }
    return duration <= maximumModulatorEnvelopeSyncBaseTicks(parameter) &&
           isCanonicalModulatorEnvelopeSyncBaseTicks(duration);
}

/**
 * Validation for decoded storage. Current files contain authorable values
 * only; invalid enum tags or off-grid durations are rejected transactionally.
 */
[[nodiscard]] constexpr bool isValidModulatorEnvelopeStoredDuration(
    ModulatorEnvelopeTimeParameter parameter,
    ModulatorTimingMode timing,
    uint16_t duration
) {
    return isValidModulatorEnvelopeAuthoringDuration(
        parameter,
        timing,
        duration
    );
}

/**
 * Applies a synchronized feel using uint32_t arithmetic. Rational results are
 * rounded to the nearest tick; an exact half rounds upward. Invalid feels use
 * the safe STRAIGHT fallback, while isValidModulatorEnvelopeFeel() lets callers
 * reject them at an authoring boundary.
 */
[[nodiscard]] constexpr uint32_t resolveModulatorEnvelopeSyncTicks(
    uint16_t baseTicks,
    ModulatorEnvelopeFeel feel
) {
    const uint32_t ticks = baseTicks;
    if (feel == ModulatorEnvelopeFeel::TRIPLET) {
        return (2U * ticks + 1U) / 3U;
    }
    if (feel == ModulatorEnvelopeFeel::DOTTED) {
        return (3U * ticks + 1U) / 2U;
    }
    return ticks;
}

}  // namespace core::state::modulation

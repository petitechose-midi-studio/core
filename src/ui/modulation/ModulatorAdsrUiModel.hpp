#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "state/modulation/ModulatorEnvelopeTiming.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::ui::modulation::adsr {

inline constexpr uint16_t FREE_DURATION_STEP_COUNT =
    core::state::modulation::MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT;
inline constexpr uint8_t SUSTAIN_STEP_COUNT = 101U;

[[nodiscard]] inline uint16_t durationCount(
    core::state::modulation::ModulatorTimingMode timing,
    core::state::modulation::ModulatorEnvelopeTimeParameter parameter
) {
    using namespace core::state::modulation;
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
    core::state::modulation::ModulatorTimingMode timing,
    core::state::modulation::ModulatorEnvelopeTimeParameter parameter
) {
    using namespace core::state::modulation;
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
    core::state::modulation::ModulatorTimingMode timing,
    core::state::modulation::ModulatorEnvelopeTimeParameter parameter
) {
    using namespace core::state::modulation;
    const uint16_t count = durationCount(timing, parameter);
    index = std::min<uint16_t>(index, static_cast<uint16_t>(count - 1U));
    if (timing == ModulatorTimingMode::SYNC) {
        return MODULATOR_ENVELOPE_SYNC_BASE_TICKS[index];
    }
    return modulatorEnvelopeFreeDurationAt(index, parameter);
}

void formatDuration(
    char* out,
    std::size_t size,
    uint16_t duration,
    core::state::modulation::ModulatorTimingMode timing
);

[[nodiscard]] inline const char* feelLabel(
    core::state::modulation::ModulatorEnvelopeFeel feel
) {
    using core::state::modulation::ModulatorEnvelopeFeel;
    if (feel == ModulatorEnvelopeFeel::TRIPLET) return "Triplet";
    if (feel == ModulatorEnvelopeFeel::DOTTED) return "Dotted";
    return "Straight";
}

inline uint8_t sustainQ15ToPercent(uint16_t sustainQ15) {
    return static_cast<uint8_t>(std::min<uint32_t>(
        100U,
        (static_cast<uint32_t>(sustainQ15) * 100U + 16384U) /
            core::state::modulation::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
    ));
}

inline uint16_t sustainPercentToQ15(uint8_t percent) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(std::min<uint8_t>(percent, 100U)) *
         core::state::modulation::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15 + 50U) /
        100U
    );
}

struct PreviewBoundaries {
    uint16_t delayEndQ16 = 0U;
    uint16_t attackEndQ16 = 0U;
    uint16_t holdEndQ16 = 0U;
    uint16_t decayEndQ16 = 0U;
    /** Authored preview note-off boundary; Sustain itself has no duration. */
    uint16_t sustainEndQ16 = 0U;
    uint32_t totalDuration = 0U;
};

inline PreviewBoundaries previewBoundaries(
    const core::state::modulation::ModulatorAdsrParameters& parameters
) {
    using namespace core::state::modulation;
    const auto timing = modulatorAdsrTiming(parameters.traits);
    const auto effective = [&](ModulatorEnvelopeTimeParameter parameter) {
        const uint16_t base = modulatorEnvelopeDuration(parameters, parameter);
        return timing == ModulatorTimingMode::FREE
            ? static_cast<uint32_t>(base)
            : resolveModulatorEnvelopeSyncTicks(
                  base,
                  modulatorAdsrFeel(parameters.traits, parameter)
              );
    };
    const auto positionQ16 = [](uint64_t elapsed, uint64_t total) {
        return static_cast<uint16_t>((elapsed * 65535U) / total);
    };
    const uint32_t delay = effective(ModulatorEnvelopeTimeParameter::DELAY);
    const uint32_t attack = effective(ModulatorEnvelopeTimeParameter::ATTACK);
    const uint32_t hold = effective(ModulatorEnvelopeTimeParameter::HOLD);
    const uint32_t decay = effective(ModulatorEnvelopeTimeParameter::DECAY);
    const uint32_t release = effective(
        ModulatorEnvelopeTimeParameter::RELEASE
    );
    const uint64_t beforeNoteOff = delay + attack + hold + decay;
    const uint64_t total = beforeNoteOff + release;
    if (total == 0U) {
        return {
            .delayEndQ16 = 0U,
            .attackEndQ16 = 0U,
            .holdEndQ16 = 0U,
            .decayEndQ16 = 0U,
            .sustainEndQ16 = 65535U,
            .totalDuration = 1U,
        };
    }
    return {
        .delayEndQ16 = positionQ16(delay, total),
        .attackEndQ16 = positionQ16(delay + attack, total),
        .holdEndQ16 = positionQ16(delay + attack + hold, total),
        .decayEndQ16 = positionQ16(delay + attack + hold + decay, total),
        .sustainEndQ16 = positionQ16(beforeNoteOff, total),
        .totalDuration = static_cast<uint32_t>(total),
    };
}

inline float segmentProgress(
    uint16_t position,
    uint16_t begin,
    uint16_t end
) {
    if (end <= begin) return 1.0f;
    return std::clamp(
        static_cast<float>(position - begin) /
            static_cast<float>(end - begin),
        0.0f,
        1.0f
    );
}

inline float previewValue(
    const core::state::modulation::ModulatorAdsrParameters& parameters,
    const PreviewBoundaries& boundaries,
    uint16_t positionQ16
) {
    using namespace core::state::modulation;
    const float sustain = std::clamp(
        static_cast<float>(parameters.sustainQ15) /
            static_cast<float>(PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15),
        0.0f,
        1.0f
    );
    const auto response = modulatorAdsrCurve(parameters.traits);
    if (positionQ16 < boundaries.delayEndQ16) return 0.0f;
    if (positionQ16 < boundaries.attackEndQ16) {
        return evaluateProjectAdsrProgress(
            response,
            segmentProgress(
                positionQ16,
                boundaries.delayEndQ16,
                boundaries.attackEndQ16
            )
        );
    }
    if (positionQ16 < boundaries.holdEndQ16) return 1.0f;
    if (positionQ16 < boundaries.decayEndQ16) {
        const float shaped = evaluateProjectAdsrProgress(
            response,
            segmentProgress(
                positionQ16,
                boundaries.holdEndQ16,
                boundaries.decayEndQ16
            )
        );
        return 1.0f + (sustain - 1.0f) * shaped;
    }
    if (positionQ16 < boundaries.sustainEndQ16) return sustain;
    const float shaped = evaluateProjectAdsrProgress(
        response,
        segmentProgress(positionQ16, boundaries.sustainEndQ16, 65535U)
    );
    return sustain * (1.0f - shaped);
}

inline bool runtimeMarkerPosition(
    const PreviewBoundaries& boundaries,
    core::state::modulation::ProjectModulationAdsrStage stage,
    uint16_t stageProgressQ16,
    uint16_t& outPositionQ16
) {
    using core::state::modulation::ProjectModulationAdsrStage;
    uint16_t begin = 0U;
    uint16_t end = 0U;
    switch (stage) {
        case ProjectModulationAdsrStage::DELAY:
            end = boundaries.delayEndQ16;
            break;
        case ProjectModulationAdsrStage::ATTACK:
            begin = boundaries.delayEndQ16;
            end = boundaries.attackEndQ16;
            break;
        case ProjectModulationAdsrStage::HOLD:
            begin = boundaries.attackEndQ16;
            end = boundaries.holdEndQ16;
            break;
        case ProjectModulationAdsrStage::DECAY:
            begin = boundaries.holdEndQ16;
            end = boundaries.decayEndQ16;
            break;
        case ProjectModulationAdsrStage::SUSTAIN:
            outPositionQ16 = static_cast<uint16_t>(
                boundaries.decayEndQ16 +
                (boundaries.sustainEndQ16 - boundaries.decayEndQ16) / 2U
            );
            return true;
        case ProjectModulationAdsrStage::RELEASE:
            begin = boundaries.sustainEndQ16;
            end = 65535U;
            break;
        case ProjectModulationAdsrStage::IDLE:
        default:
            return false;
    }
    outPositionQ16 = static_cast<uint16_t>(
        begin +
        (static_cast<uint32_t>(end - begin) * stageProgressQ16) / 65535U
    );
    return true;
}

inline const core::state::modulation::ProjectModulationRuntimeAdsrState*
runtimeState(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId
) {
    using namespace core::state::modulation;
    for (uint16_t index = 0U; index < control.runtime.sourceCount; ++index) {
        const auto& state = control.runtime.sources[index];
        if (state.id == sourceId &&
            state.payload.adsr.kind == ModulatorKind::ADSR) {
            return &state.payload.adsr;
        }
    }
    return nullptr;
}

}  // namespace core::ui::modulation::adsr

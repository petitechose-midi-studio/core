#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::ui::modulation::adsr {

enum class AuditionItem : uint8_t {
    ATTACK = 0,
    DECAY,
    SUSTAIN,
    RELEASE,
    DEPTH,
};

inline constexpr uint8_t AUDITION_ITEM_COUNT = 5U;
inline constexpr uint8_t DURATION_COUNT = 16U;
inline constexpr uint8_t SUSTAIN_STEP_COUNT = 101U;

inline constexpr std::array<uint16_t, DURATION_COUNT> FREE_DURATIONS{{
    0U, 4U, 8U, 16U, 32U, 64U, 125U, 250U,
    500U, 1000U, 2000U, 4000U, 8000U, 16000U, 32000U, 65535U,
}};

inline constexpr std::array<uint16_t, DURATION_COUNT> SYNC_DURATIONS{{
    0U, 3U, 6U, 12U, 24U, 48U, 96U, 192U,
    384U, 768U, 1536U, 3072U, 6144U, 12288U, 24576U, 65535U,
}};

inline uint8_t durationIndex(
    uint16_t duration,
    core::state::modulation::ModulatorTimingMode timing
) {
    const auto& values = timing ==
            core::state::modulation::ModulatorTimingMode::FREE
        ? FREE_DURATIONS
        : SYNC_DURATIONS;
    uint8_t nearest = 0U;
    uint32_t nearestDistance = UINT32_MAX;
    for (uint8_t index = 0U; index < values.size(); ++index) {
        const uint32_t value = values[index];
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

inline uint16_t durationAt(
    uint8_t index,
    core::state::modulation::ModulatorTimingMode timing
) {
    const auto& values = timing ==
            core::state::modulation::ModulatorTimingMode::FREE
        ? FREE_DURATIONS
        : SYNC_DURATIONS;
    return values[std::min<uint8_t>(index, DURATION_COUNT - 1U)];
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
    uint16_t attackEndQ16 = 0U;
    uint16_t decayEndQ16 = 0U;
    uint16_t sustainEndQ16 = 0U;
};

inline PreviewBoundaries previewBoundaries(
    const core::state::modulation::ModulatorAdsrParameters& parameters
) {
    uint32_t attack = parameters.attack;
    uint32_t decay = parameters.decay;
    uint32_t release = parameters.release;
    const uint32_t moving = attack + decay + release;
    uint32_t sustain = std::max<uint32_t>(1U, moving / 4U);
    if (moving == 0U) {
        attack = 1U;
        decay = 1U;
        release = 1U;
        sustain = 1U;
    }
    const uint64_t total = attack + decay + sustain + release;
    return {
        .attackEndQ16 = static_cast<uint16_t>(
            (static_cast<uint64_t>(attack) * 65535U) / total
        ),
        .decayEndQ16 = static_cast<uint16_t>(
            (static_cast<uint64_t>(attack + decay) * 65535U) / total
        ),
        .sustainEndQ16 = static_cast<uint16_t>(
            (static_cast<uint64_t>(attack + decay + sustain) * 65535U) /
                total
        ),
    };
}

inline float segmentProgress(uint16_t position, uint16_t begin, uint16_t end) {
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
    if (positionQ16 < boundaries.attackEndQ16) {
        return evaluateProjectAdsrProgress(
            parameters.curve,
            segmentProgress(positionQ16, 0U, boundaries.attackEndQ16)
        );
    }
    if (positionQ16 < boundaries.decayEndQ16) {
        const float shaped = evaluateProjectAdsrProgress(
            parameters.curve,
            segmentProgress(
                positionQ16,
                boundaries.attackEndQ16,
                boundaries.decayEndQ16
            )
        );
        return 1.0f + (sustain - 1.0f) * shaped;
    }
    if (positionQ16 < boundaries.sustainEndQ16) return sustain;
    const float shaped = evaluateProjectAdsrProgress(
        parameters.curve,
        segmentProgress(positionQ16, boundaries.sustainEndQ16, 65535U)
    );
    return sustain * (1.0f - shaped);
}

inline const core::state::modulation::ProjectModulationRuntimeAdsrState*
runtimeState(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId
) {
    using namespace core::state::modulation;
    for (uint16_t index = 0U; index < control.runtime.sourceCount; ++index) {
        const auto& state = control.runtime.sources[index];
        if (state.id == sourceId && state.payload.adsr.kind == ModulatorKind::ADSR) {
            return &state.payload.adsr;
        }
    }
    return nullptr;
}

}  // namespace core::ui::modulation::adsr

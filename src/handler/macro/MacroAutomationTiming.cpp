#include "handler/macro/MacroAutomationTiming.hpp"

#include <algorithm>
#include <limits>

#include <config/PlatformCompat.hpp>

namespace core::handler::macro {
namespace {

using namespace core::state::modulation;

inline constexpr uint32_t FALLBACK_MAX_TEMPO_BPM = 300U;
inline constexpr uint16_t AUTHORS_PER_MILLISECOND_BUDGET = 8U;

constexpr uint32_t ceilDivide(uint64_t numerator, uint64_t denominator) {
    return denominator == 0U
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>((numerator + denominator - 1U) / denominator);
}

constexpr uint32_t cadenceForCycle(uint32_t periodMs) {
    return std::clamp<uint32_t>(
        ceilDivide(periodMs, MACRO_AUTOMATION_SAMPLES_PER_FAST_CYCLE),
        MACRO_AUTOMATION_MIN_UPDATE_PERIOD_MS,
        MACRO_AUTOMATION_UPDATE_PERIOD_MS
    );
}

constexpr uint16_t shortestNonZero(
    uint16_t first,
    uint16_t second,
    uint16_t third
) {
    uint16_t shortest = std::numeric_limits<uint16_t>::max();
    if (first != 0U) shortest = std::min(shortest, first);
    if (second != 0U) shortest = std::min(shortest, second);
    if (third != 0U) shortest = std::min(shortest, third);
    return shortest == std::numeric_limits<uint16_t>::max() ? 0U : shortest;
}

FLASHMEM uint32_t sourceCadence(
    const ProjectModulationRuntimeSource& source,
    const ProjectControlTimeTelemetry& telemetry
) {
    if ((source.flags & PROJECT_MODULATOR_FLAG_ENABLED) == 0U) {
        return MACRO_AUTOMATION_UPDATE_PERIOD_MS;
    }
    if (source.kind == ModulatorKind::LFO) {
        const uint32_t cycle = source.traits.lfo.timing ==
                ModulatorTimingMode::FREE
            ? source.parameters.lfo.freePeriodMs
            : musicalPeriodMilliseconds(
                  source.parameters.lfo.periodTicks,
                  telemetry
              );
        return cadenceForCycle(cycle);
    }
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        return cadenceForCycle(musicalPeriodMilliseconds(
            source.parameters.curve.durationTicks,
            telemetry
        ));
    }
    if (source.kind == ModulatorKind::ADSR) {
        const auto& adsr = source.parameters.adsr;
        const uint16_t shortest = shortestNonZero(
            adsr.attack,
            adsr.decay,
            adsr.release
        );
        // Zero-duration stages are instantaneous transitions, not motion that
        // benefits from polling every millisecond.
        if (shortest == 0U) return MACRO_AUTOMATION_UPDATE_PERIOD_MS;
        const uint32_t stage = adsr.timing == ModulatorTimingMode::FREE
            ? shortest
            : musicalPeriodMilliseconds(shortest, telemetry);
        return cadenceForCycle(stage);
    }
    return MACRO_AUTOMATION_UPDATE_PERIOD_MS;
}

}  // namespace

FLASHMEM uint32_t musicalPeriodMilliseconds(
    uint32_t periodTicks,
    const ProjectControlTimeTelemetry& telemetry
) {
    if (periodTicks == 0U) return MACRO_AUTOMATION_MIN_UPDATE_PERIOD_MS;
    const auto& previous = telemetry.previous;
    const auto& current = telemetry.current;
    if (!current.playing) return MACRO_AUTOMATION_UPDATE_PERIOD_MS;

    if (telemetry.revision >= 2U &&
        previous.transportGeneration == current.transportGeneration &&
        current.monotonicMs > previous.monotonicMs) {
        const uint64_t previousQ16 =
            (static_cast<uint64_t>(previous.musicalTick) << 16U) |
            previous.musicalTickFractionQ16;
        const uint64_t currentQ16 =
            (static_cast<uint64_t>(current.musicalTick) << 16U) |
            current.musicalTickFractionQ16;
        if (currentQ16 > previousQ16) {
            const uint64_t elapsedMs =
                current.monotonicMs - previous.monotonicMs;
            return std::max<uint32_t>(
                1U,
                ceilDivide(
                    static_cast<uint64_t>(periodTicks) * elapsedMs * 65536ULL,
                    currentQ16 - previousQ16
                )
            );
        }
    }

    // The first running frame has no tempo observation yet. Use the supported
    // upper musical tempo so a fast LFO is never initially undersampled.
    return std::max<uint32_t>(
        1U,
        ceilDivide(
            static_cast<uint64_t>(periodTicks) * 60000ULL,
            static_cast<uint64_t>(PROJECT_CONTROL_TICKS_PER_BEAT) *
                FALLBACK_MAX_TEMPO_BPM
        )
    );
}

FLASHMEM uint32_t projectControlUpdatePeriodMilliseconds(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& curves,
    const ProjectControlTimeTelemetry& telemetry,
    uint16_t activeAuthorCount
) {
    uint32_t desired = MACRO_AUTOMATION_UPDATE_PERIOD_MS;
    for (uint16_t index = 0U; index < plan.sourceCount; ++index) {
        desired = std::min(desired, sourceCadence(plan.sources[index], telemetry));
    }
    for (uint16_t index = 0U; index < plan.destinationCount; ++index) {
        const auto& destination = plan.destinations[index];
        if ((destination.flags &
             PROJECT_CONTROL_RUNTIME_DESTINATION_FLAG_AUTOMATION_ENABLED) == 0U ||
            destination.automationCurveRecordIndex >= curves.recordCount) {
            continue;
        }
        desired = std::min(
            desired,
            cadenceForCycle(musicalPeriodMilliseconds(
                curves.records[destination.automationCurveRecordIndex]
                    .durationTicks,
                telemetry
            ))
        );
    }

    const uint16_t workload = std::max<uint16_t>(
        activeAuthorCount,
        plan.destinationCount
    );
    const uint32_t workloadFloor = std::clamp<uint32_t>(
        ceilDivide(workload, AUTHORS_PER_MILLISECOND_BUDGET),
        MACRO_AUTOMATION_MIN_UPDATE_PERIOD_MS,
        MACRO_AUTOMATION_UPDATE_PERIOD_MS
    );
    return std::max(desired, workloadFloor);
}

}  // namespace core::handler::macro

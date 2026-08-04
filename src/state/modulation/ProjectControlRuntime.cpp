#include "state/modulation/ProjectControlRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ModulatorEnvelopeTiming.hpp"
#include "state/modulation/ProjectControlRuntimeInternal.hpp"

namespace core::state::modulation {
namespace project_control_runtime_detail {
bool validTime(const ProjectControlTimeSnapshot& time) {
    return time.reserved == 0U;
}

bool validPlanBounds(const ProjectModulationRuntimePlan& plan) {
    return plan.sourceCount <= PROJECT_MODULATOR_CAPACITY &&
           plan.bindingCount <= PROJECT_MODULATION_BINDING_CAPACITY &&
           plan.destinationCount <=
               PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY &&
           plan.triggerRouteCount <= plan.sourceCount &&
           plan.triggerBucketOffset.back() == plan.triggerRouteCount;
}

FLASHMEM uint16_t adsrRouteSignature(
    const ProjectModulationRuntimeSource& source
) {
    // A compact route-continuity fingerprint lets the fixed 32-byte DAHDSR
    // state retain its phase across harmless parameter edits while clearing
    // accepted-note facts whenever any authored gate-routing fact changes.
    uint32_t hash = 2166136261U;
    const auto mix = [&hash](uint8_t value) {
        hash = (hash ^ value) * 16777619U;
    };
    mix(static_cast<uint8_t>(source.trigger.kind));
    mix(source.trigger.track);
    mix(source.trigger.noteMin);
    mix(source.trigger.noteMax);
    mix(source.triggerVelocityMin);
    mix(source.triggerVelocityMax);
    mix(static_cast<uint8_t>(source.traits.adsr.timing));
    mix(static_cast<uint8_t>(
        source.flags & PROJECT_MODULATOR_FLAG_ENABLED
    ));
    mix(static_cast<uint8_t>(
        source.triggerFlags & PROJECT_MODULATION_TRIGGER_FLAG_ENABLED
    ));
    return static_cast<uint16_t>(hash ^ (hash >> 16U));
}

FLASHMEM bool idNeededAfter(const ProjectModulationRuntimePlan& plan,
                            uint16_t first,
                            ModulatorId id) {
    if (!valid(id)) return false;
    for (uint16_t index = first; index < plan.sourceCount; ++index) {
        if (plan.sources[index].id == id) return true;
    }
    return false;
}

FLASHMEM bool bindingIdNeededAfter(const ProjectModulationRuntimePlan& plan,
                                   uint16_t first,
                                   ModulationBindingId id) {
    if (!valid(id)) return false;
    for (uint16_t index = first; index < plan.bindingCount; ++index) {
        if (plan.bindings[index].id == id) return true;
    }
    return false;
}

FLASHMEM ProjectModulationRuntimeSourceState makeSourceState(
    ModulatorId id,
    const ProjectModulationRuntimeSource& source,
    const ProjectControlTimeSnapshot& time
) {
    ProjectModulationRuntimeSourceState state{};
    state.id = id;
    const ModulatorKind kind = source.kind;
    if (kind == ModulatorKind::RECORDED_SHAPE) {
        state.payload.recordedCurve = {};
        state.payload.recordedCurve.kind = kind;
        state.payload.recordedCurve.segmentHint = 1U;
    } else if (kind == ModulatorKind::ADSR) {
        state.payload.adsr = {};
        state.payload.adsr.kind = kind;
        state.payload.adsr.routeSignature = adsrRouteSignature(source);
    } else {
        state.payload.lfo = {};
        state.payload.lfo.kind = kind;
        state.payload.lfo.explicitMusicalAnchorTick = time.musicalTick;
        state.payload.lfo.explicitMonotonicAnchorMs = time.monotonicMs;
        state.payload.lfo.explicitMusicalAnchorFractionQ16 =
            time.musicalTickFractionQ16;
    }
    return state;
}

FLASHMEM ModulatorKind sourceStateKind(
    const ProjectModulationRuntimeSourceState& state
) {
    // Both standard-layout union members share ModulatorKind as their common
    // initial sequence, so the discriminator is readable through either arm.
    return state.payload.lfo.kind;
}

FLASHMEM void preparePublishedSourceState(
    ProjectModulationRuntimeSourceState& state,
    const ProjectModulationRuntimeSource& source,
    const ProjectControlTimeSnapshot& time
) {
    if (sourceStateKind(state) != source.kind) {
        state = makeSourceState(source.id, source, time);
        return;
    }
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        // The source ID can survive a curve edit. Never retain endpoints from
        // the previously published immutable plan/arena pair.
        state.payload.recordedCurve.segmentValid = false;
        state.payload.recordedCurve.segmentHint = 1U;
    } else if (source.kind == ModulatorKind::ADSR) {
        if (state.payload.adsr.routeSignature != adsrRouteSignature(source)) {
            state = makeSourceState(source.id, source, time);
        }
    }
}

FLASHMEM void synchronizeSources(ProjectControlRuntimeState& state,
                                 const ProjectModulationRuntimePlan& plan,
                                 const ProjectControlTimeSnapshot& time) {
    const uint16_t previousCount = state.sourceCount;
    const uint16_t searchCount = std::max(previousCount, plan.sourceCount);
    for (uint16_t index = previousCount;
         index < searchCount;
         ++index) {
        state.sources[index] = {};
    }

    for (uint16_t targetIndex = 0;
         targetIndex < plan.sourceCount;
         ++targetIndex) {
        const ModulatorId targetId = plan.sources[targetIndex].id;
        if (state.sources[targetIndex].id == targetId) {
            preparePublishedSourceState(
                state.sources[targetIndex],
                plan.sources[targetIndex],
                time
            );
            continue;
        }

        uint16_t found = searchCount;
        for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
             candidate < searchCount;
             ++candidate) {
            if (state.sources[candidate].id == targetId) {
                found = candidate;
                break;
            }
        }
        if (found < searchCount) {
            std::swap(state.sources[targetIndex], state.sources[found]);
            preparePublishedSourceState(
                state.sources[targetIndex],
                plan.sources[targetIndex],
                time
            );
            continue;
        }

        const bool displacedNeeded = idNeededAfter(
            plan,
            static_cast<uint16_t>(targetIndex + 1U),
            state.sources[targetIndex].id
        );
        if (displacedNeeded) {
            for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
                 candidate < searchCount;
                 ++candidate) {
                if (!idNeededAfter(
                        plan,
                        static_cast<uint16_t>(targetIndex + 1U),
                        state.sources[candidate].id
                    )) {
                    state.sources[candidate] = state.sources[targetIndex];
                    break;
                }
            }
        }
        state.sources[targetIndex] = makeSourceState(
            targetId,
            plan.sources[targetIndex],
            time
        );
    }
    for (uint16_t index = plan.sourceCount; index < previousCount; ++index) {
        state.sources[index] = {};
    }
    state.sourceCount = plan.sourceCount;
}

FLASHMEM void synchronizeBindings(ProjectControlRuntimeState& state,
                                  const ProjectModulationRuntimePlan& plan) {
    const uint16_t previousCount = state.bindingCount;
    const uint16_t searchCount = std::max(previousCount, plan.bindingCount);
    for (uint16_t index = previousCount;
         index < searchCount;
         ++index) {
        state.bindingIds[index] = {};
        state.bindingContributionQ15[index] = 0;
    }

    for (uint16_t targetIndex = 0;
         targetIndex < plan.bindingCount;
         ++targetIndex) {
        const ModulationBindingId targetId = plan.bindings[targetIndex].id;
        if (state.bindingIds[targetIndex] == targetId) continue;

        uint16_t found = searchCount;
        for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
             candidate < searchCount;
             ++candidate) {
            if (state.bindingIds[candidate] == targetId) {
                found = candidate;
                break;
            }
        }
        if (found < searchCount) {
            std::swap(state.bindingIds[targetIndex], state.bindingIds[found]);
            std::swap(
                state.bindingContributionQ15[targetIndex],
                state.bindingContributionQ15[found]
            );
            continue;
        }

        const bool displacedNeeded = bindingIdNeededAfter(
            plan,
            static_cast<uint16_t>(targetIndex + 1U),
            state.bindingIds[targetIndex]
        );
        if (displacedNeeded) {
            for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
                 candidate < searchCount;
                 ++candidate) {
                if (!bindingIdNeededAfter(
                        plan,
                        static_cast<uint16_t>(targetIndex + 1U),
                        state.bindingIds[candidate]
                    )) {
                    state.bindingIds[candidate] = state.bindingIds[targetIndex];
                    state.bindingContributionQ15[candidate] =
                        state.bindingContributionQ15[targetIndex];
                    break;
                }
            }
        }
        state.bindingIds[targetIndex] = targetId;
        state.bindingContributionQ15[targetIndex] = 0;
    }
    for (uint16_t index = plan.bindingCount; index < previousCount; ++index) {
        state.bindingIds[index] = {};
        state.bindingContributionQ15[index] = 0;
    }
    state.bindingCount = plan.bindingCount;
}

}  // namespace project_control_runtime_detail

using namespace project_control_runtime_detail;

FLASHMEM void resetProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time
) {
    state = {};
    state.activationMusicalTick = time.musicalTick;
    state.activationMonotonicMs = time.monotonicMs;
    state.lastEvaluationMs = time.monotonicMs;
    state.lastEvaluationMusicalTick = time.musicalTick;
    state.activationMusicalTickFractionQ16 =
        time.musicalTickFractionQ16;
    state.lastEvaluationMusicalTickFractionQ16 =
        time.musicalTickFractionQ16;
    state.initialized = validTime(time);
}

FLASHMEM bool setProjectRecordedShapeSourceAudition(
    ProjectControlRuntimeState& state,
    ModulatorId sourceId,
    int16_t sourceValueQ15
) {
    if (!valid(sourceId) ||
        sourceValueQ15 == std::numeric_limits<int16_t>::min()) {
        return false;
    }
    state.recordedShapeAudition = {
        .sourceId = sourceId,
        .sourceValueQ15 = sourceValueQ15,
        .mode = ProjectRecordedShapeRuntimeAuditionMode::SOURCE_OVERRIDE,
    };
    return true;
}

FLASHMEM bool setProjectRecordedShapeDestinationAudition(
    ProjectControlRuntimeState& state,
    const ModulationDestination& destination,
    int16_t amountQ15,
    int16_t sourceValueQ15,
    uint16_t destinationScaleQ15
) {
    if (!modulationDestinationValid(destination) ||
        amountQ15 == std::numeric_limits<int16_t>::min() ||
        sourceValueQ15 == std::numeric_limits<int16_t>::min()) {
        return false;
    }
    state.recordedShapeAudition = {
        .destination = destination,
        .sourceValueQ15 = sourceValueQ15,
        .amountQ15 = amountQ15,
        .destinationScaleQ15 = destinationScaleQ15,
        .mode = ProjectRecordedShapeRuntimeAuditionMode::DESTINATION_ADD,
    };
    return true;
}

FLASHMEM void clearProjectRecordedShapeRuntimeAudition(
    ProjectControlRuntimeState& state
) {
    state.recordedShapeAudition = {};
}

FLASHMEM ProjectControlRuntimeStatus synchronizeProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time
) {
    if (!validPlanBounds(plan)) {
        return ProjectControlRuntimeStatus::INVALID_PLAN;
    }
    if (!validTime(time)) {
        return ProjectControlRuntimeStatus::INVALID_TIME;
    }
    if (!state.initialized) resetProjectControlRuntimeState(state, time);
    synchronizeSources(state, plan, time);
    synchronizeBindings(state, plan);
    return ProjectControlRuntimeStatus::OK;
}

}  // namespace core::state::modulation

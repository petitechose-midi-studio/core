#include "state/modulation/ProjectModulationDomainOps.hpp"

#include "state/modulation/ProjectModulationDomainOpsInternal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

namespace project_modulation_detail {

FLASHMEM ProjectModulationResult result(
    ProjectModulationStatus status,
    ModulatorId sourceId,
    ModulationBindingId bindingId,
    ProjectCurveId curveId
) {
    return {status, sourceId, bindingId, curveId};
}

FLASHMEM bool canAllocateId(uint32_t next, uint16_t count) {
    if (count == 0) return true;
    if (next == 0) return false;
    return static_cast<uint64_t>(next) + count - 1U <=
        std::numeric_limits<uint32_t>::max();
}

FLASHMEM uint32_t takeId(uint32_t& next) {
    const uint32_t value = next;
    next = value == std::numeric_limits<uint32_t>::max() ? 0U : value + 1U;
    return value;
}

FLASHMEM void copyName(
    std::array<char, PROJECT_MODULATOR_NAME_CAPACITY>& destination,
    const char* requested,
    const char* fallback
) {
    destination.fill('\0');
    const char* source = requested != nullptr && requested[0] != '\0'
        ? requested
        : fallback;
    if (source == nullptr || source[0] == '\0') source = "Modulator";
    std::strncpy(destination.data(), source, destination.size() - 1U);
    destination.back() = '\0';
}

FLASHMEM bool validLfoParameters(const ModulatorLfoParameters& parameters) {
    return parameters.periodTicks > 0 &&
           parameters.freePeriodMs >= PROJECT_MODULATOR_FREE_PERIOD_MIN_MS &&
           parameters.freePeriodMs <= PROJECT_MODULATOR_FREE_PERIOD_MAX_MS &&
           parameters.phaseQ15 != std::numeric_limits<int16_t>::min() &&
           static_cast<uint8_t>(parameters.shape) <=
               static_cast<uint8_t>(ModulatorLfoShape::SQUARE) &&
           static_cast<uint8_t>(parameters.retrigger) <=
               static_cast<uint8_t>(
                   ModulatorRetriggerPolicy::EXPLICIT_TRIGGER
               ) &&
           static_cast<uint8_t>(parameters.timing) <=
               static_cast<uint8_t>(ModulatorTimingMode::FREE) &&
           parameters.reserved[0] == 0U &&
           parameters.reserved[1] == 0U &&
           parameters.reserved[2] == 0U;
}

FLASHMEM bool validAdsrParameters(const ModulatorAdsrParameters& parameters) {
    return parameters.sustainQ15 <=
               PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15 &&
           static_cast<uint8_t>(modulatorAdsrTiming(parameters.traits)) <=
               static_cast<uint8_t>(ModulatorTimingMode::FREE) &&
           static_cast<uint8_t>(modulatorAdsrRetrigger(parameters.traits)) <=
               static_cast<uint8_t>(ModulatorAdsrRetriggerMode::LEGATO) &&
           static_cast<uint8_t>(modulatorAdsrCurve(parameters.traits)) <=
               static_cast<uint8_t>(ModulatorAdsrCurve::EXPONENTIAL) &&
           static_cast<uint8_t>(modulatorAdsrFeel(
               parameters.traits,
               ModulatorEnvelopeTimeParameter::DELAY
           )) <= static_cast<uint8_t>(ModulatorEnvelopeFeel::DOTTED) &&
           static_cast<uint8_t>(modulatorAdsrFeel(
               parameters.traits,
               ModulatorEnvelopeTimeParameter::ATTACK
           )) <= static_cast<uint8_t>(ModulatorEnvelopeFeel::DOTTED) &&
           static_cast<uint8_t>(modulatorAdsrFeel(
               parameters.traits,
               ModulatorEnvelopeTimeParameter::HOLD
           )) <= static_cast<uint8_t>(ModulatorEnvelopeFeel::DOTTED) &&
           static_cast<uint8_t>(modulatorAdsrFeel(
               parameters.traits,
               ModulatorEnvelopeTimeParameter::DECAY
           )) <= static_cast<uint8_t>(ModulatorEnvelopeFeel::DOTTED) &&
           static_cast<uint8_t>(modulatorAdsrFeel(
               parameters.traits,
               ModulatorEnvelopeTimeParameter::RELEASE
           )) <= static_cast<uint8_t>(ModulatorEnvelopeFeel::DOTTED) &&
           static_cast<uint8_t>(modulatorAdsrFeel(
               parameters.traits,
               ModulatorEnvelopeTimeParameter::SMOOTH
           )) <= static_cast<uint8_t>(ModulatorEnvelopeFeel::DOTTED);
}

FLASHMEM bool parameterTailZero(
    const ModulatorParameters& parameters,
    size_t first
) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&parameters);
    for (size_t index = first; index < sizeof(parameters); ++index) {
        if (bytes[index] != 0U) return false;
    }
    return true;
}

FLASHMEM bool validTriggerFilter(const ModulationTriggerFilter& trigger) {
    if (static_cast<uint8_t>(trigger.kind) >
        static_cast<uint8_t>(ModulationTriggerKind::TRACK_NOTE)) {
        return false;
    }
    if (trigger.track >= PROJECT_MODULATION_TRACK_COUNT) return false;
    return trigger.noteMin <= trigger.noteMax && trigger.noteMax <= 127U;
}

FLASHMEM bool validVelocityRange(uint8_t minimum, uint8_t maximum) {
    return minimum <= maximum && maximum <= 127U;
}

#define MS_DEFINE_ERASE_DENSE(Type, Capacity)                              \
    FLASHMEM void eraseDense(                                              \
        std::array<Type, Capacity>& entries,                               \
        uint16_t& count,                                                   \
        uint16_t index                                                     \
    ) {                                                                    \
        for (uint16_t cursor = index + 1U; cursor < count; ++cursor) {      \
            entries[cursor - 1U] = entries[cursor];                        \
        }                                                                  \
        --count;                                                           \
        entries[count] = {};                                               \
    }

MS_DEFINE_ERASE_DENSE(ModulatorSourceState, PROJECT_MODULATOR_CAPACITY)
MS_DEFINE_ERASE_DENSE(
    ModulationBindingState,
    PROJECT_MODULATION_BINDING_CAPACITY
)
MS_DEFINE_ERASE_DENSE(
    ModulationTriggerBindingState,
    PROJECT_MODULATION_TRIGGER_CAPACITY
)
MS_DEFINE_ERASE_DENSE(
    ModulationDestinationScaleState,
    PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY
)
MS_DEFINE_ERASE_DENSE(
    ProjectAutomationCurveEntry,
    PROJECT_AUTOMATION_ENTRY_CAPACITY
)

#undef MS_DEFINE_ERASE_DENSE

FLASHMEM int16_t sourceIndex(
    const ProjectModulationState& state,
    ModulatorId id
) {
    if (!valid(id)) return -1;
    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        if (state.sources[index].id == id) return static_cast<int16_t>(index);
    }
    return -1;
}

FLASHMEM int16_t outputBindingIndex(
    const ProjectModulationState& state,
    ModulationBindingId id
) {
    if (!valid(id)) return -1;
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        if (state.outputBindings[index].id == id) {
            return static_cast<int16_t>(index);
        }
    }
    return -1;
}

FLASHMEM int16_t destinationScaleIndex(
    const ProjectModulationState& state,
    const ModulationDestination& destination
) {
    const uint16_t address = modulationDestinationStableAddress(destination);
    for (uint16_t index = 0; index < state.destinationScaleCount; ++index) {
        const uint16_t candidate = modulationDestinationStableAddress(
            state.destinationScales[index].destination
        );
        if (candidate == address &&
            state.destinationScales[index].destination == destination) {
            return static_cast<int16_t>(index);
        }
        if (candidate > address) break;
    }
    return -1;
}

FLASHMEM bool destinationHasBinding(
    const ProjectModulationState& state,
    const ModulationDestination& destination
) {
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        if (state.outputBindings[index].destination == destination) return true;
    }
    return false;
}

FLASHMEM void pruneDestinationScaleIfUnbound(
    ProjectModulationState& state,
    const ModulationDestination& destination
) {
    if (destinationHasBinding(state, destination)) return;
    const int16_t index = destinationScaleIndex(state, destination);
    if (index >= 0) {
        eraseDense(
            state.destinationScales,
            state.destinationScaleCount,
            static_cast<uint16_t>(index)
        );
    }
}

FLASHMEM void pruneUnboundDestinationScales(ProjectModulationState& state) {
    for (uint16_t index = 0; index < state.destinationScaleCount;) {
        if (destinationHasBinding(
                state,
                state.destinationScales[index].destination
            )) {
            ++index;
        } else {
            eraseDense(
                state.destinationScales,
                state.destinationScaleCount,
                index
            );
        }
    }
}

FLASHMEM int16_t curveIndex(
    const ProjectCurveArena& arena,
    ProjectCurveId id
) {
    if (!valid(id)) return -1;
    for (uint16_t index = 0; index < arena.recordCount; ++index) {
        if (arena.records[index].id == id) return static_cast<int16_t>(index);
    }
    return -1;
}

FLASHMEM int16_t triggerIndexForSource(
    const ProjectModulationState& state,
    ModulatorId sourceId
) {
    for (uint16_t index = 0; index < state.triggerBindingCount; ++index) {
        if (state.triggerBindings[index].sourceId == sourceId) {
            return static_cast<int16_t>(index);
        }
    }
    return -1;
}

FLASHMEM int16_t triggerBindingIndex(
    const ProjectModulationState& state,
    ModulationBindingId id
) {
    if (!valid(id)) return -1;
    for (uint16_t index = 0; index < state.triggerBindingCount; ++index) {
        if (state.triggerBindings[index].id == id) {
            return static_cast<int16_t>(index);
        }
    }
    return -1;
}

FLASHMEM bool sameCurvePayload(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& record,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    if (record.pointCount != pointCount ||
        record.sourceDurationTicks != spec.sourceDurationTicks ||
        record.durationTicks != spec.durationTicks ||
        record.windowOffsetTicks != spec.windowOffsetTicks ||
        record.interpolation != spec.interpolation ||
        record.valueDomain != spec.valueDomain ||
        record.origin != spec.origin) {
        return false;
    }
    return std::memcmp(
        arena.points.data() + record.pointOffset,
        points,
        static_cast<size_t>(pointCount) * sizeof(ProjectPackedCurvePoint)
    ) == 0;
}

FLASHMEM bool curveInputAliasesArena(
    const ProjectCurveArena& arena,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    if (points == nullptr || pointCount == 0) return false;
    const uintptr_t inputBegin = reinterpret_cast<uintptr_t>(points);
    const uintptr_t inputEnd = inputBegin +
        static_cast<uintptr_t>(pointCount) * sizeof(ProjectPackedCurvePoint);
    if (inputEnd < inputBegin) return true;
    const uintptr_t arenaBegin = reinterpret_cast<uintptr_t>(arena.points.data());
    const uintptr_t arenaEnd = arenaBegin + sizeof(arena.points);
    return inputBegin < arenaEnd && arenaBegin < inputEnd;
}

FLASHMEM void populateCurveRecord(
    ProjectCurveRecord& record,
    ProjectCurveId id,
    uint16_t pointOffset,
    uint16_t pointCount,
    uint16_t referenceCount,
    const ProjectCurveSpec& spec
) {
    record = {};
    record.id = id;
    record.pointOffset = pointOffset;
    record.pointCount = pointCount;
    record.sourceDurationTicks = spec.sourceDurationTicks;
    record.durationTicks = spec.durationTicks;
    record.windowOffsetTicks = spec.windowOffsetTicks;
    record.referenceCount = referenceCount;
    record.interpolation = spec.interpolation;
    record.valueDomain = spec.valueDomain;
    record.origin = spec.origin;
}

/** Preconditions guarantee capacity and a usable curve ID. */
FLASHMEM ProjectCurveId appendCurve(
    ProjectCurveArena& arena,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    const ProjectCurveId id{takeId(arena.nextCurveId)};
    auto& record = arena.records[arena.recordCount];
    populateCurveRecord(
        record,
        id,
        arena.pointCount,
        pointCount,
        1U,
        spec
    );
    std::memcpy(
        arena.points.data() + arena.pointCount,
        points,
        static_cast<size_t>(pointCount) * sizeof(ProjectPackedCurvePoint)
    );
    arena.pointCount = static_cast<uint16_t>(arena.pointCount + pointCount);
    ++arena.recordCount;
    return id;
}

FLASHMEM void eraseCurveRecord(ProjectCurveArena& arena, uint16_t index) {
    const auto removed = arena.records[index];
    const uint16_t tailOffset = static_cast<uint16_t>(
        removed.pointOffset + removed.pointCount
    );
    const uint16_t tailCount = static_cast<uint16_t>(
        arena.pointCount - tailOffset
    );
    std::memmove(
        arena.points.data() + removed.pointOffset,
        arena.points.data() + tailOffset,
        static_cast<size_t>(tailCount) * sizeof(ProjectPackedCurvePoint)
    );
    arena.pointCount = static_cast<uint16_t>(
        arena.pointCount - removed.pointCount
    );

    for (uint16_t recordIndex = 0;
         recordIndex < arena.recordCount;
         ++recordIndex) {
        if (recordIndex != index &&
            arena.records[recordIndex].pointOffset > removed.pointOffset) {
            arena.records[recordIndex].pointOffset = static_cast<uint16_t>(
                arena.records[recordIndex].pointOffset - removed.pointCount
            );
        }
    }
    for (uint16_t recordIndex = index + 1U;
         recordIndex < arena.recordCount;
         ++recordIndex) {
        arena.records[recordIndex - 1U] = arena.records[recordIndex];
    }
    --arena.recordCount;
    arena.records[arena.recordCount] = {};
}

FLASHMEM void releaseCurve(ProjectCurveArena& arena, ProjectCurveId id) {
    const int16_t index = curveIndex(arena, id);
    if (index < 0) return;
    auto& record = arena.records[static_cast<uint16_t>(index)];
    if (record.referenceCount > 1U) {
        --record.referenceCount;
        return;
    }
    eraseCurveRecord(arena, static_cast<uint16_t>(index));
}

FLASHMEM ProjectModulationResult replaceOwnedCurve(
    ProjectCurveArena& arena,
    ProjectCurveId& owner,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    ModulatorId sourceId
) {
    if (!validProjectCurveSpec(spec, points, pointCount) ||
        curveInputAliasesArena(arena, points, pointCount)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    const int16_t recordPosition = curveIndex(arena, owner);
    if (recordPosition < 0) {
        return result(ProjectModulationStatus::INVARIANT_VIOLATION, sourceId);
    }
    auto& record = arena.records[static_cast<uint16_t>(recordPosition)];
    if (sameCurvePayload(arena, record, spec, points, pointCount)) {
        return result(ProjectModulationStatus::NO_CHANGE, sourceId, {}, record.id);
    }

    if (record.referenceCount > 1U) {
        if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
            arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
            return result(
                ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED,
                sourceId
            );
        }
        if (pointCount > PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
            return result(
                ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED,
                sourceId
            );
        }
        if (!canAllocateId(arena.nextCurveId)) {
            return result(ProjectModulationStatus::ID_EXHAUSTED, sourceId);
        }
        const ProjectCurveId replacement = appendCurve(
            arena,
            spec,
            points,
            pointCount
        );
        --record.referenceCount;
        owner = replacement;
        return result(ProjectModulationStatus::OK, sourceId, {}, replacement);
    }
    if (record.referenceCount != 1U) {
        return result(ProjectModulationStatus::INVARIANT_VIOLATION, sourceId);
    }

    const uint32_t available =
        PROJECT_CURVE_POINT_CAPACITY - arena.pointCount + record.pointCount;
    if (pointCount > available) {
        return result(
            ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED,
            sourceId
        );
    }

    const uint16_t oldCount = record.pointCount;
    const uint16_t oldTailOffset = static_cast<uint16_t>(
        record.pointOffset + oldCount
    );
    const uint16_t tailCount = static_cast<uint16_t>(
        arena.pointCount - oldTailOffset
    );
    if (pointCount != oldCount) {
        std::memmove(
            arena.points.data() + record.pointOffset + pointCount,
            arena.points.data() + oldTailOffset,
            static_cast<size_t>(tailCount) * sizeof(ProjectPackedCurvePoint)
        );
        const int32_t delta =
            static_cast<int32_t>(pointCount) - static_cast<int32_t>(oldCount);
        for (uint16_t index = 0; index < arena.recordCount; ++index) {
            if (index != static_cast<uint16_t>(recordPosition) &&
                arena.records[index].pointOffset > record.pointOffset) {
                arena.records[index].pointOffset = static_cast<uint16_t>(
                    static_cast<int32_t>(arena.records[index].pointOffset) + delta
                );
            }
        }
        arena.pointCount = static_cast<uint16_t>(
            static_cast<int32_t>(arena.pointCount) + delta
        );
    }
    std::memcpy(
        arena.points.data() + record.pointOffset,
        points,
        static_cast<size_t>(pointCount) * sizeof(ProjectPackedCurvePoint)
    );
    const ProjectCurveId retainedId = record.id;
    const uint16_t retainedOffset = record.pointOffset;
    populateCurveRecord(
        record,
        retainedId,
        retainedOffset,
        pointCount,
        1U,
        spec
    );
    return result(ProjectModulationStatus::OK, sourceId, {}, retainedId);
}

FLASHMEM ProjectModulationResult replaceOwnedCurveSpec(
    ProjectCurveArena& arena,
    ProjectCurveId& owner,
    const ProjectCurveSpec& spec,
    ModulatorId sourceId
) {
    const int16_t recordPosition = curveIndex(arena, owner);
    if (recordPosition < 0) {
        return result(ProjectModulationStatus::INVARIANT_VIOLATION, sourceId);
    }
    auto& record = arena.records[static_cast<uint16_t>(recordPosition)];
    const auto* points = arena.points.data() + record.pointOffset;
    if (!validProjectCurveSpec(spec, points, record.pointCount)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    if (record.sourceDurationTicks == spec.sourceDurationTicks &&
        record.durationTicks == spec.durationTicks &&
        record.windowOffsetTicks == spec.windowOffsetTicks &&
        record.interpolation == spec.interpolation &&
        record.valueDomain == spec.valueDomain &&
        record.origin == spec.origin) {
        return result(
            ProjectModulationStatus::NO_CHANGE,
            sourceId,
            {},
            record.id
        );
    }

    if (record.referenceCount > 1U) {
        if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
            arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
            return result(
                ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED,
                sourceId
            );
        }
        if (record.pointCount >
            PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
            return result(
                ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED,
                sourceId
            );
        }
        if (!canAllocateId(arena.nextCurveId)) {
            return result(ProjectModulationStatus::ID_EXHAUSTED, sourceId);
        }
        // appendCurve writes after arena.pointCount, so this arena-owned input
        // is adjacent or disjoint and cannot overlap the destination range.
        const ProjectCurveId replacement = appendCurve(
            arena,
            spec,
            points,
            record.pointCount
        );
        --record.referenceCount;
        owner = replacement;
        return result(
            ProjectModulationStatus::OK,
            sourceId,
            {},
            replacement
        );
    }
    if (record.referenceCount != 1U) {
        return result(ProjectModulationStatus::INVARIANT_VIOLATION, sourceId);
    }

    record.sourceDurationTicks = spec.sourceDurationTicks;
    record.durationTicks = spec.durationTicks;
    record.windowOffsetTicks = spec.windowOffsetTicks;
    record.interpolation = spec.interpolation;
    record.valueDomain = spec.valueDomain;
    record.origin = spec.origin;
    return result(ProjectModulationStatus::OK, sourceId, {}, record.id);
}

FLASHMEM bool selectedForSplit(
    ModulationBindingId id,
    const ModulatorSplitRequest& request
) {
    for (uint16_t index = 0; index < request.bindingCountToMove; ++index) {
        if (request.bindingIdsToMove[index] == id) return true;
    }
    return false;
}

FLASHMEM bool validSourceName(const ModulatorSourceState& source) {
    if (source.name[0] == '\0') return false;
    bool terminated = false;
    for (char value : source.name) {
        if (terminated && value != '\0') return false;
        if (value == '\0') terminated = true;
    }
    return terminated;
}

FLASHMEM bool allZeroBytes(const uint8_t* values, size_t count) {
    if (values == nullptr && count != 0U) return false;
    for (size_t index = 0U; index < count; ++index) {
        if (values[index] != 0U) return false;
    }
    return true;
}

}  // namespace project_modulation_detail

}  // namespace core::state::modulation

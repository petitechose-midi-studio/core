#include "state/modulation/ProjectModulationDomainOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

namespace {

constexpr uint8_t SOURCE_FLAGS = PROJECT_MODULATOR_FLAG_ENABLED;
constexpr uint8_t BINDING_FLAGS = PROJECT_MODULATION_BINDING_FLAG_ENABLED;
constexpr uint8_t TRIGGER_FLAGS = PROJECT_MODULATION_TRIGGER_FLAG_ENABLED;

FLASHMEM ProjectModulationResult result(
    ProjectModulationStatus status,
    ModulatorId sourceId = {},
    ModulationBindingId bindingId = {},
    ProjectCurveId curveId = {}
) {
    return {status, sourceId, bindingId, curveId};
}

FLASHMEM bool canAllocateId(uint32_t next, uint16_t count = 1) {
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
           static_cast<uint8_t>(parameters.timing) <=
               static_cast<uint8_t>(ModulatorTimingMode::FREE) &&
           static_cast<uint8_t>(parameters.retrigger) <=
               static_cast<uint8_t>(ModulatorAdsrRetriggerMode::LEGATO) &&
           static_cast<uint8_t>(parameters.curve) <=
               static_cast<uint8_t>(ModulatorAdsrCurve::EXPONENTIAL) &&
           parameters.reserved == 0U;
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

FLASHMEM bool validTriggerRef(const ModulationTriggerRef& trigger) {
    if (static_cast<uint8_t>(trigger.kind) >
        static_cast<uint8_t>(ModulationTriggerKind::TRACK_NOTE)) {
        return false;
    }
    if (trigger.track >= PROJECT_MODULATION_TRACK_COUNT) return false;
    if (trigger.kind == ModulationTriggerKind::TRACK_NOTE) {
        const bool channelValid = trigger.channel < 16U ||
            trigger.channel == PROJECT_MODULATION_TRIGGER_ANY_CHANNEL;
        const bool noteValid = trigger.data <= 127U ||
            trigger.data == PROJECT_MODULATION_TRIGGER_ANY_NOTE;
        return channelValid && noteValid;
    }
    return trigger.channel < 16U && trigger.data <= 127U;
}

template <typename Entry, size_t Capacity>
FLASHMEM void eraseDense(
    std::array<Entry, Capacity>& entries,
    uint16_t& count,
    uint16_t index
);

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
    ModulatorId sourceId = {}
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

template <typename Entry, size_t Capacity>
FLASHMEM void eraseDense(
    std::array<Entry, Capacity>& entries,
    uint16_t& count,
    uint16_t index
) {
    for (uint16_t cursor = index + 1U; cursor < count; ++cursor) {
        entries[cursor - 1U] = entries[cursor];
    }
    --count;
    entries[count] = {};
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

template <size_t Capacity>
FLASHMEM bool allZero(const std::array<uint8_t, Capacity>& values) {
    for (uint8_t value : values) {
        if (value != 0U) return false;
    }
    return true;
}

}  // namespace

FLASHMEM bool validProjectModulatorAdsrParameters(
    const ModulatorAdsrParameters& parameters
) {
    return validAdsrParameters(parameters);
}

FLASHMEM bool validProjectCurveSpec(
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    if (points == nullptr || pointCount == 0 ||
        spec.sourceDurationTicks == 0 || spec.durationTicks == 0 ||
        spec.windowOffsetTicks > spec.sourceDurationTicks ||
        spec.interpolation != ProjectCurveInterpolation::LINEAR ||
        static_cast<uint8_t>(spec.valueDomain) >
            static_cast<uint8_t>(ProjectCurveValueDomain::BIPOLAR) ||
        static_cast<uint8_t>(spec.origin) >
            static_cast<uint8_t>(ProjectCurveOrigin::CONVERTED_MIN)) {
        return false;
    }
    for (uint16_t index = 0; index < pointCount; ++index) {
        if (points[index].tick > spec.sourceDurationTicks ||
            points[index].value == std::numeric_limits<int16_t>::min() ||
            (spec.valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR &&
             points[index].value < 0)) {
            return false;
        }
        if (index > 0 && points[index - 1U].tick > points[index].tick) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool projectModulatorNaturalDomain(
    const ModulatorSourceState& source,
    const ProjectCurveArena& arena,
    ModulatorNaturalDomain& out
) {
    if (source.kind == ModulatorKind::LFO) {
        out = ModulatorNaturalDomain::CENTERED;
        return true;
    }
    if (source.kind == ModulatorKind::ADSR) {
        out = ModulatorNaturalDomain::POSITIVE;
        return true;
    }
    if (source.kind != ModulatorKind::RECORDED_SHAPE) return false;
    const auto* curve = findProjectCurve(
        arena,
        source.parameters.recordedCurveId
    );
    if (curve == nullptr) return false;
    switch (curve->valueDomain) {
        case ProjectCurveValueDomain::BIPOLAR:
            out = ModulatorNaturalDomain::CENTERED;
            return true;
        case ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR:
            out = ModulatorNaturalDomain::POSITIVE;
            return true;
        default:
            return false;
    }
}

FLASHMEM bool resolveModulationApplication(
    ModulationApplication application,
    ModulatorNaturalDomain naturalDomain,
    ResolvedModulationMapping& out
) {
    if (static_cast<uint8_t>(naturalDomain) >
        static_cast<uint8_t>(ModulatorNaturalDomain::POSITIVE)) {
        return false;
    }
    switch (application) {
        case ModulationApplication::NATURAL:
            out = ResolvedModulationMapping::IDENTITY;
            return true;
        case ModulationApplication::AROUND_BASE:
            out = naturalDomain == ModulatorNaturalDomain::CENTERED
                ? ResolvedModulationMapping::IDENTITY
                : ResolvedModulationMapping::POSITIVE_TO_CENTERED;
            return true;
        case ModulationApplication::FROM_BASE:
            out = naturalDomain == ModulatorNaturalDomain::POSITIVE
                ? ResolvedModulationMapping::IDENTITY
                : ResolvedModulationMapping::CENTERED_TO_POSITIVE;
            return true;
        default:
            return false;
    }
}

FLASHMEM const ModulatorSourceState* findProjectModulator(
    const ProjectModulationState& state,
    ModulatorId id
) {
    const int16_t index = sourceIndex(state, id);
    return index < 0 ? nullptr : &state.sources[static_cast<uint16_t>(index)];
}

FLASHMEM ModulatorSourceState* findProjectModulator(
    ProjectModulationState& state,
    ModulatorId id
) {
    const int16_t index = sourceIndex(state, id);
    return index < 0 ? nullptr : &state.sources[static_cast<uint16_t>(index)];
}

FLASHMEM const ModulationBindingState* findProjectModulationBinding(
    const ProjectModulationState& state,
    ModulationBindingId id
) {
    const int16_t index = outputBindingIndex(state, id);
    return index >= 0
        ? &state.outputBindings[static_cast<uint16_t>(index)]
        : nullptr;
}

FLASHMEM ModulationBindingState* findProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId id
) {
    const int16_t index = outputBindingIndex(state, id);
    return index >= 0
        ? &state.outputBindings[static_cast<uint16_t>(index)]
        : nullptr;
}

FLASHMEM const ModulationTriggerBindingState*
findProjectModulationTriggerForSource(
    const ProjectModulationState& state,
    ModulatorId sourceId
) {
    const int16_t index = triggerIndexForSource(state, sourceId);
    return index < 0
        ? nullptr
        : &state.triggerBindings[static_cast<uint16_t>(index)];
}

FLASHMEM ModulationTriggerBindingState* findProjectModulationTriggerForSource(
    ProjectModulationState& state,
    ModulatorId sourceId
) {
    const int16_t index = triggerIndexForSource(state, sourceId);
    return index < 0
        ? nullptr
        : &state.triggerBindings[static_cast<uint16_t>(index)];
}

FLASHMEM const ModulationDestinationScaleState*
findProjectModulationDestinationScale(
    const ProjectModulationState& state,
    const ModulationDestination& destination
) {
    if (!modulationDestinationValid(destination)) return nullptr;
    const int16_t index = destinationScaleIndex(state, destination);
    return index < 0
        ? nullptr
        : &state.destinationScales[static_cast<uint16_t>(index)];
}

FLASHMEM uint16_t projectModulationDestinationScaleQ15(
    const ProjectModulationState& state,
    const ModulationDestination& destination
) {
    const auto* entry = findProjectModulationDestinationScale(
        state,
        destination
    );
    return entry != nullptr
        ? entry->scaleQ15
        : PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
}

FLASHMEM void formatNextProjectLfoName(
    const ProjectModulationState& state,
    char* out,
    size_t outSize
) {
    formatNextProjectModulatorName(state, ModulatorKind::LFO, out, outSize);
}

FLASHMEM void formatNextProjectModulatorName(
    const ProjectModulationState& state,
    ModulatorKind kind,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    const char* prefix = "Motion";
    if (kind == ModulatorKind::LFO) {
        prefix = "LFO";
    } else if (kind == ModulatorKind::ADSR) {
        prefix = "ADSR";
    }
    for (uint16_t ordinal = 1; ordinal <= PROJECT_MODULATOR_CAPACITY; ++ordinal) {
        std::snprintf(
            out,
            outSize,
            "%s %u",
            prefix,
            static_cast<unsigned>(ordinal)
        );
        bool used = false;
        for (uint16_t index = 0; index < state.sourceCount; ++index) {
            if (std::strncmp(
                    state.sources[index].name.data(),
                    out,
                    PROJECT_MODULATOR_NAME_CAPACITY
                ) == 0) {
                used = true;
                break;
            }
        }
        if (!used) return;
    }
    std::snprintf(out, outSize, "%s", prefix);
}

FLASHMEM const ProjectCurveRecord* findProjectCurve(
    const ProjectCurveArena& arena,
    ProjectCurveId id
) {
    const int16_t index = curveIndex(arena, id);
    return index < 0 ? nullptr : &arena.records[static_cast<uint16_t>(index)];
}

FLASHMEM const ProjectAutomationCurveEntry* findProjectAutomationCurve(
    const ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination
) {
    for (uint16_t index = 0; index < automation.entryCount; ++index) {
        if (automation.entries[index].destination == destination) {
            return &automation.entries[index];
        }
    }
    return nullptr;
}

FLASHMEM ProjectAutomationCurveEntry* findProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination
) {
    return const_cast<ProjectAutomationCurveEntry*>(findProjectAutomationCurve(
        static_cast<const ProjectAutomationCurveDirectory&>(automation),
        destination
    ));
}

FLASHMEM ProjectModulationResult setProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    bool enabled
) {
    if (!modulationDestinationValid(destination) ||
        spec.valueDomain != ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
        spec.origin != ProjectCurveOrigin::NATIVE ||
        !validProjectCurveSpec(spec, points, pointCount) ||
        curveInputAliasesArena(arena, points, pointCount)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }

    auto* existing = findProjectAutomationCurve(automation, destination);
    const uint8_t flags = enabled ? PROJECT_AUTOMATION_CURVE_FLAG_ENABLED : 0U;
    if (existing != nullptr) {
        const uint8_t previousFlags = existing->flags;
        const auto replaced = replaceOwnedCurve(
            arena,
            existing->curveId,
            spec,
            points,
            pointCount
        );
        if (replaced.status != ProjectModulationStatus::OK &&
            replaced.status != ProjectModulationStatus::NO_CHANGE) {
            return replaced;
        }
        existing->flags = flags;
        if (replaced.status == ProjectModulationStatus::NO_CHANGE &&
            previousFlags == flags) {
            return replaced;
        }
        return result(
            ProjectModulationStatus::OK,
            {},
            {},
            existing->curveId
        );
    }

    if (automation.entryCount >= PROJECT_AUTOMATION_ENTRY_CAPACITY) {
        return result(ProjectModulationStatus::AUTOMATION_CAPACITY_EXCEEDED);
    }
    if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
        return result(ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED);
    }
    if (pointCount > PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
        return result(ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED);
    }
    if (!canAllocateId(arena.nextCurveId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED);
    }

    const ProjectCurveId curveId = appendCurve(arena, spec, points, pointCount);
    auto& entry = automation.entries[automation.entryCount++];
    entry = {};
    entry.destination = destination;
    entry.curveId = curveId;
    entry.flags = flags;
    return result(ProjectModulationStatus::OK, {}, {}, curveId);
}

FLASHMEM ProjectModulationResult setProjectAutomationEnabled(
    ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination,
    bool enabled
) {
    auto* entry = findProjectAutomationCurve(automation, destination);
    if (entry == nullptr) return result(ProjectModulationStatus::INVALID_ID);
    const uint8_t flags = enabled ? PROJECT_AUTOMATION_CURVE_FLAG_ENABLED : 0U;
    if (entry->flags == flags) {
        return result(ProjectModulationStatus::NO_CHANGE, {}, {}, entry->curveId);
    }
    entry->flags = flags;
    return result(ProjectModulationStatus::OK, {}, {}, entry->curveId);
}

FLASHMEM ProjectModulationResult duplicateProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& source,
    const ModulationDestination& destination
) {
    if (!modulationDestinationValid(source) ||
        !modulationDestinationValid(destination) || source == destination ||
        findProjectAutomationCurve(automation, destination) != nullptr) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    const auto* sourceEntry = findProjectAutomationCurve(automation, source);
    if (sourceEntry == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID);
    }
    auto* curve = const_cast<ProjectCurveRecord*>(findProjectCurve(
        arena,
        sourceEntry->curveId
    ));
    if (curve == nullptr || curve->referenceCount == 0U) {
        return result(ProjectModulationStatus::INVARIANT_VIOLATION);
    }
    if (automation.entryCount >= PROJECT_AUTOMATION_ENTRY_CAPACITY) {
        return result(ProjectModulationStatus::AUTOMATION_CAPACITY_EXCEEDED);
    }
    if (curve->referenceCount == std::numeric_limits<uint16_t>::max()) {
        return result(
            ProjectModulationStatus::CURVE_REFERENCE_CAPACITY_EXCEEDED
        );
    }

    auto copy = *sourceEntry;
    copy.destination = destination;
    automation.entries[automation.entryCount++] = copy;
    ++curve->referenceCount;
    return result(ProjectModulationStatus::OK, {}, {}, copy.curveId);
}

FLASHMEM ProjectModulationResult removeProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination
) {
    for (uint16_t index = 0; index < automation.entryCount; ++index) {
        if (automation.entries[index].destination != destination) continue;
        const ProjectCurveId curveId = automation.entries[index].curveId;
        releaseCurve(arena, curveId);
        eraseDense(automation.entries, automation.entryCount, index);
        return result(ProjectModulationStatus::OK, {}, {}, curveId);
    }
    return result(ProjectModulationStatus::INVALID_ID);
}

FLASHMEM ProjectModulationResult createLfoModulator(
    ProjectModulationState& state,
    const ModulatorLfoDraft& draft
) {
    if (!validLfoParameters(draft.parameters)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    if (state.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        return result(ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED);
    }
    if (!canAllocateId(state.nextSourceId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED);
    }

    ModulatorSourceState source{};
    source.id = {takeId(state.nextSourceId)};
    copyName(source.name, draft.name, "LFO");
    source.kind = ModulatorKind::LFO;
    source.flags = draft.enabled ? PROJECT_MODULATOR_FLAG_ENABLED : 0U;
    source.accent = draft.accent;
    source.parameters.lfo = draft.parameters;
    state.sources[state.sourceCount++] = source;
    return result(ProjectModulationStatus::OK, source.id);
}

FLASHMEM ProjectModulationResult createAdsrModulator(
    ProjectModulationState& state,
    const ModulatorAdsrDraft& draft
) {
    if (!validAdsrParameters(draft.parameters)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    if (state.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        return result(ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED);
    }
    if (!canAllocateId(state.nextSourceId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED);
    }

    ModulatorSourceState source{};
    source.id = {takeId(state.nextSourceId)};
    copyName(source.name, draft.name, "ADSR");
    source.kind = ModulatorKind::ADSR;
    source.flags = draft.enabled ? PROJECT_MODULATOR_FLAG_ENABLED : 0U;
    source.accent = draft.accent;
    source.parameters.adsr = draft.parameters;
    state.sources[state.sourceCount++] = source;
    return result(ProjectModulationStatus::OK, source.id);
}

FLASHMEM ProjectModulationResult createRecordedShapeModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    const RecordedShapeDraft& draft
) {
    if (!validProjectCurveSpec(draft.curve, draft.points, draft.pointCount) ||
        curveInputAliasesArena(arena, draft.points, draft.pointCount)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    if (state.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        return result(ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED);
    }
    if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
        return result(
            ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED
        );
    }
    if (draft.pointCount >
        PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
        return result(ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED);
    }
    if (!canAllocateId(state.nextSourceId) ||
        !canAllocateId(arena.nextCurveId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED);
    }

    ModulatorSourceState source{};
    source.id = {takeId(state.nextSourceId)};
    copyName(source.name, draft.name, "Recorded Shape");
    source.kind = ModulatorKind::RECORDED_SHAPE;
    source.flags = draft.enabled ? PROJECT_MODULATOR_FLAG_ENABLED : 0U;
    source.accent = draft.accent;
    source.parameters.recordedCurveId = appendCurve(
        arena,
        draft.curve,
        draft.points,
        draft.pointCount
    );
    state.sources[state.sourceCount++] = source;
    return result(
        ProjectModulationStatus::OK,
        source.id,
        {},
        source.parameters.recordedCurveId
    );
}

FLASHMEM ProjectModulationResult duplicateProjectModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    ModulatorId sourceId,
    const char* cloneName
) {
    const int16_t existingIndex = sourceIndex(state, sourceId);
    if (existingIndex < 0) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    if (state.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        return result(ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED, sourceId);
    }
    const auto& existing = state.sources[static_cast<uint16_t>(existingIndex)];
    const int16_t existingTrigger = triggerIndexForSource(state, sourceId);
    if (existingTrigger >= 0 &&
        state.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY) {
        return result(
            ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED,
            sourceId
        );
    }
    if (!canAllocateId(state.nextSourceId) ||
        !canAllocateId(state.nextBindingId, existingTrigger >= 0 ? 1U : 0U)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED, sourceId);
    }
    ProjectCurveRecord* curve = nullptr;
    if (existing.kind == ModulatorKind::RECORDED_SHAPE) {
        const int16_t index = curveIndex(
            arena,
            existing.parameters.recordedCurveId
        );
        if (index < 0) {
            return result(ProjectModulationStatus::INVARIANT_VIOLATION, sourceId);
        }
        curve = &arena.records[static_cast<uint16_t>(index)];
        if (curve->referenceCount == std::numeric_limits<uint16_t>::max()) {
            return result(
                ProjectModulationStatus::CURVE_REFERENCE_CAPACITY_EXCEEDED,
                sourceId
            );
        }
    }

    ModulatorSourceState clone = existing;
    clone.id = {takeId(state.nextSourceId)};
    copyName(clone.name, cloneName, existing.name.data());
    state.sources[state.sourceCount++] = clone;
    if (curve != nullptr) ++curve->referenceCount;
    if (existingTrigger >= 0) {
        auto trigger = state.triggerBindings[
            static_cast<uint16_t>(existingTrigger)
        ];
        trigger.id = {takeId(state.nextBindingId)};
        trigger.sourceId = clone.id;
        state.triggerBindings[state.triggerBindingCount++] = trigger;
    }
    return result(ProjectModulationStatus::OK, clone.id);
}

FLASHMEM ProjectModulationResult splitProjectModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    const ModulatorSplitRequest& request
) {
    const int16_t existingIndex = sourceIndex(state, request.sourceId);
    if (existingIndex < 0) {
        return result(ProjectModulationStatus::INVALID_ID, request.sourceId);
    }
    if (request.bindingIdsToMove == nullptr ||
        request.bindingCountToMove == 0) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, request.sourceId);
    }
    uint16_t sourceBindingCount = 0;
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        if (state.outputBindings[index].sourceId == request.sourceId) {
            ++sourceBindingCount;
        }
    }
    if (request.bindingCountToMove >= sourceBindingCount) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, request.sourceId);
    }
    for (uint16_t selected = 0;
         selected < request.bindingCountToMove;
         ++selected) {
        if (!valid(request.bindingIdsToMove[selected])) {
            return result(ProjectModulationStatus::INVALID_ID, request.sourceId);
        }
        for (uint16_t prior = 0; prior < selected; ++prior) {
            if (request.bindingIdsToMove[prior] ==
                request.bindingIdsToMove[selected]) {
                return result(
                    ProjectModulationStatus::INVALID_ARGUMENT,
                    request.sourceId
                );
            }
        }
        const int16_t index = outputBindingIndex(
            state,
            request.bindingIdsToMove[selected]
        );
        if (index < 0 ||
            state.outputBindings[static_cast<uint16_t>(index)].sourceId !=
                request.sourceId) {
            return result(ProjectModulationStatus::INVALID_ID, request.sourceId);
        }
    }
    if (state.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        return result(
            ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED,
            request.sourceId
        );
    }

    const auto& existing = state.sources[static_cast<uint16_t>(existingIndex)];
    const int16_t existingTrigger = triggerIndexForSource(state, request.sourceId);
    const uint16_t bindingIdsNeeded = existingTrigger >= 0 ? 1U : 0U;
    if (existingTrigger >= 0 &&
        state.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY) {
        return result(
            ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED,
            request.sourceId
        );
    }
    if (!canAllocateId(state.nextSourceId) ||
        !canAllocateId(state.nextBindingId, bindingIdsNeeded)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED, request.sourceId);
    }
    ProjectCurveRecord* curve = nullptr;
    if (existing.kind == ModulatorKind::RECORDED_SHAPE) {
        const int16_t index = curveIndex(
            arena,
            existing.parameters.recordedCurveId
        );
        if (index < 0) {
            return result(
                ProjectModulationStatus::INVARIANT_VIOLATION,
                request.sourceId
            );
        }
        curve = &arena.records[static_cast<uint16_t>(index)];
        if (curve->referenceCount == std::numeric_limits<uint16_t>::max()) {
            return result(
                ProjectModulationStatus::CURVE_REFERENCE_CAPACITY_EXCEEDED,
                request.sourceId
            );
        }
    }

    ModulatorSourceState clone = existing;
    clone.id = {takeId(state.nextSourceId)};
    copyName(clone.name, request.cloneName, existing.name.data());
    state.sources[state.sourceCount++] = clone;
    if (curve != nullptr) ++curve->referenceCount;

    if (existingTrigger >= 0) {
        auto trigger = state.triggerBindings[
            static_cast<uint16_t>(existingTrigger)
        ];
        trigger.id = {takeId(state.nextBindingId)};
        trigger.sourceId = clone.id;
        state.triggerBindings[state.triggerBindingCount++] = trigger;
    }
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        auto& binding = state.outputBindings[index];
        if (binding.sourceId == request.sourceId &&
            selectedForSplit(binding.id, request)) {
            binding.sourceId = clone.id;
        }
    }
    return result(ProjectModulationStatus::OK, clone.id);
}

FLASHMEM ProjectModulationResult deleteProjectModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    ModulatorId sourceId
) {
    const int16_t index = sourceIndex(state, sourceId);
    if (index < 0) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    const auto source = state.sources[static_cast<uint16_t>(index)];
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        const int16_t record = curveIndex(
            arena,
            source.parameters.recordedCurveId
        );
        if (record < 0 ||
            arena.records[static_cast<uint16_t>(record)].referenceCount == 0) {
            return result(
                ProjectModulationStatus::INVARIANT_VIOLATION,
                sourceId
            );
        }
    }
    for (uint16_t cursor = 0; cursor < state.outputBindingCount;) {
        if (state.outputBindings[cursor].sourceId == sourceId) {
            eraseDense(state.outputBindings, state.outputBindingCount, cursor);
        } else {
            ++cursor;
        }
    }
    pruneUnboundDestinationScales(state);
    for (uint16_t cursor = 0; cursor < state.triggerBindingCount;) {
        if (state.triggerBindings[cursor].sourceId == sourceId) {
            eraseDense(state.triggerBindings, state.triggerBindingCount, cursor);
        } else {
            ++cursor;
        }
    }
    eraseDense(state.sources, state.sourceCount, static_cast<uint16_t>(index));
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        releaseCurve(arena, source.parameters.recordedCurveId);
    }
    return result(ProjectModulationStatus::OK, sourceId);
}

FLASHMEM ProjectModulationResult setProjectModulatorEnabled(
    ProjectModulationState& state,
    ModulatorId sourceId,
    bool enabled
) {
    auto* source = findProjectModulator(state, sourceId);
    if (source == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    const uint8_t flags = enabled
        ? static_cast<uint8_t>(source->flags | PROJECT_MODULATOR_FLAG_ENABLED)
        : static_cast<uint8_t>(source->flags & ~PROJECT_MODULATOR_FLAG_ENABLED);
    if (source->flags == flags) {
        return result(ProjectModulationStatus::NO_CHANGE, sourceId);
    }
    source->flags = flags;
    return result(ProjectModulationStatus::OK, sourceId);
}

FLASHMEM ProjectModulationResult setProjectModulatorName(
    ProjectModulationState& state,
    ModulatorId sourceId,
    const char* name
) {
    auto* source = findProjectModulator(state, sourceId);
    if (source == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    if (name == nullptr || name[0] == '\0') {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    std::array<char, PROJECT_MODULATOR_NAME_CAPACITY> next{};
    copyName(next, name, nullptr);
    if (source->name == next) {
        return result(ProjectModulationStatus::NO_CHANGE, sourceId);
    }
    source->name = next;
    return result(ProjectModulationStatus::OK, sourceId);
}

FLASHMEM ProjectModulationResult setProjectLfoParameters(
    ProjectModulationState& state,
    ModulatorId sourceId,
    const ModulatorLfoParameters& parameters
) {
    auto* source = findProjectModulator(state, sourceId);
    if (source == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    if (source->kind != ModulatorKind::LFO || !validLfoParameters(parameters)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    if (std::memcmp(
            &source->parameters.lfo,
            &parameters,
            sizeof(parameters)
        ) == 0) {
        return result(ProjectModulationStatus::NO_CHANGE, sourceId);
    }
    source->parameters.lfo = parameters;
    return result(ProjectModulationStatus::OK, sourceId);
}

FLASHMEM ProjectModulationResult setProjectAdsrParameters(
    ProjectModulationState& state,
    ModulatorId sourceId,
    const ModulatorAdsrParameters& parameters
) {
    auto* source = findProjectModulator(state, sourceId);
    if (source == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    if (source->kind != ModulatorKind::ADSR ||
        !validAdsrParameters(parameters)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    if (std::memcmp(
            &source->parameters.adsr,
            &parameters,
            sizeof(parameters)
        ) == 0) {
        return result(ProjectModulationStatus::NO_CHANGE, sourceId);
    }
    source->parameters.adsr = parameters;
    return result(ProjectModulationStatus::OK, sourceId);
}

FLASHMEM ProjectModulationResult addProjectModulationBinding(
    ProjectModulationState& state,
    const ModulationBindingDraft& draft
) {
    const auto* source = findProjectModulator(state, draft.sourceId);
    if (source == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, draft.sourceId);
    }
    if (!modulationDestinationValid(draft.destination) ||
        draft.amountQ15 == std::numeric_limits<int16_t>::min() ||
        static_cast<uint8_t>(draft.application) >
            static_cast<uint8_t>(ModulationApplication::FROM_BASE) ||
        draft.transfer != ModulationTransfer::LINEAR) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, draft.sourceId);
    }
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& existing = state.outputBindings[index];
        if (existing.sourceId == draft.sourceId &&
            existing.destination == draft.destination) {
            return result(
                ProjectModulationStatus::DUPLICATE_BINDING,
                draft.sourceId,
                existing.id
            );
        }
    }
    if (state.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        return result(
            ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED,
            draft.sourceId
        );
    }
    if (!canAllocateId(state.nextBindingId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED, draft.sourceId);
    }

    ModulationBindingState binding{};
    binding.id = {takeId(state.nextBindingId)};
    binding.sourceId = draft.sourceId;
    binding.destination = draft.destination;
    binding.amountQ15 = draft.amountQ15;
    binding.application = draft.application;
    binding.transfer = draft.transfer;
    binding.slewMs = draft.slewMs;
    binding.flags = draft.enabled
        ? PROJECT_MODULATION_BINDING_FLAG_ENABLED
        : 0U;
    state.outputBindings[state.outputBindingCount++] = binding;
    return result(
        ProjectModulationStatus::OK,
        draft.sourceId,
        binding.id
    );
}

FLASHMEM ProjectModulationResult removeProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId bindingId
) {
    const int16_t index = outputBindingIndex(state, bindingId);
    if (index < 0) {
        return result(ProjectModulationStatus::INVALID_ID, {}, bindingId);
    }
    const auto removed = state.outputBindings[static_cast<uint16_t>(index)];
    eraseDense(
        state.outputBindings,
        state.outputBindingCount,
        static_cast<uint16_t>(index)
    );
    pruneDestinationScaleIfUnbound(state, removed.destination);
    return result(ProjectModulationStatus::OK, removed.sourceId, bindingId);
}

FLASHMEM ProjectModulationResult updateProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId bindingId,
    int16_t amountQ15,
    ModulationApplication application,
    ModulationTransfer transfer,
    bool enabled,
    uint16_t slewMs
) {
    const int16_t index = outputBindingIndex(state, bindingId);
    if (index < 0) {
        return result(ProjectModulationStatus::INVALID_ID, {}, bindingId);
    }
    if (amountQ15 == std::numeric_limits<int16_t>::min() ||
        static_cast<uint8_t>(application) >
            static_cast<uint8_t>(ModulationApplication::FROM_BASE) ||
        transfer != ModulationTransfer::LINEAR) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, {}, bindingId);
    }
    auto& binding = state.outputBindings[static_cast<uint16_t>(index)];
    const uint8_t flags = enabled
        ? PROJECT_MODULATION_BINDING_FLAG_ENABLED
        : 0U;
    if (binding.amountQ15 == amountQ15 &&
        binding.application == application &&
        binding.transfer == transfer &&
        binding.slewMs == slewMs &&
        binding.flags == flags) {
        return result(
            ProjectModulationStatus::NO_CHANGE,
            binding.sourceId,
            bindingId
        );
    }
    binding.amountQ15 = amountQ15;
    binding.application = application;
    binding.transfer = transfer;
    binding.slewMs = slewMs;
    binding.flags = flags;
    return result(
        ProjectModulationStatus::OK,
        binding.sourceId,
        bindingId
    );
}

FLASHMEM ProjectModulationResult setProjectModulationDestinationScale(
    ProjectModulationState& state,
    const ModulationDestination& destination,
    uint16_t scaleQ15
) {
    if (!modulationDestinationValid(destination) ||
        !destinationHasBinding(state, destination)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    const int16_t existing = destinationScaleIndex(state, destination);
    if (scaleQ15 == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) {
        if (existing < 0) return result(ProjectModulationStatus::NO_CHANGE);
        eraseDense(
            state.destinationScales,
            state.destinationScaleCount,
            static_cast<uint16_t>(existing)
        );
        return result(ProjectModulationStatus::OK);
    }
    if (existing >= 0) {
        auto& entry = state.destinationScales[static_cast<uint16_t>(existing)];
        if (entry.scaleQ15 == scaleQ15) {
            return result(ProjectModulationStatus::NO_CHANGE);
        }
        entry.scaleQ15 = scaleQ15;
        return result(ProjectModulationStatus::OK);
    }
    if (state.destinationScaleCount >=
        PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY) {
        return result(
            ProjectModulationStatus::DESTINATION_SCALE_CAPACITY_EXCEEDED
        );
    }
    const uint16_t address = modulationDestinationStableAddress(destination);
    uint16_t insertion = 0;
    while (insertion < state.destinationScaleCount &&
           modulationDestinationStableAddress(
               state.destinationScales[insertion].destination
           ) < address) {
        ++insertion;
    }
    for (uint16_t cursor = state.destinationScaleCount;
         cursor > insertion;
         --cursor) {
        state.destinationScales[cursor] = state.destinationScales[cursor - 1U];
    }
    state.destinationScales[insertion] = {destination, scaleQ15};
    ++state.destinationScaleCount;
    return result(ProjectModulationStatus::OK);
}

FLASHMEM ProjectModulationResult addProjectModulationTrigger(
    ProjectModulationState& state,
    const ModulationTriggerDraft& draft
) {
    if (findProjectModulator(state, draft.sourceId) == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, draft.sourceId);
    }
    if (!validTriggerRef(draft.trigger)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, draft.sourceId);
    }
    const int16_t existing = triggerIndexForSource(state, draft.sourceId);
    if (existing >= 0) {
        return result(
            ProjectModulationStatus::DUPLICATE_TRIGGER,
            draft.sourceId,
            state.triggerBindings[static_cast<uint16_t>(existing)].id
        );
    }
    if (state.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY) {
        return result(
            ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED,
            draft.sourceId
        );
    }
    if (!canAllocateId(state.nextBindingId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED, draft.sourceId);
    }

    ModulationTriggerBindingState binding{};
    binding.id = {takeId(state.nextBindingId)};
    binding.sourceId = draft.sourceId;
    binding.trigger = draft.trigger;
    binding.flags = draft.enabled ? PROJECT_MODULATION_TRIGGER_FLAG_ENABLED : 0U;
    state.triggerBindings[state.triggerBindingCount++] = binding;
    return result(
        ProjectModulationStatus::OK,
        draft.sourceId,
        binding.id
    );
}

FLASHMEM ProjectModulationResult setProjectModulationTrigger(
    ProjectModulationState& state,
    ModulatorId sourceId,
    const ModulationTriggerRef& trigger,
    bool enabled
) {
    if (!validTriggerRef(trigger)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    auto* binding = findProjectModulationTriggerForSource(state, sourceId);
    if (binding == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    const uint8_t flags = enabled
        ? PROJECT_MODULATION_TRIGGER_FLAG_ENABLED
        : 0U;
    if (binding->trigger == trigger && binding->flags == flags) {
        return result(
            ProjectModulationStatus::NO_CHANGE,
            sourceId,
            binding->id
        );
    }
    binding->trigger = trigger;
    binding->flags = flags;
    return result(ProjectModulationStatus::OK, sourceId, binding->id);
}

FLASHMEM ProjectModulationResult removeProjectModulationTrigger(
    ProjectModulationState& state,
    ModulationBindingId bindingId
) {
    const int16_t index = triggerBindingIndex(state, bindingId);
    if (index < 0) {
        return result(ProjectModulationStatus::INVALID_ID, {}, bindingId);
    }
    const auto sourceId = state.triggerBindings[
        static_cast<uint16_t>(index)
    ].sourceId;
    eraseDense(
        state.triggerBindings,
        state.triggerBindingCount,
        static_cast<uint16_t>(index)
    );
    return result(ProjectModulationStatus::OK, sourceId, bindingId);
}

FLASHMEM ProjectModulationResult replaceRecordedShapeCurve(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    ModulatorId sourceId,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    const int16_t sourcePosition = sourceIndex(state, sourceId);
    if (sourcePosition < 0) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    auto& source = state.sources[static_cast<uint16_t>(sourcePosition)];
    if (source.kind != ModulatorKind::RECORDED_SHAPE) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    return replaceOwnedCurve(
        arena,
        source.parameters.recordedCurveId,
        spec,
        points,
        pointCount,
        sourceId
    );
}

FLASHMEM bool validProjectModulationDomain(
    const ProjectModulationState& state,
    const ProjectCurveArena& arena,
    const ProjectAutomationCurveDirectory* automation
) {
    if (state.sourceCount > PROJECT_MODULATOR_CAPACITY ||
        state.outputBindingCount > PROJECT_MODULATION_BINDING_CAPACITY ||
        state.triggerBindingCount > PROJECT_MODULATION_TRIGGER_CAPACITY ||
        state.destinationScaleCount >
            PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY ||
        arena.recordCount > PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount > PROJECT_CURVE_RECORD_CAPACITY ||
        arena.pointCount > PROJECT_CURVE_POINT_CAPACITY ||
        (automation != nullptr &&
         (automation->entryCount > PROJECT_AUTOMATION_ENTRY_CAPACITY ||
          automation->reserved != 0U))) {
        return false;
    }

    uint16_t previousScaleAddress = 0U;
    for (uint16_t index = 0; index < state.destinationScaleCount; ++index) {
        const auto& entry = state.destinationScales[index];
        const uint16_t address = modulationDestinationStableAddress(
            entry.destination
        );
        if (!modulationDestinationValid(entry.destination) ||
            entry.scaleQ15 == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 ||
            !destinationHasBinding(state, entry.destination) ||
            (index > 0U && address <= previousScaleAddress)) {
            return false;
        }
        previousScaleAddress = address;
    }

    if (automation != nullptr) {
        for (uint16_t entry = 0; entry < automation->entryCount; ++entry) {
            const auto& current = automation->entries[entry];
            if (!modulationDestinationValid(current.destination) ||
                !valid(current.curveId) ||
                (current.flags & ~PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U ||
                !allZero(current.reserved)) {
                return false;
            }
            for (uint16_t prior = 0; prior < entry; ++prior) {
                if (automation->entries[prior].destination ==
                    current.destination) {
                    return false;
                }
            }
        }
    }

    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        const auto& source = state.sources[index];
        if (!valid(source.id) || !validSourceName(source) ||
            source.schemaVersion != 1U ||
            (source.flags & ~SOURCE_FLAGS) != 0U ||
            static_cast<uint8_t>(source.kind) >
                static_cast<uint8_t>(ModulatorKind::ADSR)) {
            return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            if (state.sources[prior].id == source.id) return false;
        }
        if (state.nextSourceId != 0 &&
            source.id.value >= state.nextSourceId) {
            return false;
        }
        if (source.kind == ModulatorKind::LFO) {
            if (!validLfoParameters(source.parameters.lfo)) {
                return false;
            }
        } else if (source.kind == ModulatorKind::ADSR) {
            if (!validAdsrParameters(source.parameters.adsr) ||
                !parameterTailZero(
                    source.parameters,
                    sizeof(ModulatorAdsrParameters)
                )) {
                return false;
            }
        } else if (source.kind == ModulatorKind::RECORDED_SHAPE) {
            if (!valid(source.parameters.recordedCurveId) ||
                curveIndex(arena, source.parameters.recordedCurveId) < 0 ||
                !parameterTailZero(
                    source.parameters,
                    sizeof(ProjectCurveId)
                )) {
                return false;
            }
        } else {
            return false;
        }
    }

    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        const auto* source = findProjectModulator(state, binding.sourceId);
        if (!valid(binding.id) || source == nullptr ||
            !modulationDestinationValid(binding.destination) ||
            binding.amountQ15 == std::numeric_limits<int16_t>::min() ||
            static_cast<uint8_t>(binding.application) >
                static_cast<uint8_t>(ModulationApplication::FROM_BASE) ||
            binding.transfer != ModulationTransfer::LINEAR ||
            (binding.flags & ~BINDING_FLAGS) != 0U ||
            binding.reserved != 0U ||
            (state.nextBindingId != 0 &&
             binding.id.value >= state.nextBindingId)) {
            return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            const auto& other = state.outputBindings[prior];
            if (other.id == binding.id ||
                (other.sourceId == binding.sourceId &&
                 other.destination == binding.destination)) {
                return false;
            }
        }
    }

    for (uint16_t index = 0; index < state.triggerBindingCount; ++index) {
        const auto& trigger = state.triggerBindings[index];
        if (!valid(trigger.id) ||
            findProjectModulator(state, trigger.sourceId) == nullptr ||
            !validTriggerRef(trigger.trigger) ||
            (trigger.flags & ~TRIGGER_FLAGS) != 0U ||
            !allZero(trigger.reserved) ||
            (state.nextBindingId != 0 &&
             trigger.id.value >= state.nextBindingId)) {
            return false;
        }
        for (uint16_t prior = 0; prior < state.outputBindingCount; ++prior) {
            if (state.outputBindings[prior].id == trigger.id) return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            if (state.triggerBindings[prior].id == trigger.id ||
                state.triggerBindings[prior].sourceId == trigger.sourceId) {
                return false;
            }
        }
    }

    uint32_t coveredPointCount = 0;
    for (uint16_t index = 0; index < arena.recordCount; ++index) {
        const auto& curve = arena.records[index];
        if (!valid(curve.id) || curve.referenceCount == 0 ||
            curve.pointCount == 0 || curve.flags != 0U ||
            static_cast<uint32_t>(curve.pointOffset) + curve.pointCount >
                arena.pointCount ||
            (arena.nextCurveId != 0 && curve.id.value >= arena.nextCurveId)) {
            return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            const auto& other = arena.records[prior];
            if (other.id == curve.id) return false;
            const uint32_t begin = curve.pointOffset;
            const uint32_t end = begin + curve.pointCount;
            const uint32_t otherBegin = other.pointOffset;
            const uint32_t otherEnd = otherBegin + other.pointCount;
            if (begin < otherEnd && otherBegin < end) return false;
        }
        ProjectCurveSpec spec{};
        spec.sourceDurationTicks = curve.sourceDurationTicks;
        spec.durationTicks = curve.durationTicks;
        spec.windowOffsetTicks = curve.windowOffsetTicks;
        spec.interpolation = curve.interpolation;
        spec.valueDomain = curve.valueDomain;
        spec.origin = curve.origin;
        if (!validProjectCurveSpec(
                spec,
                arena.points.data() + curve.pointOffset,
                curve.pointCount
            )) {
            return false;
        }
        uint16_t references = 0;
        bool referencedByRecordedShape = false;
        bool referencedByAutomation = false;
        for (uint16_t source = 0; source < state.sourceCount; ++source) {
            if (state.sources[source].kind == ModulatorKind::RECORDED_SHAPE &&
                state.sources[source].parameters.recordedCurveId == curve.id) {
                ++references;
                referencedByRecordedShape = true;
            }
        }
        if (automation != nullptr) {
            for (uint16_t entry = 0; entry < automation->entryCount; ++entry) {
                if (automation->entries[entry].curveId == curve.id) {
                    if (curve.valueDomain != ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
                        curve.origin != ProjectCurveOrigin::NATIVE) {
                        return false;
                    }
                    ++references;
                    referencedByAutomation = true;
                }
            }
        }
        // The two chunks own disjoint curve directories. Sharing across them
        // would duplicate identity during decode even if values remained equal.
        if (referencedByRecordedShape && referencedByAutomation) return false;
        if (references != curve.referenceCount) return false;
        coveredPointCount += curve.pointCount;
    }
    if (coveredPointCount != arena.pointCount) return false;
    if (automation != nullptr) {
        for (uint16_t entry = 0; entry < automation->entryCount; ++entry) {
            if (curveIndex(arena, automation->entries[entry].curveId) < 0) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace core::state::modulation

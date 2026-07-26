#include "state/modulation/ProjectModulationDomainOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOpsInternal.hpp"

namespace core::state::modulation {

using namespace project_modulation_detail;
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
    copyName(source.name, draft.name, "DAHDSR");
    source.kind = ModulatorKind::ADSR;
    source.flags = draft.enabled ? PROJECT_MODULATOR_FLAG_ENABLED : 0U;
    source.accent = draft.accent;
    source.schemaVersion = PROJECT_MODULATOR_ADSR_SCHEMA_VERSION;
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

}  // namespace core::state::modulation

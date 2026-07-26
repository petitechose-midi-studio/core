#include "state/modulation/ProjectModulationDomainOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOpsInternal.hpp"

namespace core::state::modulation {

using namespace project_modulation_detail;
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
    if (!validTriggerFilter(draft.trigger) ||
        !validVelocityRange(draft.velocityMin, draft.velocityMax)) {
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
    binding.velocityMin = draft.velocityMin;
    binding.velocityMax = draft.velocityMax;
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
    const ModulationTriggerFilter& trigger,
    bool enabled,
    uint8_t velocityMin,
    uint8_t velocityMax
) {
    if (!validTriggerFilter(trigger) ||
        !validVelocityRange(velocityMin, velocityMax)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT, sourceId);
    }
    auto* binding = findProjectModulationTriggerForSource(state, sourceId);
    if (binding == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID, sourceId);
    }
    const uint8_t flags = enabled
        ? PROJECT_MODULATION_TRIGGER_FLAG_ENABLED
        : 0U;
    if (binding->trigger == trigger &&
        binding->velocityMin == velocityMin &&
        binding->velocityMax == velocityMax &&
        binding->flags == flags) {
        return result(
            ProjectModulationStatus::NO_CHANGE,
            sourceId,
            binding->id
        );
    }
    binding->trigger = trigger;
    binding->velocityMin = velocityMin;
    binding->velocityMax = velocityMax;
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

}  // namespace core::state::modulation

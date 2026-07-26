#include "state/macro/MacroHistoryInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

using namespace history_detail;

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createRecordedShape_(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress* address,
    const core::state::modulation::RecordedShapeDraft& sourceDraft,
    const core::state::modulation::ModulationBindingDraft* bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    const bool assigned = address != nullptr && bindingDraft != nullptr;
    if ((address == nullptr) != (bindingDraft == nullptr) ||
        (!assigned && (createMacroSlot || destinationPlan != nullptr)) ||
        (assigned &&
         (!macroAutomationAddressValid(*address) ||
          bindingDraft->destination != projectControlDestination(*address) ||
          (destinationPlan != nullptr &&
           (!destinationPlan->valid ||
            !sameAddress(destinationPlan->address, *address))) ||
          (createMacroSlot && destinationPlan != nullptr))) ||
        !validProjectCurveSpec(
            sourceDraft.curve,
            sourceDraft.points,
            sourceDraft.pointCount
        ) || sourceDraft.pointCount >
            RECORDED_SHAPE_HISTORY_POINT_CAPACITY ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        if (sourceDraft.pointCount >
            RECORDED_SHAPE_HISTORY_POINT_CAPACITY) {
            failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        }
        return failure;
    }

    auto& domain = pages.control.authored;
    auto& graph = domain.modulation;
    auto& arena = domain.curves;
    if (!historyDomainValid(domain)) {
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        failure.status = ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED;
        return failure;
    }
    if (graph.nextSourceId == 0U || arena.nextCurveId == 0U) {
        failure.status = ProjectModulationStatus::ID_EXHAUSTED;
        return failure;
    }
    if (assigned &&
        graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        failure.status = ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        return failure;
    }
    if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
        failure.status = ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED;
        return failure;
    }
    if (sourceDraft.pointCount >
        PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
        failure.status = ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED;
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = assigned
        ? MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT
        : MacroHistoryActionKind::CREATE_PROJECT_MODULATOR;
    if (assigned) change->address = *address;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    if (assigned) {
        payload.beforeBindingTail =
            graph.outputBindings[graph.outputBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = assigned;
    payload.macroCreated = assigned && createMacroSlot;
    if (!prepareDestinationStructure(pages, destinationPlan, payload)) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }

    payload.recordedShape = core::app::makeExtmemUnique<
        ProjectRecordedShapeCreationHistoryPayload
    >();
    if (!payload.recordedShape) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    auto& recorded = *payload.recordedShape;
    recorded.points = core::app::makeExtmemUniqueArrayForOverwrite<
        ProjectPackedCurvePoint
    >(sourceDraft.pointCount);
    recorded.beforePointTail = core::app::makeExtmemUniqueArrayForOverwrite<
        ProjectPackedCurvePoint
    >(sourceDraft.pointCount);
    if (!recorded.points || !recorded.beforePointTail) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    recorded.beforeNextCurveId = arena.nextCurveId;
    recorded.beforeRecordCount = arena.recordCount;
    recorded.beforePointCount = arena.pointCount;
    recorded.pointCount = sourceDraft.pointCount;
    recorded.beforeRecordTail = arena.records[arena.recordCount];
    const auto curveHash = unrelatedCurveHash(
        domain,
        ProjectCurveId{arena.nextCurveId}
    );
    if (!curveHash.valid) {
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }
    recorded.unrelatedCurveHash = curveHash.value;
    recorded.unrelatedCurveHashValid = true;
    std::memcpy(
        recorded.points.get(),
        sourceDraft.points,
        static_cast<size_t>(sourceDraft.pointCount) *
            sizeof(ProjectPackedCurvePoint)
    );
    std::memcpy(
        recorded.beforePointTail.get(),
        arena.points.data() + arena.pointCount,
        static_cast<size_t>(sourceDraft.pointCount) *
            sizeof(ProjectPackedCurvePoint)
    );
    recorded.curve = {};
    recorded.curve.id = ProjectCurveId{recorded.beforeNextCurveId};
    recorded.curve.pointOffset = recorded.beforePointCount;
    recorded.curve.pointCount = recorded.pointCount;
    recorded.curve.sourceDurationTicks = sourceDraft.curve.sourceDurationTicks;
    recorded.curve.durationTicks = sourceDraft.curve.durationTicks;
    recorded.curve.windowOffsetTicks = sourceDraft.curve.windowOffsetTicks;
    recorded.curve.referenceCount = 1U;
    recorded.curve.interpolation = sourceDraft.curve.interpolation;
    recorded.curve.valueDomain = sourceDraft.curve.valueDomain;
    recorded.curve.origin = sourceDraft.curve.origin;
    recorded.afterNextCurveId = historyNextStableId(
        recorded.beforeNextCurveId
    );
    recorded.valid = true;
    if (!recordedCreationStorageValid(recorded)) {
        recorded.valid = false;
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }

    if (assigned && !applyMacroCreation(pages, *address, payload)) {
        return failure;
    }
    auto stableDraft = sourceDraft;
    stableDraft.points = recorded.points.get();
    const auto created = createRecordedShapeModulator(
        graph,
        arena,
        stableDraft
    );
    if (!created.changed()) {
        if (assigned) {
            restoreMacroCreationState(pages, *address, payload, false);
        }
        return created;
    }

    ProjectModulationResult bound{};
    bound.status = ProjectModulationStatus::NO_CHANGE;
    if (assigned) {
        auto binding = *bindingDraft;
        binding.sourceId = created.sourceId;
        bound = addProjectModulationBinding(graph, binding);
        if (!bound.changed()) {
            restoreCreationBefore(pages, *address, payload, true);
            return bound;
        }
        if (!applyDestinationStructure(pages, payload)) {
            restoreCreationBefore(pages, *address, payload, true);
            failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
            return failure;
        }
        payload.binding = graph.outputBindings[payload.beforeBindingCount];
    }
    payload.source = graph.sources[payload.beforeSourceCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;

    pages.control.markAuthoredMutation();
    endCoalescing();
    recordNewEntry_(std::move(change));
    return {
        .status = ProjectModulationStatus::OK,
        .sourceId = created.sourceId,
        .bindingId = assigned ? bound.bindingId : ModulationBindingId{},
        .curveId = created.curveId,
    };
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedRecordedShape(
    MacroPagesState& pages,
    const core::state::modulation::RecordedShapeDraft& sourceDraft
) {
    return createRecordedShape_(
        pages,
        nullptr,
        sourceDraft,
        nullptr,
        false,
        nullptr
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createAssignedRecordedShape(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::RecordedShapeDraft& sourceDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    return createRecordedShape_(
        pages,
        &address,
        sourceDraft,
        &bindingDraft,
        createMacroSlot,
        destinationPlan
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::duplicateProjectModulator(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const char* cloneName
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (cloneName == nullptr || cloneName[0] == '\0' ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return failure;
    }
    auto& graph = pages.control.authored.modulation;
    auto& arena = pages.control.authored.curves;
    const auto* source = findProjectModulator(graph, sourceId);
    if (!source) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    const auto* sourceTrigger = findProjectModulationTriggerForSource(
        graph,
        sourceId
    );
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        (sourceTrigger != nullptr &&
         graph.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY)) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED;
        return failure;
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::CREATE_PROJECT_MODULATOR;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    if (sourceTrigger != nullptr) {
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = false;
    payload.triggerCreated = sourceTrigger != nullptr;
    if (source->kind == ModulatorKind::RECORDED_SHAPE) {
        const auto* record = findProjectCurve(
            arena,
            source->parameters.recordedCurveId
        );
        if (!record) {
            failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
            return failure;
        }
        payload.sharedCurveReferenceCreated = true;
        payload.sharedCurveId = record->id;
        payload.beforeSharedCurveReferenceCount = record->referenceCount;
    }

    const auto duplicated = core::state::modulation::duplicateProjectModulator(
        graph,
        arena,
        sourceId,
        cloneName
    );
    if (!duplicated.changed()) return duplicated;
    payload.source = graph.sources[payload.beforeSourceCount];
    if (payload.triggerCreated) {
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    endCoalescing();
    recordNewEntry_(std::move(change));
    return duplicated;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginExistingModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (!macroAutomationAddressValid(address) || !valid(sourceId) ||
        bindingDraft.destination != projectControlDestination(address) ||
        (destinationPlan != nullptr &&
         (!destinationPlan->valid || destinationPlan->address.track != address.track ||
          destinationPlan->address.page != address.page ||
          destinationPlan->address.macro != address.macro)) ||
        (createMacroSlot && destinationPlan != nullptr) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return failure;
    }

    auto& graph = pages.control.authored.modulation;
    const auto* source = findProjectModulator(graph, sourceId);
    if (source == nullptr) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    if (graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        failure.status = ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT;
    change->address = address;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSource = *source;
    payload.source = *source;
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    payload.sourceCreated = false;
    payload.bindingCreated = true;
    payload.macroCreated = createMacroSlot;
    payload.pending = true;
    if (!prepareDestinationStructure(pages, destinationPlan, payload)) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    MacroHistoryChange* reserved = change.get();
    if (!parkPending_(std::move(change))) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }

    if (!applyMacroCreation(pages, address, payload)) {
        (void)takePending_();
        return failure;
    }

    auto binding = bindingDraft;
    binding.sourceId = sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        restoreCreationBefore(pages, address, payload, true);
        (void)takePending_();
        return bound;
    }

    auto& committedPayload = reserved->modulator;
    committedPayload.binding =
        graph.outputBindings[committedPayload.beforeBindingCount];
    committedPayload.afterNextSourceId = graph.nextSourceId;
    committedPayload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    committedPayload.generation = auditionGeneration(
        pages.control.authoredRevision,
        sourceId,
        bound.bindingId
    );
    pages.control.audition = {
        .sourceId = sourceId,
        .bindingId = bound.bindingId,
        .destination = binding.destination,
        .generation = committedPayload.generation,
        .mode = ProjectModulatorSourceSessionMode::AUDITION_EXISTING,
    };
    return {
        .status = ProjectModulationStatus::OK,
        .sourceId = sourceId,
        .bindingId = bound.bindingId,
    };
}

FLASHMEM bool MacroHistoryService::cancelModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    auto* slot = pendingModulatorSlot_();
    if (slot == nullptr || !*slot) return false;
    auto& change = **slot;
    auto& payload = change.modulator;
    const auto& audition = pages.control.audition;
    if (!payload.pending || !sameAddress(change.address, address) ||
        !audition.active() || audition.generation != payload.generation ||
        audition.sourceId != payload.source.id ||
        audition.bindingId != payload.binding.id ||
        !creationIdentityMatches(pages, address, payload, false)) {
        return false;
    }
    restoreCreationBefore(pages, address, payload, true);
    pages.control.audition = {};
    (void)takePending_();
    return true;
}

FLASHMEM bool MacroHistoryService::commitModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    auto* slot = pendingModulatorSlot_();
    if (slot == nullptr || !*slot) return false;
    auto& change = **slot;
    auto& payload = change.modulator;
    const auto& audition = pages.control.audition;
    if (!payload.pending || !sameAddress(change.address, address) ||
        !audition.active() || audition.generation != payload.generation ||
        !creationIdentityMatches(pages, address, payload, false)) {
        return false;
    }
    if (!applyDestinationStructure(pages, payload)) return false;
    const auto& graph = pages.control.authored.modulation;
    const auto* source = core::state::modulation::findProjectModulator(
        graph,
        audition.sourceId
    );
    if (source == nullptr) return false;
    payload.source = *source;
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    if (payload.triggerCreated) {
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    auto committed = takePending_();
    if (!committed) return false;
    committed->modulator.pending = false;
    pages.control.audition = {};
    endCoalescing();
    recordNewEntry_(std::move(committed));
    return true;
}

FLASHMEM bool MacroHistoryService::modulatorAuditionPending(
    const MacroAutomationSlotAddress& address
) const {
    const auto* slot = pendingModulatorSlot_();
    return slot != nullptr && *slot && (*slot)->modulator.pending &&
           sameAddress((*slot)->address, address);
}

FLASHMEM bool MacroHistoryService::hasPendingModulatorAuditionTransaction(
    const MacroPagesState& pages
) const {
    return pendingModulatorSlot_() != nullptr ||
           pages.control.audition.mode !=
               core::state::modulation::ProjectModulatorSourceSessionMode::
                   DURABLE_PROJECT;
}

FLASHMEM bool MacroHistoryService::abortPendingModulatorAudition(
    MacroPagesState& pages
) {
    auto* slot = pendingModulatorSlot_();
    if (!pages.control.audition.active()) {
        return slot == nullptr &&
               pages.control.audition.mode ==
                   core::state::modulation::ProjectModulatorSourceSessionMode::
                       DURABLE_PROJECT;
    }
    if (slot == nullptr || !*slot || !(*slot)->modulator.pending) return false;
    return cancelModulatorAudition(pages, (*slot)->address);
}

}  // namespace core::state::macro

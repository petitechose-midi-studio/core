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

FLASHMEM MacroHistoryChangePtr
MacroHistoryService::prepareModulationAssignments_(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroHistoryActionKind kind
) const {
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return {};
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->modulationAssignments = core::app::makeExtmemUnique<
        MacroModulationAssignmentsHistoryPayload
    >();
    if (!change->modulationAssignments) return {};
    change->kind = kind;
    change->address = address;
    if (!captureModulationAssignments(
            pages,
            address,
            change->modulationAssignments->before
        )) {
        return {};
    }
    return change;
}

FLASHMEM bool MacroHistoryService::commitModulationAssignments_(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (!change || !change->modulationAssignments) return false;
    auto& payload = *change->modulationAssignments;
    if (!captureModulationAssignments(pages, change->address, payload.after)) {
        (void)applyModulationAssignments(pages, payload.before);
        return false;
    }
    if (sameModulationAssignments(payload.before, payload.after)) return false;

    if (coalesce && coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == change->kind &&
        sameAddress(coalesced_address_, change->address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->modulationAssignments &&
            sameModulationAssignments(
                previous->modulationAssignments->after,
                payload.before
            )) {
            previous->modulationAssignments->after = payload.after;
            clearRedo_();
            return true;
        }
    }

    recordNewEntry_(std::move(change));
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = undo_[undo_count_ - 1U]->kind;
        coalesced_address_ = undo_[undo_count_ - 1U]->address;
    }
    return true;
}

FLASHMEM bool MacroHistoryService::commitProjectSourceEdit_(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (!change || !change->sourceEdit.valid ||
        change->kind != MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT ||
        change->sourceEdit.before.id != change->sourceEdit.after.id) {
        return false;
    }
    const auto* live = core::state::modulation::findProjectModulator(
        pages.control.authored.modulation,
        change->sourceEdit.after.id
    );
    if (live == nullptr ||
        !sameObjectBits(*live, change->sourceEdit.after) ||
        sameObjectBits(
            change->sourceEdit.before,
            change->sourceEdit.after
        )) {
        return false;
    }

    if (coalesce && coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == change->kind) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->sourceEdit.valid &&
            previous->sourceEdit.after.id == change->sourceEdit.before.id &&
            sameObjectBits(
                previous->sourceEdit.after,
                change->sourceEdit.before
            )) {
            previous->sourceEdit.after = change->sourceEdit.after;
            clearRedo_();
            return true;
        }
    }

    recordNewEntry_(std::move(change));
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    }
    return true;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginNewModulatorAudition_(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorLfoDraft* lfoDraft,
    const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
    const core::state::modulation::ModulationTriggerDraft* triggerDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if ((lfoDraft == nullptr) == (adsrDraft == nullptr) ||
        !macroAutomationAddressValid(address) ||
        bindingDraft.destination != projectControlDestination(address) ||
        (destinationPlan != nullptr &&
         (!destinationPlan->valid || destinationPlan->address.track != address.track ||
          destinationPlan->address.page != address.page ||
          destinationPlan->address.macro != address.macro)) ||
        (createMacroSlot && destinationPlan != nullptr) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
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
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY ||
        (triggerDraft != nullptr &&
         graph.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY)) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY
                ? ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED
                : ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED;
        return failure;
    }
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    if (triggerDraft != nullptr) {
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = true;
    payload.triggerCreated = triggerDraft != nullptr;
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

    const auto created = lfoDraft != nullptr
        ? createLfoModulator(graph, *lfoDraft)
        : createAdsrModulator(graph, *adsrDraft);
    if (!created.changed()) {
        restoreMacroCreationState(pages, address, payload, false);
        (void)takePending_();
        return created;
    }
    if (triggerDraft != nullptr) {
        auto trigger = *triggerDraft;
        trigger.sourceId = created.sourceId;
        const auto triggered = addProjectModulationTrigger(graph, trigger);
        if (!triggered.changed()) {
            restoreCreationBefore(pages, address, payload, true);
            (void)takePending_();
            return triggered;
        }
    }
    auto binding = bindingDraft;
    binding.sourceId = created.sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        restoreCreationBefore(pages, address, payload, true);
        (void)takePending_();
        return bound;
    }

    payload.source = graph.sources[payload.beforeSourceCount];
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    if (payload.triggerCreated) {
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    payload.generation = auditionGeneration(
        pages.control.authoredRevision,
        created.sourceId,
        bound.bindingId
    );
    pages.control.audition = {
        .sourceId = created.sourceId,
        .bindingId = bound.bindingId,
        .destination = binding.destination,
        .generation = payload.generation,
        .mode = ProjectModulatorSourceSessionMode::AUDITION_NEW,
    };
    (void)reserved;
    return {
        .status = ProjectModulationStatus::OK,
        .sourceId = created.sourceId,
        .bindingId = bound.bindingId,
    };
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginLfoModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    return beginNewModulatorAudition_(
        pages,
        address,
        &sourceDraft,
        nullptr,
        nullptr,
        bindingDraft,
        createMacroSlot,
        destinationPlan
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginAdsrModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
    const core::state::modulation::ModulationTriggerDraft& triggerDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    return beginNewModulatorAudition_(
        pages,
        address,
        nullptr,
        &sourceDraft,
        &triggerDraft,
        bindingDraft,
        createMacroSlot,
        destinationPlan
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedModulator_(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorLfoDraft* lfoDraft,
    const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
    const core::state::modulation::ModulationTriggerDraft* triggerDraft
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if ((lfoDraft == nullptr) == (adsrDraft == nullptr)) return failure;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return failure;
    }
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        (triggerDraft != nullptr &&
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
    if (triggerDraft != nullptr) {
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = false;
    payload.triggerCreated = triggerDraft != nullptr;

    const auto created = lfoDraft != nullptr
        ? createLfoModulator(graph, *lfoDraft)
        : createAdsrModulator(graph, *adsrDraft);
    if (!created.changed()) return created;
    if (triggerDraft != nullptr) {
        auto trigger = *triggerDraft;
        trigger.sourceId = created.sourceId;
        const auto triggered = addProjectModulationTrigger(graph, trigger);
        if (!triggered.changed()) {
            restoreCreationBefore(pages, {}, payload, true);
            return triggered;
        }
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
    payload.source = graph.sources[payload.beforeSourceCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    endCoalescing();
    recordNewEntry_(std::move(change));
    return created;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedLfo(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft
) {
    return createUnassignedModulator_(
        pages,
        &sourceDraft,
        nullptr,
        nullptr
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedAdsr(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
    const core::state::modulation::ModulationTriggerDraft& triggerDraft
) {
    return createUnassignedModulator_(
        pages,
        nullptr,
        &sourceDraft,
        &triggerDraft
    );
}

}  // namespace core::state::macro

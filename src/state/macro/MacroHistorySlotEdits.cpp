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

FLASHMEM bool MacroHistoryService::removeMacroSlot(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr ||
        !macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(
            address.macro
        )) {
        return false;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->slotRemoval =
        core::app::makeExtmemUnique<MacroSlotRemovalHistoryPayload>();
    if (!change->slotRemoval) return false;
    change->kind = MacroHistoryActionKind::REMOVE_SLOT;
    change->address = address;
    auto& payload = *change->slotRemoval;
    if (!captureMacroSlotRemovalState(pages, address, payload.before)) {
        return false;
    }

    payload.after.automation.address = address;
    payload.after.modulation = payload.before.modulation;
    payload.after.modulation.globalBindingCount = static_cast<uint16_t>(
        payload.before.modulation.globalBindingCount -
        payload.before.modulation.assignmentCount
    );
    payload.after.modulation.assignmentCount = 0U;
    payload.after.modulation.destinationScaleQ15 =
        PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    payload.after.modulation.assignments = {};
    payload.after.macroActive = false;
    payload.after.cc = defaultMacroCc(address.page, address.macro);
    payload.after.staticValue = 0.5f;

    if (!liveMacroSlotRemovalStateMatches(pages, address, payload.before) ||
        !applyMacroSlotRemovalState(pages, address, payload.after) ||
        !liveMacroSlotRemovalStateMatches(pages, address, payload.after)) {
        if (!liveMacroSlotRemovalStateMatches(pages, address, payload.before)) {
            (void)applyMacroSlotRemovalState(pages, address, payload.before);
        }
        return false;
    }

    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

FLASHMEM bool MacroHistoryService::pasteModulationBinding(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulationBindingDraft& draft,
    bool overwriteExisting,
    core::state::modulation::ModulationBindingId* appliedBinding
) {
    using namespace core::state::modulation;
    const auto destination = projectControlDestination(address);
    if (!macroAutomationAddressValid(address) ||
        draft.destination != destination || !valid(draft.sourceId)) {
        return false;
    }

    auto& graph = pages.control.authored.modulation;
    ModulationBindingState* existing = nullptr;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        auto& candidate = graph.outputBindings[index];
        if (candidate.sourceId == draft.sourceId &&
            candidate.destination == destination) {
            existing = &candidate;
            break;
        }
    }
    if ((existing != nullptr) != overwriteExisting) return false;

    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::PASTE_MODULATION
    );
    if (!change) return false;

    ProjectModulationResult mutation{};
    if (existing != nullptr) {
        mutation = updateProjectModulationBinding(
            graph,
            existing->id,
            draft.amountQ15,
            draft.application,
            draft.transfer,
            draft.enabled,
            draft.slewMs
        );
    } else {
        mutation = addProjectModulationBinding(graph, draft);
    }
    if (!mutation.changed()) return false;
    pages.control.markAuthoredMutation();
    if (!commitModulationAssignments_(pages, std::move(change))) return false;
    if (appliedBinding != nullptr) *appliedBinding = mutation.bindingId;
    return true;
}

FLASHMEM bool MacroHistoryService::setProjectModulatorEnabled(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    bool enabled
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectModulatorEnabled(
        pages.control.authored.modulation,
        sourceId,
        enabled
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    endCoalescing();
    return commitProjectSourceEdit_(pages, std::move(change), false);
}

FLASHMEM bool MacroHistoryService::setProjectModulatorName(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const char* name
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectModulatorName(
        pages.control.authored.modulation,
        sourceId,
        name
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    endCoalescing();
    return commitProjectSourceEdit_(pages, std::move(change), false);
}

FLASHMEM bool MacroHistoryService::setProjectLfoParametersCoalesced(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulatorLfoParameters& parameters
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source || source->kind != ModulatorKind::LFO) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectLfoParameters(
        pages.control.authored.modulation,
        sourceId,
        parameters
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    return commitProjectSourceEdit_(pages, std::move(change), true);
}

FLASHMEM bool MacroHistoryService::setProjectAdsrParametersCoalesced(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulatorAdsrParameters& parameters
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source || source->kind != ModulatorKind::ADSR) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectAdsrParameters(
        pages.control.authored.modulation,
        sourceId,
        parameters
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    return commitProjectSourceEdit_(pages, std::move(change), true);
}

}  // namespace core::state::macro

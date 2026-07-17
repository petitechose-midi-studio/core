#include "handler/common/ModulatorNavigationWorkflow.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectMenuModel.hpp"

namespace core::handler::modulator_navigation {

namespace {

using core::state::macro::MacroAutomationSlotAddress;
using core::state::modulation::ModulationBindingId;
using core::state::modulation::ModulationBindingState;
using core::state::modulation::ModulationDestination;
using core::state::modulation::ModulatorId;

constexpr bool sameAddress(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
) {
    return lhs.track == rhs.track && lhs.page == rhs.page &&
           lhs.macro == rhs.macro;
}

struct DestinationAssignments {
    uint16_t count = 0;
    int selectedOrdinal = -1;
    ModulationBindingId firstBinding{};
};

FLASHMEM DestinationAssignments inspectDestination(
    const core::state::modulation::ProjectModulationState& graph,
    const ModulationDestination& destination,
    ModulationBindingId selected
) {
    DestinationAssignments result{};
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (result.count == 0U) result.firstBinding = binding.id;
        if (binding.id == selected) {
            result.selectedOrdinal = static_cast<int>(result.count);
        }
        ++result.count;
    }
    return result;
}

constexpr uint8_t rowForOrdinal(uint16_t count, int ordinal) {
    const int first = count > 1U ? 1 : 0;
    return static_cast<uint8_t>(first + std::max(ordinal, 0));
}

FLASHMEM void restoreMacroOverlayStack(StateRefs state) {
    state.activeView.set(core::ui::ViewType::MACRO);
    // Restore only the visible child. Rebuilding parent + child in the same
    // notification wave can expose the transparent parent for one frame. The
    // parent state is prepared before this call, but remains untracked and
    // parked until Back materializes it.
    state.overlays.show(core::ui::OverlayType::MACRO_AUTOMATION, false);
}

}  // namespace

FLASHMEM bool openSourceFromMacro(
    StateRefs state,
    uint8_t macroIndex,
    ModulationBindingId bindingId,
    uint8_t focusedRow
) {
    using namespace core::state::modulation;
    if (state.activeView.get() != core::ui::ViewType::MACRO ||
        state.macroEdit.flowPhase.get() !=
            core::state::MacroEditFlowPhase::MODULATION ||
        macroIndex >= core::state::macro::MACRO_COUNT ||
        state.macroEdit.editingIndex.get() != macroIndex ||
        !valid(bindingId)) {
        return false;
    }

    const MacroAutomationSlotAddress address{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = macroIndex,
    };
    const auto destination = projectControlDestination(address);
    const auto& graph = state.pages.control.authored.modulation;
    const auto* binding = findProjectModulationBinding(graph, bindingId);
    if (binding == nullptr || binding->destination != destination) return false;
    const auto* source = findProjectModulator(graph, binding->sourceId);
    if (source == nullptr) return false;

    if (!core::state::project::openProjectModulatorWorkspace(
            state.projectNavigation,
            source->id
        )) {
        return false;
    }
    state.projectNavigation.modulatorReturn = {
        .sourceId = source->id,
        .bindingId = binding->id,
        .macroAddress = address,
        .caller = core::state::project::
            ModulatorNavigationCaller::MACRO_ASSIGNMENT,
        .focusedRow = focusedRow,
    };

    char feedback[32]{};
    std::snprintf(
        feedback,
        sizeof(feedback),
        "Macro %u · Back returns",
        static_cast<unsigned>(macroIndex + 1U)
    );
    state.projectNavigation.setLifecycleFeedback(feedback);
    state.macroEdit.setModulatorNavigationFeedback(
        core::state::MacroModulatorNavigationFeedback::NONE,
        0U
    );
    state.overlays.hideAll();
    state.activeView.set(core::ui::ViewType::PROJECT);
    return true;
}

bool macroReturnPending(
    const core::state::project::ProjectNavigationState& navigation
) {
    return navigation.modulatorReturn.active() &&
           navigation.modulatorReturn.caller == core::state::project::
               ModulatorNavigationCaller::MACRO_ASSIGNMENT;
}

bool shouldReturnToMacroOnBack(
    const core::state::project::ProjectNavigationState& navigation
) {
    if (!macroReturnPending(navigation)) return false;
    if (navigation.depth.get() == 0U) return true;
    return navigation.depth.get() == 1U &&
           navigation.currentNode.get() == core::state::project::
               ProjectNodeId::MODULATOR_SOURCE_DETAIL;
}

FLASHMEM bool returnToMacro(StateRefs state, uint32_t nowMs) {
    using namespace core::state::modulation;
    if (!macroReturnPending(state.projectNavigation)) return false;

    const auto returnContext = state.projectNavigation.modulatorReturn;
    state.projectNavigation.modulatorReturn = {};
    state.projectNavigation.clearLifecycleFeedback();

    const MacroAutomationSlotAddress currentAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = std::min<uint8_t>(
            returnContext.macroAddress.macro,
            static_cast<uint8_t>(core::state::macro::MACRO_COUNT - 1U)
        ),
    };
    const bool contextUnchanged = sameAddress(
        currentAddress,
        returnContext.macroAddress
    );
    const auto destination = projectControlDestination(currentAddress);
    auto& graph = state.pages.control.authored.modulation;
    const auto* source = findProjectModulator(graph, returnContext.sourceId);
    const auto* binding = findProjectModulationBinding(
        graph,
        returnContext.bindingId
    );
    const bool exactAssignment = contextUnchanged && source != nullptr &&
        binding != nullptr && binding->sourceId == returnContext.sourceId &&
        binding->destination == destination;

    const auto assignments = inspectDestination(
        graph,
        destination,
        exactAssignment ? returnContext.bindingId : ModulationBindingId{}
    );
    uint8_t focusedRow = 0U;
    if (exactAssignment && assignments.selectedOrdinal >= 0) {
        (void)setProjectControlFocusedModulationBinding(
            state.pages.control,
            currentAddress,
            returnContext.bindingId
        );
        focusedRow = rowForOrdinal(
            assignments.count,
            assignments.selectedOrdinal
        );
    } else if (valid(assignments.firstBinding)) {
        (void)setProjectControlFocusedModulationBinding(
            state.pages.control,
            currentAddress,
            assignments.firstBinding
        );
        focusedRow = rowForOrdinal(assignments.count, 0);
    }

    core::state::MacroModulatorNavigationFeedback feedback =
        core::state::MacroModulatorNavigationFeedback::NONE;
    if (!contextUnchanged) {
        feedback = core::state::MacroModulatorNavigationFeedback::CONTEXT_CHANGED;
    } else if (source == nullptr) {
        feedback = core::state::MacroModulatorNavigationFeedback::SOURCE_UNAVAILABLE;
    } else if (!exactAssignment) {
        feedback = core::state::MacroModulatorNavigationFeedback::
            ASSIGNMENT_UNAVAILABLE;
    }

    // Clear Project presentation before preparing Macro state. hideAll() also
    // reconciles every registered visibility signal, so doing it after
    // openModulation() would leave a visually present but inactive orphan
    // child.
    state.overlays.hideAll();
    state.macroEdit.loadActiveConfig(
        currentAddress.macro,
        state.pages.activeConfigs[currentAddress.macro].channel,
        state.pages.activeConfigs[currentAddress.macro].cc
    );
    state.macroEdit.openModulation(focusedRow);
    state.macroEdit.setModulatorNavigationFeedback(feedback, nowMs);
    restoreMacroOverlayStack(state);
    return true;
}

}  // namespace core::handler::modulator_navigation

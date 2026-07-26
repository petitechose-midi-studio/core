#include "handler/common/ModulatorNavigationWorkflow.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "state/contextual/OperationFeedbackState.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulatorSourceSession.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

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
    // The Macro Modulation screen always exposes its aggregate All row as
    // soon as at least one assignment exists.
    const int first = count > 0U ? 1 : 0;
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

FLASHMEM void publishMacroAuditionFeedback(
    core::state::MacroEditState& macroEdit,
    const core::state::project::ModulatorReturnContext& returnContext,
    bool committed,
    uint32_t nowMs
) {
    namespace contextual = core::state::contextual;
    const contextual::ContextEntityRef modulation{
        .kind = contextual::ContextEntityKind::MODULATION_LANE,
        .track = returnContext.macroAddress.track,
        .page = returnContext.macroAddress.page,
        .item = returnContext.macroAddress.macro,
    };
    auto feedback = macroEdit.contextFeedback.get();
    contextual::setOperationFeedback(
        feedback,
        committed ? contextual::ContextActionId::APPLY
                  : contextual::ContextActionId::CANCEL,
        modulation,
        modulation,
        committed ? contextual::OperationFeedbackStatus::APPLIED
                  : contextual::OperationFeedbackStatus::CANCELLED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        committed ? Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
                  : Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
    );
    macroEdit.contextFeedback.set(feedback);
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
        .target = core::state::project::
            ModulatorMacroReturnTarget::MODULATION_ASSIGNMENT,
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
    state.activeView.set(core::ui::ViewType::MODULATORS);
    return true;
}

FLASHMEM bool openAuditionSourceFromMacro(StateRefs state, uint8_t macroIndex) {
    using namespace core::state::modulation;
    if (state.activeView.get() != core::ui::ViewType::MACRO ||
        macroIndex >= core::state::macro::MACRO_COUNT ||
        state.macroEdit.editingIndex.get() != macroIndex) {
        return false;
    }

    const MacroAutomationSlotAddress address{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = macroIndex,
    };
    const auto& audition = state.pages.control.audition;
    const auto destination = projectControlDestination(address);
    const auto& graph = state.pages.control.authored.modulation;
    const auto session = resolveProjectModulatorSourceSession(
        state.pages.control,
        audition.sourceId
    );
    const auto* source = findProjectModulator(graph, audition.sourceId);
    const auto* binding = findProjectModulationBinding(
        graph,
        audition.bindingId
    );
    const auto phase = state.macroEdit.flowPhase.get();
    const bool newSession = session.mode ==
        ProjectModulatorSourceSessionMode::AUDITION_NEW;
    const bool existingSession = session.mode ==
        ProjectModulatorSourceSessionMode::AUDITION_EXISTING;
    const bool validNewOrigin = newSession &&
        phase == core::state::MacroEditFlowPhase::MODULATOR_CREATE &&
        state.macroEdit.modulationFocusedRow.get() <= 1U &&
        ((state.macroEdit.modulationFocusedRow.get() == 0U &&
          source != nullptr && source->kind == ModulatorKind::LFO) ||
         (state.macroEdit.modulationFocusedRow.get() == 1U &&
          source != nullptr && source->kind == ModulatorKind::ADSR));
    const int pickerIndex = state.macroEdit.modulatorPickerIndex.get();
    const bool validExistingOrigin = existingSession &&
        phase == core::state::MacroEditFlowPhase::MODULATOR_PICKER &&
        pickerIndex >= 0 && pickerIndex < static_cast<int>(graph.sourceCount) &&
        graph.sources[static_cast<uint16_t>(pickerIndex)].id ==
            audition.sourceId;
    if (!session.audition() || (!validNewOrigin && !validExistingOrigin) ||
        audition.destination != destination || source == nullptr ||
        binding == nullptr || binding->sourceId != source->id ||
        binding->destination != destination) {
        return false;
    }
    if (!core::state::project::openProjectModulatorWorkspace(
            state.projectNavigation,
            source->id
        )) {
        return false;
    }
    if (existingSession) {
        const auto layout = core::state::project::modulators::
            sourceAuditionLayout(source->kind);
        for (uint8_t row = 0U; row < layout.count; ++row) {
            if (layout.at(row) != core::state::project::modulators::
                    SourceDetailItem::DEPTH) {
                continue;
            }
            state.projectNavigation.focusedRow.set(row);
            state.projectNavigation.notifyContentChanged();
            break;
        }
    }

    state.projectNavigation.modulatorReturn = {
        .sourceId = source->id,
        .bindingId = binding->id,
        .macroAddress = address,
        .caller = core::state::project::
            ModulatorNavigationCaller::MACRO_AUDITION,
        .target = validNewOrigin
            ? core::state::project::
                  ModulatorMacroReturnTarget::MODULATOR_CREATE
            : core::state::project::
                  ModulatorMacroReturnTarget::MODULATOR_PICKER,
        .focusedRow = static_cast<uint8_t>(
            validNewOrigin
                ? state.macroEdit.modulationFocusedRow.get()
                : 0U
        ),
    };
    state.projectNavigation.setLifecycleFeedback(
        validNewOrigin
            ? "Preview - Apply or Back"
            : "Shared source - Depth preview"
    );
    state.overlays.hideAll();
    state.activeView.set(core::ui::ViewType::MODULATORS);
    return true;
}

bool macroAuditionReturnPending(
    const core::state::project::ProjectNavigationState& navigation
) {
    return navigation.modulatorReturn.active() &&
           navigation.modulatorReturn.caller == core::state::project::
               ModulatorNavigationCaller::MACRO_AUDITION;
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
        core::state::project::projectTrackMidiChannel(
            state.projectTracks,
            currentAddress.track
        ),
        state.pages.activeConfigs[currentAddress.macro].cc
    );
    state.macroEdit.openModulation(focusedRow);
    state.macroEdit.setModulatorNavigationFeedback(feedback, nowMs);
    restoreMacroOverlayStack(state);
    return true;
}

FLASHMEM bool returnToMacroFromAudition(
    StateRefs state,
    bool committed,
    uint32_t nowMs
) {
    if (!macroAuditionReturnPending(state.projectNavigation)) return false;
    const auto returnContext = state.projectNavigation.modulatorReturn;
    if (!committed) {
        state.projectNavigation.modulatorReturn = {};
        state.projectNavigation.clearLifecycleFeedback();

        const MacroAutomationSlotAddress currentAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = std::min<uint8_t>(
                returnContext.macroAddress.macro,
                static_cast<uint8_t>(
                    core::state::macro::MACRO_COUNT - 1U
                )
            ),
        };
        const bool contextUnchanged = sameAddress(
            currentAddress,
            returnContext.macroAddress
        );
        state.overlays.hideAll();
        state.macroEdit.loadActiveConfig(
            currentAddress.macro,
            core::state::project::projectTrackMidiChannel(
                state.projectTracks,
                currentAddress.track
            ),
            state.pages.activeConfigs[currentAddress.macro].cc
        );
        if (contextUnchanged && returnContext.target ==
                core::state::project::
                    ModulatorMacroReturnTarget::MODULATOR_CREATE) {
            state.macroEdit.openModulatorCreate(
                std::min<uint8_t>(returnContext.focusedRow, 1U)
            );
        } else if (contextUnchanged && returnContext.target ==
                       core::state::project::
                           ModulatorMacroReturnTarget::MODULATOR_PICKER) {
            const auto& graph = state.pages.control.authored.modulation;
            int selected = -1;
            for (uint16_t index = 0U; index < graph.sourceCount; ++index) {
                if (graph.sources[index].id == returnContext.sourceId) {
                    selected = static_cast<int>(index);
                    break;
                }
            }
            if (selected >= 0) {
                state.macroEdit.openModulatorPicker(selected);
            } else {
                state.macroEdit.openModulation(0U);
                state.macroEdit.setModulatorNavigationFeedback(
                    core::state::MacroModulatorNavigationFeedback::
                        SOURCE_UNAVAILABLE,
                    nowMs
                );
            }
        } else {
            state.macroEdit.openModulation(0U);
            if (!contextUnchanged) {
                state.macroEdit.setModulatorNavigationFeedback(
                    core::state::MacroModulatorNavigationFeedback::
                        CONTEXT_CHANGED,
                    nowMs
                );
            }
        }
        publishMacroAuditionFeedback(
            state.macroEdit,
            returnContext,
            false,
            nowMs
        );
        restoreMacroOverlayStack(state);
        return true;
    }
    state.projectNavigation.modulatorReturn.caller = core::state::project::
        ModulatorNavigationCaller::MACRO_ASSIGNMENT;
    state.projectNavigation.modulatorReturn.target = core::state::project::
        ModulatorMacroReturnTarget::MODULATION_ASSIGNMENT;
    if (!returnToMacro(state, nowMs)) return false;
    publishMacroAuditionFeedback(
        state.macroEdit,
        returnContext,
        true,
        nowMs
    );
    return true;
}

}  // namespace core::handler::modulator_navigation

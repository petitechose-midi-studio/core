#include "handler/project/ProjectHandlerInternals.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "handler/common/ModulatorNavigationWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ModulationDepthParameterMapping.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

namespace project_handler_internal {

const char MODULATOR_PREVIEW_PENDING_FEEDBACK[] PROGMEM =
    "Preview - Apply or Back";

}  // namespace project_handler_internal

using namespace project_handler_internal;
namespace depth_parameter = core::state::modulation::depth;

namespace {

const char FEEDBACK_SOURCE_FULL[] PROGMEM = "Source full - remove one";
const char FEEDBACK_BINDINGS_FULL[] PROGMEM =
    "Assignments full - remove one";
const char FEEDBACK_ALREADY_ASSIGNED[] PROGMEM = "Already assigned";
const char FEEDBACK_UNDO_FULL[] PROGMEM = "Undo memory full";
const char FEEDBACK_SOURCE_MISSING[] PROGMEM = "Source missing - retry";
const char FEEDBACK_SOURCE_IDS_FULL[] PROGMEM = "Source IDs full";
const char FEEDBACK_STATE_CHANGED[] PROGMEM = "State changed - retry";
const char FEEDBACK_NO_CHANGE[] PROGMEM = "No change";
const char FEEDBACK_ASSIGN_FAILED[] PROGMEM = "Cannot assign - retry";
const char FEEDBACK_CREATE_TRACK_FORMAT[] PROGMEM =
    "Creates T%u · P1 · M%u on Apply";
const char FEEDBACK_CREATE_PAGE_FORMAT[] PROGMEM =
    "Creates P%u · M%u on Apply";
const char FEEDBACK_CREATE_MACRO_FORMAT[] PROGMEM =
    "Creates M%u on Apply";
const char FEEDBACK_DEPTH_PREVIEW_FORMAT[] PROGMEM =
    "Depth %+d%% - Preview";
const char FEEDBACK_APPLY_FAILED[] PROGMEM =
    "State changed - Back to cancel";
const char FEEDBACK_APPLIED_UNDO[] PROGMEM = "Applied - one Undo";
const char FEEDBACK_DESTINATION_APPLIED[] PROGMEM = "Destination applied";
const char FEEDBACK_PREVIEW_CANCELLED[] PROGMEM = "Preview cancelled";

FLASHMEM bool sourceAlreadyTargets(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationDestination& destination
) {
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId == sourceId && binding.destination == destination) {
            return true;
        }
    }
    return false;
}

FLASHMEM const char* destinationAuditionFailureLabel(
    core::state::modulation::ProjectModulationStatus status
) {
    using core::state::modulation::ProjectModulationStatus;
    // Keep this cold formatter branch-based. A dense switch makes GCC emit a
    // pointer table in initialized DTCM; crossing the following alignment
    // boundary then costs a full 1 KiB of scarce RAM1 for a failure-only path.
    const volatile ProjectModulationStatus branchStatus = status;
    if (branchStatus == ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED) {
        return FEEDBACK_SOURCE_FULL;
    }
    if (branchStatus == ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED) {
        return FEEDBACK_BINDINGS_FULL;
    }
    if (branchStatus == ProjectModulationStatus::DUPLICATE_BINDING) {
        return FEEDBACK_ALREADY_ASSIGNED;
    }
    if (branchStatus == ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED) {
        return FEEDBACK_UNDO_FULL;
    }
    if (branchStatus == ProjectModulationStatus::INVALID_ID) {
        return FEEDBACK_SOURCE_MISSING;
    }
    if (branchStatus == ProjectModulationStatus::ID_EXHAUSTED) {
        return FEEDBACK_SOURCE_IDS_FULL;
    }
    if (branchStatus == ProjectModulationStatus::INVALID_ARGUMENT ||
        branchStatus == ProjectModulationStatus::INVARIANT_VIOLATION) {
        return FEEDBACK_STATE_CHANGED;
    }
    if (branchStatus == ProjectModulationStatus::NO_CHANGE) {
        return FEEDBACK_NO_CHANGE;
    }
    return FEEDBACK_ASSIGN_FAILED;
}

}  // namespace

FLASHMEM void ProjectHandler::startDestinationPickerAudition() {
    using namespace core::state::modulation;
    using RowKind = core::state::project::modulators::DestinationPickerRowKind;
    if (navigation_.currentNode.get() !=
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return;
    }
    core::state::macro::MacroAutomationSlotAddress pendingAddress{};
    if (modulatorAuditionAddress(pendingAddress)) {
        navigation_.setLifecycleFeedback(
            MODULATOR_PREVIEW_PENDING_FEEDBACK
        );
        return;
    }
    const bool creating = navigation_.creatingModulatorSource;
    const auto pickerTarget = core::state::project::modulators::
        destinationPickerTargetAtRow(
            pages_,
            navigation_,
            navigation_.focusedRow.get()
        );
    if (!pickerTarget.valid) return;
    const uint8_t track = navigation_.destinationPickerTrack;
    const uint8_t page = navigation_.destinationPickerPage;
    auto& graph = pages_.control.authored.modulation;

    const ModulatorKind creatingKind = navigation_.creatingModulatorKind;
    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectModulatorName(graph, creatingKind, name, sizeof(name));
    ModulatorLfoDraft sourceDraft{};
    sourceDraft.name = name;
    sourceDraft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    sourceDraft.parameters.shape = ModulatorLfoShape::SINE;
    sourceDraft.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    sourceDraft.parameters.timing = ModulatorTimingMode::SYNC;
    ModulatorAdsrDraft adsrDraft{};
    adsrDraft.name = name;
    ModulationTriggerDraft triggerDraft{};
    triggerDraft.trigger = {
        .kind = ModulationTriggerKind::TRACK_NOTE,
        .track = track,
        .noteMin = 0U,
        .noteMax = 127U,
    };

    if (creating && pickerTarget.kind == RowKind::KEEP_UNASSIGNED) {
        ProjectModulationResult created{};
        if (creatingKind == ModulatorKind::ADSR) {
            created = macro_history_.createUnassignedAdsr(
                pages_, adsrDraft, triggerDraft
            );
        } else if (creatingKind == ModulatorKind::LFO) {
            created = macro_history_.createUnassignedLfo(pages_, sourceDraft);
        } else {
            navigation_.setLifecycleFeedback("Invalid source workflow");
            return;
        }
        if (!created.changed()) {
            navigation_.setLifecycleFeedback(
                destinationAuditionFailureLabel(created.status)
            );
            return;
        }
        while (navigation_.depth.get() > 0U) {
            (void)core::state::project::backProjectNavigation(navigation_);
        }
        for (uint16_t index = 0; index < graph.sourceCount; ++index) {
            if (graph.sources[index].id == created.sourceId) {
                navigation_.focusedRow.set(static_cast<uint8_t>(index));
                break;
            }
        }
        navigation_.selectedModulator = created.sourceId;
        publishModulatorMutation(false);
        navigation_.setLifecycleFeedback(
            creatingKind == ModulatorKind::ADSR
                ? "DAHDSR created · Unassigned"
                : "LFO created · Unassigned"
        );
        return;
    }
    if (pickerTarget.kind != RowKind::MACRO) return;
    const uint8_t row = pickerTarget.index;

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        track,
        page,
        row,
    };
    const auto topology = core::state::macro::MacroWorkflow::
        planDestinationActivation(pages_, address);
    if (!topology.valid) {
        navigation_.setLifecycleFeedback(FEEDBACK_STATE_CHANGED);
        return;
    }
    const auto destination = projectControlDestination(address);
    ModulationBindingDraft binding{};
    binding.destination = destination;
    binding.amountQ15 = 8192;
    binding.application = ModulationApplication::NATURAL;

    ProjectModulationResult begun{};
    if (creating) {
        if (creatingKind == ModulatorKind::ADSR) {
            begun = macro_history_.beginAdsrModulatorAudition(
                pages_,
                address,
                adsrDraft,
                triggerDraft,
                binding,
                false,
                &topology
            );
        } else if (creatingKind == ModulatorKind::LFO) {
            begun = macro_history_.beginLfoModulatorAudition(
                pages_, address, sourceDraft, binding, false, &topology
            );
        } else {
            navigation_.setLifecycleFeedback("Invalid source workflow");
            return;
        }
    } else {
        const ModulatorId targetSource = navigation_.selectedModulator;
        const auto* source = findProjectModulator(graph, targetSource);
        if (!source) {
            navigation_.setLifecycleFeedback("Source unavailable");
            return;
        }
        if (sourceAlreadyTargets(graph, targetSource, destination)) {
            navigation_.setLifecycleFeedback(FEEDBACK_ALREADY_ASSIGNED);
            return;
        }
        begun = macro_history_.beginExistingModulatorAudition(
            pages_,
            address,
            targetSource,
            binding,
            false,
            &topology
        );
    }
    if (!begun.changed()) {
        navigation_.setLifecycleFeedback(
            destinationAuditionFailureLabel(begun.status)
        );
        return;
    }
    navigation_.selectedModulationBinding = begun.bindingId;
    refreshModulatorPreview(false);
    if (creating) {
        (void)core::state::project::openProjectModulatorWorkspace(
            navigation_,
            begun.sourceId
        );
        navigation_.selectedModulationBinding = begun.bindingId;
        navigation_.focusedRow.set(0U);
    }
    if (topology.changesTopology()) {
        char feedback[48]{};
        if (topology.createTrack) {
            std::snprintf(
                feedback,
                sizeof(feedback),
                FEEDBACK_CREATE_TRACK_FORMAT,
                static_cast<unsigned>(track + 1U),
                static_cast<unsigned>(row + 1U)
            );
        } else if (topology.createPage) {
            std::snprintf(
                feedback,
                sizeof(feedback),
                FEEDBACK_CREATE_PAGE_FORMAT,
                static_cast<unsigned>(page + 1U),
                static_cast<unsigned>(row + 1U)
            );
        } else {
            std::snprintf(
                feedback,
                sizeof(feedback),
                FEEDBACK_CREATE_MACRO_FORMAT,
                static_cast<unsigned>(row + 1U)
            );
        }
        navigation_.setLifecycleFeedback(feedback);
    } else {
        const auto* binding = findProjectModulationBinding(
            pages_.control.authored.modulation,
            begun.bindingId
        );
        const auto scale = binding != nullptr
            ? depth_parameter::scaleFor(
                  pages_.control.authored.modulation,
                  pages_.control.authored.curves,
                  *binding
              )
            : depth_parameter::Scale::STANDARD;
        const int percent = binding != nullptr
            ? depth_parameter::amountQ15ToPercent(binding->amountQ15, scale)
            : 0;
        char feedback[48]{};
        std::snprintf(
            feedback,
            sizeof(feedback),
            FEEDBACK_DEPTH_PREVIEW_FORMAT,
            percent
        );
        navigation_.setLifecycleFeedback(feedback);
    }
}

FLASHMEM void ProjectHandler::applyDestinationPickerAudition() {
    using namespace core::state::modulation;
    core::state::macro::MacroAutomationSlotAddress address{};
    if (!modulatorAuditionAddress(address)) return;
    const auto audition = pages_.control.audition;
    const bool sourceCreated = audition.sourceCreated();
    const bool macroReturn =
        modulator_navigation::macroAuditionReturnPending(navigation_);
    if (!macro_history_.commitModulatorAudition(pages_, address)) {
        navigation_.setLifecycleFeedback(FEEDBACK_APPLY_FAILED);
        return;
    }
    navigation_.creatingModulatorSource = false;

    (void)macro_edit_services_.synchronizeSharedTrackState();

    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        macros_,
        pages_
    );
    auto& graph = pages_.control.authored.modulation;
    publishModulatorMutation(false);
    if (macroReturn) {
        (void)modulator_navigation::returnToMacroFromAudition(
            {
                overlays_,
                active_view_,
                navigation_,
                macro_edit_,
                pages_,
                project_tracks_,
            },
            true,
            time_provider_ ? time_provider_() : 0U
        );
        return;
    }
    (void)core::state::project::backProjectNavigation(navigation_);
    if (sourceCreated) {
        while (navigation_.depth.get() > 0U) {
            (void)core::state::project::backProjectNavigation(navigation_);
        }
        uint16_t sourceIndex = 0;
        while (sourceIndex < graph.sourceCount &&
               graph.sources[sourceIndex].id != audition.sourceId) {
            ++sourceIndex;
        }
        navigation_.focusedRow.set(static_cast<uint8_t>(sourceIndex));
        navigation_.selectedModulator = audition.sourceId;
        (void)core::state::project::openProjectModulatorDetail(
            navigation_, audition.sourceId
        );
        (void)core::state::project::openProjectModulatorDestinations(navigation_);
    }
    const uint16_t destinationCount =
        core::state::project::modulators::sourceDestinationCount(
            graph,
            audition.sourceId
        );
    navigation_.focusedRow.set(
        static_cast<uint8_t>(destinationCount > 0U ? destinationCount - 1U : 0U)
    );
    navigation_.selectedModulationBinding = audition.bindingId;
    navigation_.setLifecycleFeedback(
        sourceCreated ? FEEDBACK_APPLIED_UNDO : FEEDBACK_DESTINATION_APPLIED
    );
    syncFocusedEncoder();
}

FLASHMEM bool ProjectHandler::cancelDestinationPickerAudition() {
    core::state::macro::MacroAutomationSlotAddress address{};
    const auto node = navigation_.currentNode.get();
    if ((node != core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
         node != core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL) ||
        !modulatorAuditionAddress(address)) {
        return false;
    }
    const auto audition = pages_.control.audition;
    const bool macroReturn =
        modulator_navigation::macroAuditionReturnPending(navigation_);
    const auto creatingKind = navigation_.creatingModulatorKind;
    const auto pickerTrack = navigation_.destinationPickerTrack;
    const auto pickerPage = navigation_.destinationPickerPage;
    if (!macro_history_.cancelModulatorAudition(pages_, address)) {
        navigation_.setLifecycleFeedback(FEEDBACK_STATE_CHANGED);
        return true;
    }
    navigation_.selectedModulationBinding = {};
    refreshModulatorPreview(true, address.macro);
    if (macroReturn) {
        (void)modulator_navigation::returnToMacroFromAudition(
            {
                overlays_,
                active_view_,
                navigation_,
                macro_edit_,
                pages_,
                project_tracks_,
            },
            false,
            time_provider_ ? time_provider_() : 0U
        );
        return true;
    }
    if (audition.sourceCreated() && node ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
        while (navigation_.depth.get() > 0U) {
            (void)core::state::project::backProjectNavigation(navigation_);
        }
        if (core::state::project::openProjectModulatorKindPicker(navigation_)) {
            navigation_.creatingModulatorKind = creatingKind;
            if (core::state::project::openProjectModulatorDestinationPicker(
                    navigation_,
                    pickerTrack,
                    pickerPage,
                    true
                )) {
                navigation_.destinationPickerLevel =
                    core::state::project::ModulatorDestinationPickerLevel::MACRO;
                navigation_.focusedRow.set(address.macro);
            }
        }
    }
    navigation_.setLifecycleFeedback(FEEDBACK_PREVIEW_CANCELLED);
    return true;
}

}  // namespace core::handler

#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "handler/common/ModulatorNavigationWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

using namespace project_handler_internal;
namespace adsr_ui = core::ui::modulation::adsr;

namespace {

const char FEEDBACK_PREVIEW_PENDING[] PROGMEM = "Preview - Apply or Back";
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
const char FEEDBACK_PREVIEW[] PROGMEM = "+25% - Preview";
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

FLASHMEM bool ProjectHandler::modulatorAuditionAddress(
    core::state::macro::MacroAutomationSlotAddress& out
) const {
    using core::state::modulation::ModulationDestinationKind;
    if (!pages_.control.audition.active ||
        pages_.control.audition.destination.kind !=
            ModulationDestinationKind::MACRO_SLOT) {
        return false;
    }
    const auto& destination = pages_.control.audition.destination;
    out = {destination.track, destination.page, destination.macro};
    return macro_history_.modulatorAuditionPending(out);
}

FLASHMEM void ProjectHandler::navigate(float delta) {
    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    if (delta != 0.0f && navigation_.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
        modulatorAuditionAddress(auditionAddress)) {
        navigation_.setLifecycleFeedback(FEEDBACK_PREVIEW_PENDING);
        return;
    }
    if (delta != 0.0f) {
        navigation_.clearLifecycleFeedback();
    }
    if (isProjectNameEditorNode(navigation_.currentNode.get())) {
        navigation_.projectNameKeyIndex = core::state::project::projectNameKeyboardMoveColumn(
            navigation_.projectNameKeyIndex,
            signedStepCount(delta)
        );
        navigation_.notifyContentChanged();
        return;
    }
    macro_history_.endCoalescing();
    core::state::project::navigateProjectRows(
        navigation_,
        delta,
        pages_.control.authored.modulation.sourceCount,
        focusedModulatorDetailRowCount()
    );
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        const auto* binding = focusedModulationBinding();
        navigation_.selectedModulationBinding = binding
            ? binding->id
            : core::state::modulation::ModulationBindingId{};
    }
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::switchTab(float delta) {
    if (delta == 0.0f) return;
    navigation_.clearLifecycleFeedback();
    core::state::project::switchProjectTab(navigation_, signedStepCount(delta));
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::enterFocused() {
    const auto node = navigation_.currentNode.get();
    if (node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
        node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER ||
        node == core::state::project::ProjectNodeId::MODULATOR_TRIGGER ||
        node ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        navigation_.clearLifecycleFeedback();
        enterFocusedModulator();
        syncFocusedEncoder();
        return;
    }
    if (activateFocusedProjectAction()) {
        syncFocusedEncoder();
        return;
    }
    if (applyFocusedProjectStep(1)) {
        syncFocusedEncoder();
        return;
    }
    core::state::project::enterFocusedProjectRow(navigation_);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::enterFocusedModulator() {
    using core::state::project::ProjectNodeId;
    if (navigation_.currentNode.get() == ProjectNodeId::MODULATORS_ROOT) {
        const auto* source = focusedModulator();
        if (source != nullptr) {
            (void)core::state::project::openProjectModulatorDetail(
                navigation_,
                source->id
            );
        } else {
            (void)core::state::project::openProjectModulatorKindPicker(
                navigation_
            );
        }
        return;
    }

    if (navigation_.currentNode.get() ==
        ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        navigation_.creatingModulatorKind = navigation_.focusedRow.get() == 0U
            ? core::state::modulation::ModulatorKind::LFO
            : core::state::modulation::ModulatorKind::ADSR;
        (void)core::state::project::openProjectModulatorDestinationPicker(
            navigation_,
            pages_.currentActiveTrack(),
            pages_.currentActivePage(),
            true
        );
        return;
    }

    if (navigation_.currentNode.get() ==
        ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        using Level = core::state::project::ModulatorDestinationPickerLevel;
        using RowKind = core::state::project::modulators::
            DestinationPickerRowKind;
        const auto target = core::state::project::modulators::
            destinationPickerTargetAtRow(
                pages_,
                navigation_,
                navigation_.focusedRow.get()
            );
        if (!target.valid) return;
        if (navigation_.destinationPickerLevel == Level::TRACK &&
            target.kind == RowKind::TRACK) {
            navigation_.destinationPickerTrack = target.index;
            navigation_.destinationPickerPage = pages_.isTrackEnabled(target.index)
                ? pages_.tracks[target.index].activePage
                : 0U;
            navigation_.destinationPickerLevel = Level::PAGE;
            navigation_.focusedRow.set(
                core::state::project::modulators::destinationPickerPageRow(
                    pages_,
                    target.index,
                    navigation_.destinationPickerPage
                )
            );
            navigation_.notifyContentChanged();
            return;
        }
        if (navigation_.destinationPickerLevel == Level::PAGE &&
            target.kind == RowKind::PAGE) {
            navigation_.destinationPickerPage = target.index;
            navigation_.destinationPickerLevel = Level::MACRO;
            navigation_.focusedRow.set(0U);
            navigation_.notifyContentChanged();
            return;
        }
        startDestinationPickerAudition();
        return;
    }

    if (navigation_.currentNode.get() == ProjectNodeId::MODULATOR_TRIGGER) {
        return;
    }

    if (navigation_.currentNode.get() == ProjectNodeId::MODULATOR_DESTINATIONS) {
        if (focusedModulationBinding() != nullptr) {
            openFocusedModulationDestination();
        } else {
            (void)core::state::project::openProjectModulatorDestinationPicker(
                navigation_,
                pages_.currentActiveTrack(),
                pages_.currentActivePage(),
                false
            );
        }
        return;
    }

    const auto* source = focusedModulator();
    if (!source) return;
    const bool options = navigation_.currentNode.get() ==
        ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
    const bool audition = pages_.control.audition.active &&
        pages_.control.audition.sourceCreated &&
        pages_.control.audition.sourceId == source->id;
    const auto layout = audition
        ? (options
            ? core::state::project::modulators::sourceAuditionOptionsLayout(
                  source->kind
              )
            : core::state::project::modulators::sourceAuditionLayout(
                  source->kind
              ))
        : (options
            ? core::state::project::modulators::sourceOptionsLayout(source->kind)
            : core::state::project::modulators::sourceDetailLayout(source->kind));
    const auto item = layout.at(navigation_.focusedRow.get());
    using Item = core::state::project::modulators::SourceDetailItem;
    if (item == Item::OPTIONS) {
        (void)core::state::project::openProjectModulatorOptions(navigation_);
    } else if (item == Item::DESTINATIONS) {
        if (core::state::project::openProjectModulatorDestinations(navigation_)) {
            const auto* first = focusedModulationBinding();
            navigation_.selectedModulationBinding = first
                ? first->id
                : core::state::modulation::ModulationBindingId{};
        }
    } else if (item == Item::RENAME) {
        (void)core::state::project::openProjectNameEditor(
            navigation_,
            ProjectNodeId::MODULATOR_SOURCE_RENAME,
            source->name.data()
        );
    } else if (item == Item::TRIGGER) {
        (void)core::state::project::openProjectModulatorTrigger(navigation_);
    }
}

FLASHMEM void ProjectHandler::openFocusedModulationDestination() {
    using namespace core::state::modulation;
    const auto* binding = focusedModulationBinding();
    if (binding == nullptr ||
        binding->destination.kind != ModulationDestinationKind::MACRO_SLOT ||
        binding->destination.track >= core::state::macro::TRACK_COUNT ||
        binding->destination.page >= core::state::macro::PAGE_COUNT ||
        binding->destination.macro >= core::state::macro::MACRO_COUNT) {
        navigation_.setLifecycleFeedback("Destination unavailable");
        return;
    }

    const auto destination = binding->destination;
    const uint16_t trackBit = static_cast<uint16_t>(1U << destination.track);
    if ((pages_.currentTrackEnabledMask() & trackBit) == 0U) {
        char feedback[32]{};
        std::snprintf(
            feedback,
            sizeof(feedback),
            "Track %u is Off",
            static_cast<unsigned>(destination.track + 1U)
        );
        navigation_.setLifecycleFeedback(feedback);
        return;
    }

    const auto bindingId = binding->id;
    macro_edit_services_.switchToTrack(destination.track);
    if (pages_.currentActiveTrack() != destination.track) {
        navigation_.setLifecycleFeedback("Destination unavailable");
        return;
    }
    macro_edit_services_.switchToPage(destination.page);
    if (pages_.currentActivePage() != destination.page) {
        navigation_.setLifecycleFeedback("Destination unavailable");
        return;
    }

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        destination.track,
        destination.page,
        destination.macro,
    };
    (void)setProjectControlFocusedModulationBinding(
        pages_.control,
        address,
        bindingId
    );

    uint16_t assignmentCount = 0U;
    uint16_t selectedOrdinal = 0U;
    const auto& graph = pages_.control.authored.modulation;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& candidate = graph.outputBindings[index];
        if (candidate.destination != destination) continue;
        if (candidate.id == bindingId) selectedOrdinal = assignmentCount;
        ++assignmentCount;
    }
    const uint8_t focusedRow = static_cast<uint8_t>(
        (assignmentCount > 0U ? 1U : 0U) + selectedOrdinal
    );

    navigation_.modulatorReturn = {};
    navigation_.clearLifecycleFeedback();
    overlays_.hideAll();
    macro_edit_.loadActiveConfig(
        destination.macro,
        pages_.activeConfigs[destination.macro].channel,
        pages_.activeConfigs[destination.macro].cc
    );
    macro_edit_.openModulation(focusedRow);
    active_view_.set(core::ui::ViewType::MACRO);
    overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, false);
}

FLASHMEM void ProjectHandler::startDestinationPickerAudition() {
    using namespace core::state::modulation;
    using RowKind = core::state::project::modulators::DestinationPickerRowKind;
    if (navigation_.currentNode.get() !=
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return;
    }
    core::state::macro::MacroAutomationSlotAddress pendingAddress{};
    if (modulatorAuditionAddress(pendingAddress)) {
        navigation_.setLifecycleFeedback(FEEDBACK_PREVIEW_PENDING);
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
        .channel = PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        .data = PROJECT_MODULATION_TRIGGER_ANY_NOTE,
    };

    if (creating && pickerTarget.kind == RowKind::KEEP_UNASSIGNED) {
        ProjectModulationResult created{};
        if (creatingKind == ModulatorKind::ADSR) {
            adsrDraft.reach = {};
            created = macro_history_.createUnassignedAdsr(
                pages_, adsrDraft, triggerDraft
            );
        } else {
            sourceDraft.reach = {};
            created = macro_history_.createUnassignedLfo(pages_, sourceDraft);
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
                ? "ADSR created · Unassigned"
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
            adsrDraft.reach = projectModulatorGlobalReach();
            begun = macro_history_.beginAdsrModulatorAudition(
                pages_,
                address,
                adsrDraft,
                triggerDraft,
                binding,
                false,
                &topology
            );
        } else {
            sourceDraft.reach = projectModulatorGlobalReach();
            begun = macro_history_.beginLfoModulatorAudition(
                pages_, address, sourceDraft, binding, false, &topology
            );
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
            nullptr,
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
        navigation_.setLifecycleFeedback(FEEDBACK_PREVIEW);
    }
}

FLASHMEM void ProjectHandler::applyDestinationPickerAudition() {
    using namespace core::state::modulation;
    core::state::macro::MacroAutomationSlotAddress address{};
    if (!modulatorAuditionAddress(address)) return;
    const auto audition = pages_.control.audition;
    const bool sourceCreated = audition.sourceCreated;
    const bool macroReturn =
        modulator_navigation::macroAuditionReturnPending(navigation_);
    if (!macro_history_.commitModulatorAudition(pages_, address)) {
        navigation_.setLifecycleFeedback(FEEDBACK_APPLY_FAILED);
        return;
    }

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
            },
            false,
            time_provider_ ? time_provider_() : 0U
        );
        return true;
    }
    if (audition.sourceCreated && node ==
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

FLASHMEM void ProjectHandler::setFocusedValue(float normalized) {
    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    const bool auditioning = modulatorAuditionAddress(auditionAddress);
    if (setFocusedProjectValue(normalized)) {
        if (!auditioning) navigation_.clearLifecycleFeedback();
    }
}

FLASHMEM void ProjectHandler::syncFocusedEncoder() {
    using core::state::project::ProjectNodeId;

    const auto node = navigation_.currentNode.get();
    const uint8_t row = navigation_.focusedRow.get();

    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
        modulatorAuditionAddress(auditionAddress)) {
        const auto* binding =
            core::state::modulation::findProjectModulationBinding(
                pages_.control.authored.modulation,
                pages_.control.audition.bindingId
            );
        const int16_t percent = binding
            ? core::ui::macro::lfo_audition::depthQ15ToPercent(
                  binding->amountQ15
              )
            : 0;
        configureOptDiscrete(
            encoders_,
            core::ui::macro::lfo_audition::DEPTH_STEP_COUNT,
            static_cast<float>(percent + 100) / 200.0f
        );
        return;
    }

    if (node == ProjectNodeId::MODULATORS_ROOT) {
        const auto* source = focusedModulator();
        if (source == nullptr ||
            source->kind != core::state::modulation::ModulatorKind::LFO) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto& lfo = source->parameters.lfo;
        if (lfo.timing == core::state::modulation::ModulatorTimingMode::FREE) {
            const int count = static_cast<int>(
                PROJECT_MODULATOR_FREE_PERIODS_MS.size()
            );
            configureOptDiscrete(
                encoders_,
                count,
                indexToNormalized(
                    projectModulatorFreePeriodIndex(lfo.freePeriodMs),
                    count
                )
            );
        } else {
            configureOptDiscrete(
                encoders_,
                core::ui::macro::lfo_audition::RATE_COUNT,
                indexToNormalized(
                    core::ui::macro::lfo_audition::rateIndex(lfo.periodTicks),
                    core::ui::macro::lfo_audition::RATE_COUNT
                )
            );
        }
        return;
    }

    if (node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        configureOptDiscrete(encoders_, 1, 0.0f);
        return;
    }

    if (node == ProjectNodeId::MODULATOR_TRIGGER) {
        using core::state::project::modulators::TriggerDetailItem;
        const auto* source = focusedModulator();
        const auto* trigger = source
            ? core::state::modulation::findProjectModulationTriggerForSource(
                  pages_.control.authored.modulation,
                  source->id
              )
            : nullptr;
        if (!trigger) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto& route = trigger->trigger;
        const auto item = static_cast<TriggerDetailItem>(row);
        if (item == TriggerDetailItem::TRACK) {
            configureOptDiscrete(
                encoders_,
                16,
                indexToNormalized(route.track, 16)
            );
        } else if (item == TriggerDetailItem::CHANNEL) {
            const int index = route.channel ==
                    core::state::modulation::PROJECT_MODULATION_TRIGGER_ANY_CHANNEL
                ? 0
                : static_cast<int>(route.channel) + 1;
            configureOptDiscrete(encoders_, 17, indexToNormalized(index, 17));
        } else {
            const int index = route.data ==
                    core::state::modulation::PROJECT_MODULATION_TRIGGER_ANY_NOTE
                ? 0
                : static_cast<int>(route.data) + 1;
            configureOptDiscrete(encoders_, 129, indexToNormalized(index, 129));
        }
        return;
    }

    if (node == ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS) {
        const auto* source = focusedModulator();
        if (!source) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const bool audition = pages_.control.audition.active &&
            pages_.control.audition.sourceCreated &&
            pages_.control.audition.sourceId == source->id;
        const bool options = node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
        const auto layout = audition
            ? (options
                ? core::state::project::modulators::sourceAuditionOptionsLayout(
                      source->kind
                  )
                : core::state::project::modulators::sourceAuditionLayout(
                      source->kind
                  ))
            : (options
                ? core::state::project::modulators::sourceOptionsLayout(
                      source->kind
                  )
                : core::state::project::modulators::sourceDetailLayout(
                      source->kind
                  ));
        const auto item = layout.at(row);
        using Item = core::state::project::modulators::SourceDetailItem;
        using namespace core::state::modulation;
        switch (item) {
            case Item::ENABLED:
                configureOptDiscrete(
                    encoders_,
                    2,
                    (source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U
                        ? 1.0f : 0.0f
                );
                return;
            case Item::SHAPE:
                configureOptDiscrete(
                    encoders_,
                    core::ui::macro::lfo_audition::SHAPE_COUNT,
                    indexToNormalized(
                        static_cast<int>(source->parameters.lfo.shape),
                        core::ui::macro::lfo_audition::SHAPE_COUNT
                    )
                );
                return;
            case Item::RATE: {
                const auto& lfo = source->parameters.lfo;
                if (lfo.timing == ModulatorTimingMode::FREE) {
                    const int count = static_cast<int>(
                        PROJECT_MODULATOR_FREE_PERIODS_MS.size()
                    );
                    configureOptDiscrete(
                        encoders_,
                        count,
                        indexToNormalized(
                            projectModulatorFreePeriodIndex(lfo.freePeriodMs),
                            count
                        )
                    );
                } else {
                    configureOptDiscrete(
                        encoders_,
                        core::ui::macro::lfo_audition::RATE_COUNT,
                        indexToNormalized(
                            core::ui::macro::lfo_audition::rateIndex(
                                lfo.periodTicks
                            ),
                            core::ui::macro::lfo_audition::RATE_COUNT
                        )
                    );
                }
                return;
            }
            case Item::TIMING:
                configureOptDiscrete(
                    encoders_,
                    2,
                    (source->kind == ModulatorKind::ADSR
                         ? source->parameters.adsr.timing
                         : source->parameters.lfo.timing) ==
                            ModulatorTimingMode::FREE
                        ? 1.0f : 0.0f
                );
                return;
            case Item::PHASE:
                configureOptDiscrete(
                    encoders_,
                    101,
                    std::clamp(
                        (static_cast<float>(source->parameters.lfo.phaseQ15) +
                         32767.0f) /
                            65534.0f,
                        0.0f,
                        1.0f
                    )
                );
                return;
            case Item::RETRIGGER:
                if (source->kind == ModulatorKind::ADSR) {
                    configureOptDiscrete(
                        encoders_,
                        2,
                        indexToNormalized(
                            static_cast<int>(
                                source->parameters.adsr.retrigger
                            ),
                            2
                        )
                    );
                    return;
                }
                if (source->parameters.lfo.retrigger ==
                    ModulatorRetriggerPolicy::EXPLICIT_TRIGGER) {
                    configureOptDiscrete(encoders_, 1, 0.0f);
                    return;
                }
                configureOptDiscrete(
                    encoders_,
                    2,
                    indexToNormalized(
                        static_cast<int>(source->parameters.lfo.retrigger),
                        2
                    )
                );
                return;
            case Item::DEPTH: {
                const auto* binding = findProjectModulationBinding(
                    pages_.control.authored.modulation,
                    pages_.control.audition.bindingId
                );
                configureOptDiscrete(
                    encoders_,
                    201,
                    binding
                        ? std::clamp(
                              (static_cast<float>(binding->amountQ15) /
                                   32767.0f +
                               1.0f) *
                                  0.5f,
                              0.0f,
                              1.0f
                          )
                        : 0.5f
                );
                return;
            }
            case Item::ATTACK:
            case Item::DECAY:
            case Item::RELEASE: {
                const uint16_t duration = item == Item::ATTACK
                    ? source->parameters.adsr.attack
                    : (item == Item::DECAY
                        ? source->parameters.adsr.decay
                        : source->parameters.adsr.release);
                const int count = adsr_ui::DURATION_COUNT;
                configureOptDiscrete(
                    encoders_,
                    count,
                    indexToNormalized(
                        adsr_ui::durationIndex(
                            duration,
                            source->parameters.adsr.timing
                        ),
                        count
                    )
                );
                return;
            }
            case Item::SUSTAIN:
                configureOptDiscrete(
                    encoders_,
                    101,
                    std::clamp(
                        static_cast<float>(source->parameters.adsr.sustainQ15) /
                            static_cast<float>(
                                PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
                            ),
                        0.0f,
                        1.0f
                    )
                );
                return;
            case Item::CURVE:
                configureOptDiscrete(
                    encoders_,
                    3,
                    indexToNormalized(
                        static_cast<int>(source->parameters.adsr.curve),
                        3
                    )
                );
                return;
            default:
                configureOptDiscrete(encoders_, 1, 0.0f);
                return;
        }
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATIONS) {
        const auto* binding = focusedModulationBinding();
        if (!binding) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        configureOptDiscrete(
            encoders_,
            201,
            std::clamp(
                (static_cast<float>(binding->amountQ15) / 32767.0f + 1.0f) *
                    0.5f,
                0.0f,
                1.0f
            )
        );
        return;
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        configureOptDiscrete(encoders_, 1, 0.0f);
        return;
    }

    if (node == ProjectNodeId::MUSIC_SCALE && row <= 2) {
        const int count = sequencer_settings_.choiceCount(row);
        if (count > 0) {
            configureOptDiscrete(
                encoders_,
                count,
                indexToNormalized(sequencer_settings_.currentChoiceIndex(row), count)
            );
        }
        return;
    }

    if (node == ProjectNodeId::MUSIC_ROOT && row == 3) {
        configureOptDiscrete(
            encoders_,
            project::PROJECT_STEP_PASTE_MODE_COUNT,
            indexToNormalized(
                static_cast<int>(navigation_.stepPasteMode),
                project::PROJECT_STEP_PASTE_MODE_COUNT
            )
        );
        return;
    }

    if (node == ProjectNodeId::TRANSPORT_ROOT) {
        switch (row) {
            case 0:
                configureOptContinuous(
                    encoders_,
                    tempoToNormalized(status_bar_.tempo.get()),
                    normalizedTurnsForStepRate(
                        project::PROJECT_TEMPO_RANGE_STEPS,
                        PROJECT_OPT_TEMPO_STEPS_PER_TURN
                    )
                );
                return;
            case 1:
                configureOptDiscrete(
                    encoders_,
                    project::PROJECT_SWING_STEPS,
                    indexToNormalized(navigation_.transportSwingPercent, project::PROJECT_SWING_STEPS),
                    normalizedTurnsForStepRate(
                        project::PROJECT_SWING_STEPS,
                        PROJECT_OPT_PERCENT_STEPS_PER_TURN
                    )
                );
                return;
            case 2:
                configureOptDiscrete(
                    encoders_,
                    3,
                    indexToNormalized(midiSyncModeIndex(midi_sync_.mode.get()), 3)
                );
                return;
            case 3:
                configureOptDiscrete(
                    encoders_,
                    project::PROJECT_RUN_MODE_COUNT,
                    indexToNormalized(navigation_.transportRunMode, project::PROJECT_RUN_MODE_COUNT)
                );
                return;
            default:
                return;
        }
    }

    if (node == ProjectNodeId::STORAGE_ROOT) {
        switch (row) {
            case 6:
                configureOptDiscrete(encoders_, 2, navigation_.autosaveEnabled ? 1.0f : 0.0f);
                return;
            default:
                return;
        }
    }

    if (node == ProjectNodeId::ROUTING_ROOT &&
        row < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        const uint8_t activeTrack = sequencer_tracks_.activeTrackIndex();
        const uint8_t channel = (row == activeTrack)
            ? sequencer_.pattern.midiChannel.get()
            : sequencer_tracks_.track(row).midiChannel.get();
        configureOptDiscrete(
            encoders_,
            project::PROJECT_MIDI_CHANNEL_COUNT,
            indexToNormalized(channel, project::PROJECT_MIDI_CHANNEL_COUNT)
        );
        return;
    }

    if (isProjectNameEditorNode(node)) {
        navigation_.projectNameOptRawPosition = 0.0f;
        navigation_.projectNameOptRowAccumulator = 0.0f;
        configureOptRaw(encoders_);
        return;
    }
}


}  // namespace core::handler

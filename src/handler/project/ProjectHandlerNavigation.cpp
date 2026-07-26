#include "handler/project/ProjectHandlerInternals.hpp"

#include <cmath>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulatorSourceSession.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"

namespace core::handler {

using namespace project_handler_internal;
namespace adsr_ui = core::ui::modulation::adsr;

namespace {

const char FEEDBACK_SHARED_SOURCE_READ_ONLY[] PROGMEM =
    "Shared source - Apply then edit";

}  // namespace

FLASHMEM bool ProjectHandler::modulatorAuditionAddress(
    core::state::macro::MacroAutomationSlotAddress& out
) const {
    using core::state::modulation::ModulationDestinationKind;
    const auto& audition = pages_.control.audition;
    const auto session = core::state::modulation::
        resolveProjectModulatorSourceSession(
        pages_.control,
        audition.sourceId
    );
    if (!session.audition() || audition.destination.kind !=
            ModulationDestinationKind::MACRO_SLOT) {
        return false;
    }
    const auto& destination = audition.destination;
    out = {destination.track, destination.page, destination.macro};
    return macro_history_.modulatorAuditionPending(out);
}

FLASHMEM void ProjectHandler::navigate(float delta) {
    if (delta != 0.0f) {
        commitPendingRoutingGesture();
        endProjectSettingsGesture();
    }
    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    if (delta != 0.0f && navigation_.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
        modulatorAuditionAddress(auditionAddress)) {
        navigation_.setLifecycleFeedback(
            MODULATOR_PREVIEW_PENDING_FEEDBACK
        );
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
    commitPendingRoutingGesture();
    endProjectSettingsGesture();
    navigation_.clearLifecycleFeedback();
    core::state::project::switchProjectTab(navigation_, signedStepCount(delta));
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::enterFocused() {
    endProjectSettingsGesture();
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
        const auto target = core::state::project::modulators::
            sourceKindTargetAtRow(navigation_.focusedRow.get());
        if (!target.valid) return;
        navigation_.creatingModulatorKind = target.kind;
        if (navigation_.creatingModulatorKind ==
            core::state::modulation::ModulatorKind::RECORDED_SHAPE) {
            (void)createDefaultRecordedShape();
            return;
        }
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

    if (navigation_.currentNode.get() == ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        navigation_.currentNode.get() == ProjectNodeId::MODULATOR_SOURCE_OPTIONS) {
        using namespace core::state::modulation;
        using Item = core::state::project::modulators::SourceDetailItem;
        auto* focusedSource = focusedModulator();
        if (focusedSource != nullptr && focusedSource->kind == ModulatorKind::ADSR) {
            const auto session = resolveProjectModulatorSourceSession(
                pages_.control,
                focusedSource->id
            );
            if (!pages_.control.audition.active() || session.valid()) {
                const bool options = navigation_.currentNode.get() ==
                    ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
                const auto layout = core::state::project::modulators::
                    sourceWorkspaceLayout(
                        focusedSource->kind,
                        options,
                        session.audition()
                    );
                const Item item = layout.at(navigation_.focusedRow.get());
                ModulatorEnvelopeTimeParameter parameter{};
                const char* parameterLabel = nullptr;
                if (item == Item::DELAY) {
                    parameter = ModulatorEnvelopeTimeParameter::DELAY;
                    parameterLabel = "Delay";
                } else if (item == Item::ATTACK) {
                    parameter = ModulatorEnvelopeTimeParameter::ATTACK;
                    parameterLabel = "Attack";
                } else if (item == Item::HOLD) {
                    parameter = ModulatorEnvelopeTimeParameter::HOLD;
                    parameterLabel = "Hold";
                } else if (item == Item::DECAY) {
                    parameter = ModulatorEnvelopeTimeParameter::DECAY;
                    parameterLabel = "Decay";
                } else if (item == Item::RELEASE) {
                    parameter = ModulatorEnvelopeTimeParameter::RELEASE;
                    parameterLabel = "Release";
                } else if (item == Item::SMOOTH) {
                    parameter = ModulatorEnvelopeTimeParameter::SMOOTH;
                    parameterLabel = "Smooth";
                }
                if (parameterLabel != nullptr &&
                    modulatorAdsrTiming(focusedSource->parameters.adsr.traits) ==
                        ModulatorTimingMode::SYNC) {
                    if (!session.allows(
                            ProjectModulatorSourceSessionCapability::EDIT_SOURCE
                        )) {
                        navigation_.setLifecycleFeedback(
                            FEEDBACK_SHARED_SOURCE_READ_ONLY
                        );
                        return;
                    }
                    auto parameters = focusedSource->parameters.adsr;
                    const auto current = modulatorAdsrFeel(
                        parameters.traits,
                        parameter
                    );
                    const auto next = static_cast<ModulatorEnvelopeFeel>(
                        (static_cast<uint8_t>(current) + 1U) % 3U
                    );
                    parameters.traits = withModulatorAdsrFeel(
                        parameters.traits,
                        parameter,
                        next
                    );
                    if (session.newAudition()) {
                        focusedSource->parameters.adsr = parameters;
                        pages_.control.markAuthoredMutation();
                        refreshModulatorPreview(false);
                    } else if (macro_history_.setProjectAdsrParametersCoalesced(
                                   pages_,
                                   focusedSource->id,
                                   parameters
                               )) {
                        publishModulatorMutation(false);
                    }
                    char feedback[40]{};
                    std::snprintf(
                        feedback,
                        sizeof(feedback),
                        "%s · %s",
                        parameterLabel,
                        adsr_ui::feelLabel(next)
                    );
                    navigation_.setLifecycleFeedback(feedback);
                    return;
                }
            }
        }
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
    const auto session = core::state::modulation::
        resolveProjectModulatorSourceSession(pages_.control, source->id);
    if (pages_.control.audition.active() && !session.valid()) return;
    const auto layout = core::state::project::modulators::sourceWorkspaceLayout(
        source->kind,
        options,
        session.audition()
    );
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
        if (core::state::project::openProjectModulatorTrigger(navigation_) &&
            session.existingAudition()) {
            navigation_.setLifecycleFeedback(
                FEEDBACK_SHARED_SOURCE_READ_ONLY
            );
        }
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
        core::state::project::projectTrackMidiChannel(
            project_tracks_,
            destination.track
        ),
        pages_.activeConfigs[destination.macro].cc
    );
    macro_edit_.openModulation(focusedRow);
    active_view_.set(core::ui::ViewType::MACRO);
    overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, false);
}

FLASHMEM void ProjectHandler::setFocusedValue(float normalized) {
    if (recorded_shape_capture_button_active_) {
        (void)recorded_shape_capture_.touchRawEncoder(
            static_cast<int32_t>(std::lround(normalized)),
            time_provider_ ? time_provider_() : 0U
        );
        syncRecordedShapeCaptureRevision();
        return;
    }
    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    const bool auditioning = modulatorAuditionAddress(auditionAddress);
    if (setFocusedProjectValue(normalized)) {
        if (!auditioning) navigation_.clearLifecycleFeedback();
    }
}

}  // namespace core::handler

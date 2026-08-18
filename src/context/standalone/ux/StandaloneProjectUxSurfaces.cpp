#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/modulation/ModulationDepthParameterMapping.hpp"
#include "state/modulation/ModulatorLfoParameterMapping.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulatorSourceSession.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "ui/modulation/ModulatorLfoUiModel.hpp"

namespace core::context::standalone::ux {
namespace {

using core::state::modulation::ModulationBindingState;
using core::state::modulation::ModulatorSourceState;
using core::state::project::ProjectNodeId;
using core::state::project::modulators::SourceDetailItem;

FLASHMEM bool isButton(
    const oc::core::input::InputBindingTraceEvent& event,
    Config::ButtonID button,
    oc::core::input::ButtonBindingType type
) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonId == static_cast<oc::type::ButtonID>(button) &&
           event.buttonType == type;
}

FLASHMEM bool isEncoder(
    const oc::core::input::InputBindingTraceEvent& event,
    Config::EncoderID encoder
) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           event.encoderId == static_cast<oc::type::EncoderID>(encoder);
}

FLASHMEM const char* sourceKind(const ModulatorSourceState& source) {
    if (source.kind == core::state::modulation::ModulatorKind::LFO) {
        return "lfo";
    }
    if (source.kind == core::state::modulation::ModulatorKind::ADSR) {
        return "adsr";
    }
    return "recorded_shape";
}

FLASHMEM const char* recordedShapeOperationStatus(
    core::state::modulation::ProjectRecordedShapeCaptureStatus status
) {
    using Status = core::state::modulation::
        ProjectRecordedShapeCaptureStatus;
    switch (status) {
        case Status::ARMED: return "armed";
        case Status::RECORDING:
        case Status::REDUCED: return "recording";
        case Status::COMMITTED: return "committed";
        case Status::NO_CHANGE: return "no_change";
        case Status::CANCELLED: return "cancelled";
        case Status::INVALIDATED:
        case Status::SCRATCH_UNAVAILABLE:
        case Status::COMMIT_FAILED: return "invalidated";
        case Status::IDLE:
        default: return "idle";
    }
}

FLASHMEM const char* recordedShapeOutcome(
    core::state::modulation::ProjectRecordedShapeCaptureStatus status
) {
    using Status = core::state::modulation::
        ProjectRecordedShapeCaptureStatus;
    switch (status) {
        case Status::RECORDING:
        case Status::REDUCED:
        case Status::COMMITTED: return "applied";
        case Status::NO_CHANGE: return "no_change";
        case Status::CANCELLED: return "cancelled";
        case Status::INVALIDATED:
        case Status::SCRATCH_UNAVAILABLE:
        case Status::COMMIT_FAILED: return "invalidated";
        default: return nullptr;
    }
}

FLASHMEM const char* recordedShapeStatusFromFeedback(const char* feedback) {
    if (!feedback) return nullptr;
    if (std::strncmp(feedback, "Recorded ", 9U) == 0) return "committed";
    if (std::strncmp(feedback, "No movement", 11U) == 0) return "no_change";
    if (std::strstr(feedback, "cancelled") != nullptr) return "cancelled";
    if (std::strstr(feedback, "failed") != nullptr ||
        std::strstr(feedback, "unavailable") != nullptr) {
        return "invalidated";
    }
    return nullptr;
}

FLASHMEM const char* detailProperty(SourceDetailItem item) {
    switch (item) {
        case SourceDetailItem::PREVIEW: return "source";
        case SourceDetailItem::ENABLED: return "enabled";
        case SourceDetailItem::SHAPE: return "shape";
        case SourceDetailItem::RATE: return "rate";
        case SourceDetailItem::TIMING: return "timing";
        case SourceDetailItem::PHASE: return "phase";
        case SourceDetailItem::RETRIGGER: return "retrigger";
        case SourceDetailItem::RECORD: return "record";
        case SourceDetailItem::LENGTH: return "length";
        case SourceDetailItem::SOURCE_DOMAIN: return "domain";
        case SourceDetailItem::DESTINATIONS: return "destinations";
        case SourceDetailItem::OPTIONS: return "details";
        case SourceDetailItem::RENAME: return "rename";
        case SourceDetailItem::DELAY: return "delay";
        case SourceDetailItem::ATTACK: return "attack";
        case SourceDetailItem::HOLD: return "hold";
        case SourceDetailItem::DECAY: return "decay";
        case SourceDetailItem::SUSTAIN: return "sustain";
        case SourceDetailItem::RELEASE: return "release";
        case SourceDetailItem::TRIGGER: return "trigger";
        case SourceDetailItem::SMOOTH: return "smooth";
        case SourceDetailItem::RESPONSE: return "response";
        case SourceDetailItem::DEPTH: return "depth";
        case SourceDetailItem::INVALID: return "invalid";
    }
    return "invalid";
}

FLASHMEM bool isEnvelopeTimeProperty(const char* property) {
    return property != nullptr &&
        (std::strcmp(property, "delay") == 0 ||
         std::strcmp(property, "attack") == 0 ||
         std::strcmp(property, "hold") == 0 ||
         std::strcmp(property, "decay") == 0 ||
         std::strcmp(property, "release") == 0 ||
         std::strcmp(property, "smooth") == 0);
}

FLASHMEM void formatSourcePrimary(
    char (&out)[16],
    const core::state::modulation::ProjectControlState& control,
    const ModulatorSourceState& source
) {
    using namespace core::state::modulation;
    if (source.kind == ModulatorKind::LFO) {
        const auto& lfo = source.parameters.lfo;
        if (lfo.timing == ModulatorTimingMode::FREE) {
            std::snprintf(
                out,
                sizeof(out),
                "%lums",
                static_cast<unsigned long>(lfo.freePeriodMs)
            );
        } else {
            std::snprintf(
                out,
                sizeof(out),
                "%s",
                core::ui::modulation::lfo::rateLabel(
                    core::state::modulation::lfo::rateIndex(lfo.periodTicks)
                )
            );
        }
        return;
    }
    if (source.kind == ModulatorKind::ADSR) {
        std::snprintf(
            out,
            sizeof(out),
            "A%u",
            static_cast<unsigned>(source.parameters.adsr.attack)
        );
        return;
    }
    const auto* curve = findProjectCurve(
        control.authored.curves,
        source.parameters.recordedCurveId
    );
    const uint32_t ticks = curve ? curve->durationTicks : 0U;
    const uint32_t tenths =
        (ticks * 10U + PROJECT_CONTROL_TICKS_PER_BEAT / 2U) /
        PROJECT_CONTROL_TICKS_PER_BEAT;
    std::snprintf(
        out,
        sizeof(out),
        "%lu.%lub",
        static_cast<unsigned long>(tenths / 10U),
        static_cast<unsigned long>(tenths % 10U)
    );
}

FLASHMEM void formatBindingDepth(
    char (&out)[16],
    const core::state::modulation::ProjectControlState& control,
    const ModulationBindingState& binding
) {
    const int16_t percent =
        core::state::modulation::depth::amountQ15ToPercent(
        binding.amountQ15,
        core::state::modulation::depth::scaleFor(
            control.authored.modulation,
            control.authored.curves,
            binding
        )
    );
    std::snprintf(out, sizeof(out), "%+d%%", static_cast<int>(percent));
}

FLASHMEM const ModulatorSourceState* focusedSource(
    const core::state::project::ProjectNavigationState& navigation,
    const core::state::modulation::ProjectModulationState& graph
) {
    if (navigation.currentNode.get() == ProjectNodeId::MODULATORS_ROOT) {
        const uint16_t row = navigation.focusedRow.get();
        return row < graph.sourceCount ? &graph.sources[row] : nullptr;
    }
    return core::state::modulation::findProjectModulator(
        graph,
        navigation.selectedModulator
    );
}

FLASHMEM const ModulationBindingState* focusedBinding(
    const core::state::project::ProjectNavigationState& navigation,
    const core::state::modulation::ProjectModulationState& graph
) {
    if (navigation.currentNode.get() != ProjectNodeId::MODULATOR_DESTINATIONS) {
        return nullptr;
    }
    return core::state::project::modulators::sourceBindingAtOrdinal(
        graph,
        navigation.selectedModulator,
        navigation.focusedRow.get()
    );
}

FLASHMEM const char* feedbackOutcome(const char* feedback) {
    if (!feedback || feedback[0] == '\0') return nullptr;
    if (std::strstr(feedback, "cancelled") != nullptr) return "cancelled";
    if (std::strncmp(feedback, "Deleted ", 8U) == 0 ||
        std::strncmp(feedback, "Ready ", 6U) == 0 ||
        std::strncmp(feedback, "Recorded ", 9U) == 0 ||
        std::strcmp(feedback, "Destination removed") == 0 ||
        std::strcmp(feedback, "Destination applied") == 0 ||
        std::strcmp(feedback, "Applied - one Undo") == 0 ||
        std::strncmp(feedback, "Pasted ", 7U) == 0 ||
        std::strncmp(feedback, "Copied ", 7U) == 0 ||
        std::strncmp(feedback, "Independent", 11U) == 0) {
        return "applied";
    }
    if (std::strncmp(feedback, "No movement", 11U) == 0) {
        return "no_change";
    }
    if (std::strstr(feedback, "failed") != nullptr ||
        std::strstr(feedback, "unavailable") != nullptr) {
        return "rejected";
    }
    return nullptr;
}

constexpr const char* projectTabToken(
    core::state::project::ProjectTab tab
) {
    using core::state::project::ProjectTab;
    switch (tab) {
        case ProjectTab::MUSIC: return "music";
        case ProjectTab::TRANSPORT: return "transport";
        case ProjectTab::STORAGE: return "storage";
        case ProjectTab::ROUTING: return "routing";
        case ProjectTab::MODULATORS: return "modulators";
        case ProjectTab::OVERVIEW:
        case ProjectTab::COUNT:
        default: return "overview";
    }
}

template <size_t N>
constexpr const char* projectNodeTokenLiteral(const char (&token)[N]) {
    static_assert(
        N <= sizeof(core::validation::ux::SemanticUxContext{}.valueLabel),
        "Project UX token exceeds SemanticUxContext::valueLabel"
    );
    return token;
}

constexpr const char* projectNodeToken(ProjectNodeId node) {
    switch (node) {
        case ProjectNodeId::OVERVIEW_ROOT:
            return projectNodeTokenLiteral("overview_root");
        case ProjectNodeId::MUSIC_ROOT:
            return projectNodeTokenLiteral("music_root");
        case ProjectNodeId::MUSIC_SCALE:
            return projectNodeTokenLiteral("music_scale");
        case ProjectNodeId::MUSIC_CC_DEFAULTS:
            return projectNodeTokenLiteral("music_cc");
        case ProjectNodeId::TRANSPORT_ROOT:
            return projectNodeTokenLiteral("transport_root");
        case ProjectNodeId::STORAGE_ROOT:
            return projectNodeTokenLiteral("storage_root");
        case ProjectNodeId::ROUTING_ROOT:
            return projectNodeTokenLiteral("routing_root");
        case ProjectNodeId::MODULATORS_ROOT:
            return projectNodeTokenLiteral("modulators_root");
        case ProjectNodeId::MODULATOR_SOURCE_DETAIL:
            return projectNodeTokenLiteral("mod_source");
        case ProjectNodeId::MODULATOR_DESTINATIONS:
            return projectNodeTokenLiteral("mod_dests");
        case ProjectNodeId::MODULATOR_DESTINATION_PICKER:
            return projectNodeTokenLiteral("mod_dest_pick");
        case ProjectNodeId::NEW_PROJECT_CONFIRM:
            return projectNodeTokenLiteral("new_confirm");
        case ProjectNodeId::LOAD_PROJECT:
            return projectNodeTokenLiteral("load_project");
        case ProjectNodeId::LOAD_PROJECT_CONFIRM:
            return projectNodeTokenLiteral("load_confirm");
        case ProjectNodeId::SAVE_AS_PROJECT_NAME:
            return projectNodeTokenLiteral("save_as_name");
        case ProjectNodeId::RENAME_PROJECT_NAME:
            return projectNodeTokenLiteral("rename_name");
        case ProjectNodeId::MODULATOR_SOURCE_OPTIONS:
            return projectNodeTokenLiteral("mod_options");
        case ProjectNodeId::MODULATOR_SOURCE_RENAME:
            return projectNodeTokenLiteral("mod_rename");
        case ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER:
            return projectNodeTokenLiteral("mod_kind_pick");
        case ProjectNodeId::MODULATOR_TRIGGER:
            return projectNodeTokenLiteral("mod_trigger");
    }
    return projectNodeTokenLiteral("unknown");
}

}  // namespace

FLASHMEM ProjectNavigationUxSurface::ProjectNavigationUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::project::ProjectNavigationState& navigation
)
    : active_view_(activeView), navigation_(navigation) {}

FLASHMEM bool ProjectNavigationUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    using core::state::interaction::ControllerIntent;

    if (active_view_.get() != core::ui::ViewType::PROJECT) return false;

    const bool navTurn = isEncoder(event, Config::EncoderID::NAV);
    const bool optTurn = isEncoder(event, Config::EncoderID::OPT);
    const bool navRelease = isButton(
        event,
        Config::ButtonID::NAV,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool tabPress = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool tabRelease = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool backRelease = isButton(
        event,
        Config::ButtonID::LEFT_TOP,
        oc::core::input::ButtonBindingType::RELEASE
    );
    if (!navTurn && !optTurn && !navRelease && !tabPress && !tabRelease &&
        !backRelease) {
        return false;
    }

    const bool tabNavigation = navigation_.physicalHoldActive.get();
    out.mode = "project";
    out.target = "project_navigation";
    out.targetIndex = navigation_.focusedRow.get();
    out.property = projectTabToken(navigation_.activeTab.get());
    std::snprintf(
        out.valueLabel,
        sizeof(out.valueLabel),
        "%s",
        projectNodeToken(navigation_.currentNode.get())
    );
    out.operationStatus = tabNavigation ? "tab_navigation" : "content";

    if (navTurn) {
        out.intent = tabNavigation
            ? ControllerIntent::NAVIGATE_SECONDARY_AXIS
            : ControllerIntent::MOVE_FOCUS;
        out.effect = tabNavigation
            ? "switch_project_tab" : "focus_project_row";
    } else if (optTurn) {
        out.intent = ControllerIntent::EDIT_VALUE;
        out.effect = "edit_project_value";
    } else if (navRelease) {
        out.intent = ControllerIntent::ACTIVATE;
        out.effect = "activate_project_item";
    } else if (tabPress) {
        out.intent = ControllerIntent::OPEN_ADVANCED;
        out.effect = "enter_project_tab_navigation";
    } else if (tabRelease) {
        out.effect = "leave_project_tab_navigation";
    } else {
        out.intent = ControllerIntent::BACK;
        out.effect = "back_project_navigation";
    }
    return true;
}

FLASHMEM ProjectModulatorsUxSurface::ProjectModulatorsUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::project::ProjectNavigationState& navigation,
    core::state::macro::MacroPagesState& pages,
    core::state::macro::MacroUiState& macroUi,
    core::state::StructureClipboardState& clipboard,
    core::state::macro::MacroHistoryService& history
)
    : active_view_(activeView),
      navigation_(navigation),
      pages_(pages),
      macro_ui_(macroUi),
      clipboard_(clipboard),
      history_(history) {}

FLASHMEM bool ProjectModulatorsUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MODULATORS ||
        navigation_.activeTab.get() !=
            core::state::project::ProjectTab::MODULATORS) {
        return false;
    }

    const auto node = navigation_.currentNode.get();
    if (node != ProjectNodeId::MODULATORS_ROOT &&
        node != ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
        node != ProjectNodeId::MODULATOR_SOURCE_OPTIONS &&
        node != ProjectNodeId::MODULATOR_SOURCE_RENAME &&
        node != ProjectNodeId::MODULATOR_DESTINATIONS &&
        node != ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER &&
        node != ProjectNodeId::MODULATOR_TRIGGER &&
        node != ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return false;
    }

    const auto& control = pages_.control;
    const auto& graph = control.authored.modulation;
    const auto* source = focusedSource(navigation_, graph);
    const auto* binding = focusedBinding(navigation_, graph);
    const auto sourceSession = core::state::modulation::
        resolveProjectModulatorSourceSession(
            control,
            source != nullptr ? source->id : control.audition.sourceId
        );
    const bool sourceAudition = source != nullptr && sourceSession.audition();
    const auto* auditionBinding = sourceAudition
        ? core::state::modulation::findProjectModulationBinding(
              graph,
              control.audition.bindingId
          )
        : nullptr;
    const auto& auditionDestination = control.audition.destination;
    const bool auditionDestinationExists = sourceSession.audition() &&
        auditionDestination.track < core::state::macro::TRACK_COUNT &&
        auditionDestination.page < core::state::macro::PAGE_COUNT &&
        auditionDestination.macro < core::state::macro::MACRO_COUNT &&
        pages_.isTrackEnabled(auditionDestination.track) &&
        pages_.tracks[auditionDestination.track].isPageEnabled(
            auditionDestination.page
        ) &&
        pages_.pageData(
            auditionDestination.track,
            auditionDestination.page
        ).isMacroActive(auditionDestination.macro);
    const char* feedback = navigation_.lifecycleFeedback.empty()
        ? nullptr : navigation_.lifecycleFeedback.get();

    out.projection = "committed";
    out.hasOperationGeneration = history_.undoCount() > 0U;
    out.operationGeneration = history_.undoCount();
    out.outcome = feedbackOutcome(feedback);
    if (sourceAudition) {
        out.projection = auditionDestinationExists
            ? "audible_audition" : "silent_preview";
        out.hasOperationGeneration = true;
        out.operationGeneration = control.audition.generation;
        out.operationStatus = "audition";
        out.sourceTrack = control.audition.destination.track;
        out.targetTrack = control.audition.destination.track;
    }

    if (node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        const auto target = core::state::project::modulators::
            sourceKindTargetAtRow(navigation_.focusedRow.get());
        if (!target.valid) return false;
        const auto kind = target.kind;
        out.mode = "project.modulator_kind_picker";
        out.target = "source_kind";
        out.targetIndex = navigation_.focusedRow.get();
        out.property = kind == core::state::modulation::ModulatorKind::LFO
            ? "lfo"
            : (kind == core::state::modulation::ModulatorKind::ADSR
                   ? "adsr"
                   : "recorded_shape");
        out.valueLabel[0] = kind ==
                core::state::modulation::ModulatorKind::LFO
            ? 'L'
            : (kind == core::state::modulation::ModulatorKind::ADSR ? 'A' : 'M');
        out.valueLabel[1] = '\0';
        out.operationStatus = "explicit_selection";
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_source_kind";
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = kind ==
                    core::state::modulation::ModulatorKind::RECORDED_SHAPE
                ? "create_project_recorded_shape"
                : "choose_source_kind";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "cancel_source_creation";
        }
        return true;
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        using PickerLevel =
            core::state::project::ModulatorDestinationPickerLevel;
        const bool auditioning = sourceSession.audition();
        const auto level = navigation_.destinationPickerLevel;
        out.mode = level == PickerLevel::TRACK
            ? "project.modulator_destination.track"
            : (level == PickerLevel::PAGE
                   ? "project.modulator_destination.page"
                   : "project.modulator_destination.macro");
        out.projection = auditioning
            ? (auditionDestinationExists
                   ? "audible_audition" : "silent_preview")
            : "committed";
        out.target = level == PickerLevel::TRACK
            ? "track"
            : (level == PickerLevel::PAGE ? "page" : "macro_destination");
        out.targetIndex = navigation_.focusedRow.get();
        out.sourceTrack = navigation_.destinationPickerTrack;
        out.targetTrack = navigation_.destinationPickerTrack;
        out.property = navigation_.creatingModulatorSource
            ? (navigation_.creatingModulatorKind ==
                       core::state::modulation::ModulatorKind::LFO
                   ? "new_lfo_destination"
                   : (navigation_.creatingModulatorKind ==
                              core::state::modulation::ModulatorKind::ADSR
                          ? "new_adsr_destination"
                          : "new_recorded_shape_destination"))
            : "destination";
        out.operationStatus = auditioning
            ? "audition"
            : (level == PickerLevel::TRACK
                   ? "selecting_track"
                   : (level == PickerLevel::PAGE
                          ? "selecting_page"
                          : (navigation_.creatingModulatorSource
                                 ? "creating_source"
                                 : "adding_destination")));
        if (auditioning) {
            out.hasOperationGeneration = true;
            out.operationGeneration = control.audition.generation;
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = level == PickerLevel::TRACK
                ? "focus_track"
                : (level == PickerLevel::PAGE
                       ? "focus_page" : "focus_destination");
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = level == PickerLevel::TRACK
                ? "choose_track"
                : (level == PickerLevel::PAGE
                       ? "choose_page"
                       : "start_destination_audition");
        } else if (isEncoder(event, Config::EncoderID::OPT) && auditioning) {
            out.effect = "edit_destination_audition_depth";
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_RIGHT,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "apply_destination_audition";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = feedback && std::strstr(feedback, "cancelled")
                ? "cancel_destination_audition"
                : "cancel_destination_picker";
        }
        return true;
    }

    if (source == nullptr) {
        out.mode = "project.modulators";
        out.target = "add_source";
        out.targetIndex = navigation_.focusedRow.get();
        out.property = "new_source";
        out.operationStatus = "explicit_selection";
        if (isButton(
                event,
                Config::ButtonID::NAV,
                oc::core::input::ButtonBindingType::RELEASE
            )) {
            out.effect = "open_source_kind_picker";
        } else if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = navigation_.physicalHoldActive.get()
                ? "open_modulators_tab" : "focus_source";
        }
        return true;
    }

    const uint16_t destinationCount =
        core::state::project::modulators::sourceDestinationCount(
            graph,
            source->id
        );
    out.mappingIndex = static_cast<int16_t>(source->id.value);
    out.mappingCount = static_cast<int16_t>(destinationCount);
    out.source = sourceKind(*source);
    out.winner = source->name.data();
    out.routePolicy = "project";
    if (!sourceAudition) {
        out.operationStatus =
            (source->flags &
             core::state::modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U
            ? "enabled" : "disabled";
    }

    if (node == ProjectNodeId::MODULATORS_ROOT) {
        out.mode = "project.modulators";
        out.target = "modulator";
        out.targetIndex = navigation_.focusedRow.get();
        out.property = source->name.data();
        formatSourcePrimary(out.valueLabel, control, *source);
    } else if (node == ProjectNodeId::MODULATOR_TRIGGER) {
        const auto* route =
            core::state::modulation::findProjectModulationTriggerForSource(
                graph,
                source->id
            );
        const uint8_t row = navigation_.focusedRow.get();
        out.mode = "project.modulator_trigger";
        out.target = "trigger_route";
        out.targetIndex = row;
        constexpr const char* properties[]{
            "track",
            "note_low",
            "note_high",
            "velocity_low",
            "velocity_high",
        };
        out.property = row < 5U ? properties[row] : "invalid";
        if (sourceSession.existingAudition()) {
            out.operationStatus = "shared_read_only";
        }
        if (route) {
            const auto& trigger = route->trigger;
            if (row == 0U) {
                std::snprintf(
                    out.valueLabel,
                    sizeof(out.valueLabel),
                    "%u",
                    static_cast<unsigned>(trigger.track + 1U)
                );
            } else {
                const uint8_t value = row == 1U
                    ? trigger.noteMin
                    : (row == 2U
                        ? trigger.noteMax
                        : (row == 3U
                            ? route->velocityMin
                            : route->velocityMax));
                std::snprintf(
                    out.valueLabel,
                    sizeof(out.valueLabel),
                    "%u",
                    static_cast<unsigned>(value)
                );
            }
        }
    } else if (node == ProjectNodeId::MODULATOR_DESTINATIONS) {
        out.mode = "project.modulator_destinations";
        if (binding == nullptr) {
            out.target = "add_destination";
            out.targetIndex = navigation_.focusedRow.get();
            out.property = "destination";
            out.operationStatus = "explicit_selection";
        } else {
            out.target = "modulator_assignment";
            out.targetIndex = navigation_.focusedRow.get();
            out.targetTrack = binding->destination.track;
            out.targetKind = "macro";
            out.property = "depth";
            formatBindingDepth(out.valueLabel, control, *binding);
            out.operationStatus =
                (binding->flags & core::state::modulation::
                     PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U
                ? "enabled" : "disabled";
        }
    } else {
        const bool options = node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
        const bool rename = node == ProjectNodeId::MODULATOR_SOURCE_RENAME;
        out.mode = rename
            ? "project.modulator_rename"
            : (options ? "project.modulator_options"
                       : "project.modulator_detail");
        out.target = "modulator";
        out.targetIndex = navigation_.focusedRow.get();
        const auto layout = core::state::project::modulators::
            sourceWorkspaceLayout(source->kind, options, sourceAudition);
        const auto item = layout.at(navigation_.focusedRow.get());
        out.property = rename ? "name" : detailProperty(item);
        if (sourceSession.existingAudition() &&
            item != SourceDetailItem::DEPTH) {
            out.operationStatus = "shared_read_only";
        }
        if (item == SourceDetailItem::RECORD) {
            const auto& capture = macro_ui_.recordedShapeCapture;
            const bool matchingCapture = capture.active() &&
                capture.mode == core::state::modulation::
                    ProjectRecordedShapeCaptureMode::REPLACE_EXISTING &&
                capture.sourceId == source->id;
            const char* status = matchingCapture
                ? recordedShapeOperationStatus(capture.status)
                : recordedShapeStatusFromFeedback(feedback);
            std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s",
                          matchingCapture
                              ? (capture.status == core::state::modulation::
                                        ProjectRecordedShapeCaptureStatus::ARMED
                                     ? "Armed"
                                     : "Recording")
                              : "HOLD + OPT");
            if (!sourceSession.existingAudition()) {
                out.operationStatus = status ? status : "idle";
            }
            if (!matchingCapture && status != nullptr) {
                out.outcome = feedbackOutcome(feedback);
            }
        } else if (item == SourceDetailItem::DESTINATIONS) {
            std::snprintf(
                out.valueLabel,
                sizeof(out.valueLabel),
                "%u",
                static_cast<unsigned>(destinationCount)
            );
        } else if (item == SourceDetailItem::DEPTH && auditionBinding != nullptr) {
            formatBindingDepth(out.valueLabel, control, *auditionBinding);
        } else {
            formatSourcePrimary(out.valueLabel, control, *source);
        }
    }

    const bool recordedShapeRecord = source->kind ==
            core::state::modulation::ModulatorKind::RECORDED_SHAPE &&
        out.property != nullptr && std::strcmp(out.property, "record") == 0;
    const auto& recordedShapeCapture = macro_ui_.recordedShapeCapture;
    const bool matchingRecordedShapeCapture = recordedShapeRecord &&
        recordedShapeCapture.active() &&
        recordedShapeCapture.mode == core::state::modulation::
            ProjectRecordedShapeCaptureMode::REPLACE_EXISTING &&
        recordedShapeCapture.sourceId == source->id;

    if (recordedShapeRecord && isButton(
            event,
            Config::ButtonID::BOTTOM_LEFT,
            oc::core::input::ButtonBindingType::PRESS
        )) {
        recorded_shape_capture_button_seen_ = true;
        out.effect = "arm_project_recorded_shape";
    } else if (recordedShapeRecord &&
               isEncoder(event, Config::EncoderID::OPT)) {
        if (recorded_shape_capture_button_seen_ ||
            matchingRecordedShapeCapture) {
            out.effect = "record_project_recorded_shape_value";
            out.outcome = recordedShapeOutcome(recordedShapeCapture.status);
        } else {
            out.effect = "show_project_recorded_shape_record_hint";
            out.outcome = "ignored";
            out.reason = "hold_bottom_left";
        }
    } else if (recordedShapeRecord && isButton(
                   event,
                   Config::ButtonID::LEFT_TOP,
                   oc::core::input::ButtonBindingType::RELEASE
               ) && (recorded_shape_capture_button_seen_ ||
                     matchingRecordedShapeCapture)) {
        out.effect = "cancel_project_recorded_shape";
        out.outcome = "cancelled";
    } else if (recordedShapeRecord && isButton(
                   event,
                   Config::ButtonID::BOTTOM_LEFT,
                   oc::core::input::ButtonBindingType::RELEASE
               ) && recorded_shape_capture_button_seen_) {
        const bool cancelled = recordedShapeCapture.status ==
            core::state::modulation::
                ProjectRecordedShapeCaptureStatus::CANCELLED;
        const bool touched = recordedShapeCapture.take != nullptr &&
            recordedShapeCapture.take->touched;
        const bool commitAttempted = touched ||
            recordedShapeCapture.status == core::state::modulation::
                ProjectRecordedShapeCaptureStatus::COMMITTED ||
            recordedShapeCapture.status == core::state::modulation::
                ProjectRecordedShapeCaptureStatus::INVALIDATED ||
            recordedShapeCapture.status == core::state::modulation::
                ProjectRecordedShapeCaptureStatus::COMMIT_FAILED;
        out.effect = cancelled
            ? "cancel_project_recorded_shape"
            : (commitAttempted
                   ? "commit_project_recorded_shape"
                   : "discard_empty_project_recorded_shape");
        if (cancelled) out.outcome = "cancelled";
        recorded_shape_capture_button_seen_ = false;
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        if (navigation_.physicalHoldActive.get()) {
            out.effect = "open_modulators_tab";
        } else if (node == ProjectNodeId::MODULATOR_TRIGGER) {
            out.effect = "focus_trigger_field";
        } else {
            out.effect = "focus_modulator_item";
        }
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        if (sourceSession.audition() &&
            ((node == ProjectNodeId::MODULATOR_TRIGGER &&
              !sourceSession.allows(core::state::modulation::
                  ProjectModulatorSourceSessionCapability::EDIT_TRIGGER)) ||
             (node != ProjectNodeId::MODULATOR_TRIGGER &&
              out.property != nullptr &&
              std::strcmp(out.property, "depth") != 0 &&
              !sourceSession.allows(core::state::modulation::
                  ProjectModulatorSourceSessionCapability::EDIT_SOURCE)))) {
            out.effect = "inspect_shared_source_read_only";
            out.outcome = "blocked";
        } else if (node == ProjectNodeId::MODULATOR_TRIGGER) {
            out.effect = "edit_trigger_route";
        } else {
            out.effect = (binding ||
                          (sourceAudition && auditionBinding != nullptr &&
                           out.property != nullptr &&
                           std::strcmp(out.property, "depth") == 0))
                ? "edit_modulator_depth"
                                 : "edit_modulator_property";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::NAV,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        if (node == ProjectNodeId::MODULATORS_ROOT) {
            out.effect = "open_modulator_detail";
        } else if (node == ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
                   node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS) {
            if (node == ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
                out.property && std::strcmp(out.property, "record") == 0) {
                out.effect = "show_project_recorded_shape_record_hint";
            } else if (out.property &&
                       std::strcmp(out.property, "details") == 0) {
                out.effect = "open_modulator_options";
            } else if (out.property &&
                       std::strcmp(out.property, "destinations") == 0) {
                out.effect = "open_modulator_destinations";
            } else if (out.property &&
                       std::strcmp(out.property, "trigger") == 0) {
                out.effect = "open_modulator_trigger";
            } else if (out.property &&
                       std::strcmp(out.property, "rename") == 0) {
                out.effect = "open_modulator_rename";
            } else if (
                source->kind == core::state::modulation::ModulatorKind::ADSR &&
                isEnvelopeTimeProperty(out.property) &&
                core::state::modulation::modulatorAdsrTiming(
                    source->parameters.adsr.traits
                ) == core::state::modulation::ModulatorTimingMode::SYNC
            ) {
                if (sourceSession.allows(core::state::modulation::
                        ProjectModulatorSourceSessionCapability::EDIT_SOURCE)) {
                    out.effect = "cycle_modulator_feel";
                } else {
                    out.effect = "inspect_shared_source_read_only";
                    out.outcome = "blocked";
                }
            } else {
                out.effect = "inspect_modulator_property";
            }
        } else {
            out.effect = binding ? "open_macro_destination"
                                 : "open_destination_picker";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_TOP,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        out.effect = sourceAudition &&
                node == ProjectNodeId::MODULATOR_SOURCE_DETAIL
            ? "cancel_destination_audition"
            : "back_modulator_context";
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_LEFT,
                   oc::core::input::ButtonBindingType::PRESS
               )) {
        out.effect = binding ? "press_assignment_toggle"
                             : "press_modulator_toggle";
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_LEFT,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        const auto guard = navigation_.modulatorGuard.get();
        const bool toggled = feedback &&
            (std::strcmp(feedback, "Source On") == 0 ||
             std::strcmp(feedback, "Source Off") == 0 ||
             std::strcmp(feedback, "Destination On") == 0 ||
             std::strcmp(feedback, "Destination Off") == 0);
        if (toggled ||
            guard.phase == core::state::contextual::GuardedActionPhase::PRESSED) {
            out.effect = binding ? "toggle_modulator_assignment"
                                 : "toggle_modulator";
        } else {
            out.effect = binding ? "remove_modulator_assignment"
                                 : "delete_modulator";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_RIGHT,
                   oc::core::input::ButtonBindingType::PRESS
               )) {
        out.effect = node == ProjectNodeId::MODULATOR_DESTINATIONS
            ? "press_make_modulator_independent"
            : (clipboard_.hasProjectModulatorSource()
                ? "press_source_copy_or_paste" : "press_source_copy");
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_RIGHT,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        if (sourceAudition) {
            out.effect = "apply_destination_audition";
            return true;
        }
        if (node == ProjectNodeId::MODULATOR_DESTINATIONS && feedback &&
            (std::strcmp(feedback, "Destination applied") == 0 ||
             std::strcmp(feedback, "Applied - one Undo") == 0)) {
            out.effect = "apply_destination_audition";
        } else if (node == ProjectNodeId::MODULATOR_DESTINATIONS) {
            out.effect = "make_modulator_independent";
        } else {
            const auto guard = navigation_.modulatorClipboardGuard.get();
            out.effect = guard.phase ==
                    core::state::contextual::GuardedActionPhase::PRESSED
                ? "copy_modulator_source" : "paste_modulator_source";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_CENTER,
                   oc::core::input::ButtonBindingType::RELEASE
               ) && node == ProjectNodeId::MODULATORS_ROOT) {
        out.effect = "open_modulators_tab";
    }
    return true;
}

}  // namespace core::context::standalone::ux

#endif

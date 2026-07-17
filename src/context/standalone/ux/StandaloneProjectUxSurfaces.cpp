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
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"

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
    return source.kind == core::state::modulation::ModulatorKind::LFO
        ? "lfo" : "recorded_shape";
}

FLASHMEM const char* reachName(
    core::state::modulation::ModulatorReachKind reach
) {
    using core::state::modulation::ModulatorReachKind;
    switch (reach) {
        case ModulatorReachKind::MACRO: return "macro";
        case ModulatorReachKind::TRACK_SET: return "track_set";
        case ModulatorReachKind::PROJECT: return "project";
        case ModulatorReachKind::DETACHED:
        default: return "detached";
    }
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
        case SourceDetailItem::LENGTH: return "length";
        case SourceDetailItem::SOURCE_DOMAIN: return "domain";
        case SourceDetailItem::REACH: return "reach";
        case SourceDetailItem::DESTINATIONS: return "destinations";
    }
    return "source";
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
                core::ui::macro::lfo_audition::rateLabel(
                    core::ui::macro::lfo_audition::rateIndex(lfo.periodTicks)
                )
            );
        }
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
    const ModulationBindingState& binding
) {
    const int16_t percent = core::ui::macro::lfo_audition::depthQ15ToPercent(
        binding.amountQ15
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
        std::strncmp(feedback, "Split ", 6U) == 0 ||
        std::strncmp(feedback, "Reach · ", 8U) == 0 ||
        std::strcmp(feedback, "Destination removed") == 0 ||
        std::strncmp(feedback, "Pasted ", 7U) == 0 ||
        std::strncmp(feedback, "Copied ", 7U) == 0) {
        return "applied";
    }
    if (std::strstr(feedback, "failed") != nullptr ||
        std::strstr(feedback, "unavailable") != nullptr) {
        return "rejected";
    }
    return nullptr;
}

}  // namespace

FLASHMEM ProjectModulatorsUxSurface::ProjectModulatorsUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::project::ProjectNavigationState& navigation,
    core::state::macro::MacroPagesState& pages,
    core::state::StructureClipboardState& clipboard,
    core::state::macro::MacroHistoryService& history
)
    : active_view_(activeView),
      navigation_(navigation),
      pages_(pages),
      clipboard_(clipboard),
      history_(history) {}

FLASHMEM bool ProjectModulatorsUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::PROJECT ||
        navigation_.activeTab.get() !=
            core::state::project::ProjectTab::MODULATORS) {
        return false;
    }

    const auto node = navigation_.currentNode.get();
    if (node != ProjectNodeId::MODULATORS_ROOT &&
        node != ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
        node != ProjectNodeId::MODULATOR_REACH &&
        node != ProjectNodeId::MODULATOR_DESTINATIONS &&
        node != ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return false;
    }

    const auto& control = pages_.control;
    const auto& graph = control.authored.modulation;
    const auto* source = focusedSource(navigation_, graph);
    const auto* binding = focusedBinding(navigation_, graph);
    const char* feedback = navigation_.lifecycleFeedback.empty()
        ? nullptr : navigation_.lifecycleFeedback.get();

    out.projection = "committed";
    out.hasOperationGeneration = history_.undoCount() > 0U;
    out.operationGeneration = history_.undoCount();
    out.outcome = feedbackOutcome(feedback);

    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        out.mode = "project.modulator_destination_picker";
        out.target = "macro_destination";
        out.targetIndex = navigation_.focusedRow.get();
        out.sourceTrack = navigation_.destinationPickerTrack;
        out.targetTrack = navigation_.destinationPickerTrack;
        out.property = navigation_.creatingModulatorSource
            ? "new_lfo_destination" : "destination";
        out.operationStatus = navigation_.creatingModulatorSource
            ? "creating_source" : "adding_destination";
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_destination";
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = navigation_.creatingModulatorSource
                ? "create_lfo_assignment" : "add_modulator_destination";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "cancel_destination_picker";
        }
        return true;
    }

    if (source == nullptr) {
        out.mode = "project.modulators";
        out.target = "add_source";
        out.targetIndex = navigation_.focusedRow.get();
        out.property = "new_lfo";
        out.operationStatus = "explicit_selection";
        if (isButton(
                event,
                Config::ButtonID::NAV,
                oc::core::input::ButtonBindingType::RELEASE
            )) {
            out.effect = "open_source_destination_picker";
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
    out.routePolicy = reachName(source->reach.kind);
    out.operationStatus =
        (source->flags & core::state::modulation::PROJECT_MODULATOR_FLAG_ENABLED)
            != 0U
        ? "enabled" : "disabled";

    if (node == ProjectNodeId::MODULATORS_ROOT) {
        out.mode = "project.modulators";
        out.target = "modulator";
        out.targetIndex = navigation_.focusedRow.get();
        out.property = source->name.data();
        formatSourcePrimary(out.valueLabel, control, *source);
    } else if (node == ProjectNodeId::MODULATOR_REACH) {
        const auto layout =
            core::state::project::modulators::sourceReachChoiceLayout(
                graph,
                source->id
            );
        const auto choice = layout.at(navigation_.focusedRow.get());
        using core::state::project::modulators::ReachChoiceKind;
        out.mode = "project.modulator_reach";
        out.target = "modulator_reach";
        out.targetIndex = navigation_.focusedRow.get();
        if (choice.kind == ReachChoiceKind::TIGHTEST) {
            out.property = "tightest";
            std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s", "minimum");
        } else if (choice.kind == ReachChoiceKind::PROJECT) {
            out.property = "project";
            std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s", "all");
        } else {
            out.property = "split_track";
            out.targetTrack = choice.track;
            out.mappingCount = static_cast<int16_t>(choice.destinationCount);
            std::snprintf(
                out.valueLabel,
                sizeof(out.valueLabel),
                "T%u x%u",
                static_cast<unsigned>(choice.track + 1U),
                static_cast<unsigned>(choice.destinationCount)
            );
        }
        out.operationStatus = "explicit_selection";
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
            formatBindingDepth(out.valueLabel, *binding);
            out.operationStatus =
                (binding->flags & core::state::modulation::
                     PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U
                ? "enabled" : "disabled";
        }
    } else {
        out.mode = "project.modulator_detail";
        out.target = "modulator";
        out.targetIndex = navigation_.focusedRow.get();
        const auto layout =
            core::state::project::modulators::sourceDetailLayout(source->kind);
        const auto item = layout.at(navigation_.focusedRow.get());
        out.property = detailProperty(item);
        if (item == SourceDetailItem::REACH) {
            std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s", out.routePolicy);
        } else if (item == SourceDetailItem::DESTINATIONS) {
            std::snprintf(
                out.valueLabel,
                sizeof(out.valueLabel),
                "%u",
                static_cast<unsigned>(destinationCount)
            );
        } else {
            formatSourcePrimary(out.valueLabel, control, *source);
        }
    }

    if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = navigation_.physicalHoldActive.get()
            ? "open_modulators_tab"
            : (node == ProjectNodeId::MODULATOR_REACH
                   ? "focus_reach_choice" : "focus_modulator_item");
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        if (node != ProjectNodeId::MODULATOR_REACH) {
            out.effect = binding ? "edit_modulator_depth"
                                 : "edit_modulator_property";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::NAV,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        if (node == ProjectNodeId::MODULATORS_ROOT) {
            out.effect = feedback && std::strncmp(feedback, "Split ", 6U) == 0
                ? "split_modulator_by_track" : "open_modulator_detail";
        } else if (node == ProjectNodeId::MODULATOR_REACH) {
            out.effect = out.property &&
                    std::strcmp(out.property, "split_track") == 0
                ? "split_modulator_by_track" : "apply_modulator_reach";
        } else if (node == ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
            out.effect = navigation_.focusedRow.get() == 0U
                ? "open_modulator_detail"
                : (out.property && std::strcmp(out.property, "destinations") == 0
                       ? "open_modulator_destinations"
                       : (out.property && std::strcmp(out.property, "reach") == 0
                              ? "open_modulator_reach"
                              : "inspect_modulator_property"));
        } else {
            out.effect = binding ? "focus_modulator_assignment"
                                 : "open_destination_picker";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_TOP,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        out.effect = "back_modulator_context";
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
        out.effect = clipboard_.hasProjectModulatorSource()
            ? "press_source_copy_or_paste" : "press_source_copy";
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_RIGHT,
                   oc::core::input::ButtonBindingType::RELEASE
               )) {
        const auto guard = navigation_.modulatorClipboardGuard.get();
        out.effect = guard.phase ==
                core::state::contextual::GuardedActionPhase::PRESSED
            ? "copy_modulator_source" : "paste_modulator_source";
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

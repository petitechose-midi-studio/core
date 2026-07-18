#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/macro/MacroInteractionContextBuilder.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"
#include "validation/ux/SemanticUxTraceState.hpp"

namespace core::context::standalone::ux {
namespace {

namespace detail_ui = core::ui::macro;

FLASHMEM const char* modulatorSourceLabel(
    core::state::modulation::ModulatorKind kind
) {
    using Kind = core::state::modulation::ModulatorKind;
    switch (kind) {
        case Kind::LFO: return "lfo";
        case Kind::ADSR: return "adsr";
        case Kind::RECORDED_SHAPE:
        default: return "recorded_shape";
    }
}

FLASHMEM detail_ui::MacroSourceDetailContext macroSourceDetailContext(
    const core::state::modulation::ProjectControlMacroSlotView& slot,
    bool manualOverride
) {
    return {
        .automationStored = slot.automationStored,
        .modulationStored = slot.modulationStored,
        .automationPlayback = slot.automationEnabled,
        .modulationPlayback = slot.activeModulationCount > 0U,
        .manualOverride = manualOverride,
    };
}

FLASHMEM const char* macroSourceLabel(
    const detail_ui::MacroSourceDetailContext& context
) {
    if (context.manualOverride && context.modulationPlayback) {
        return "manual_modulation";
    }
    if (context.manualOverride) return "manual";
    if (context.automationPlayback && context.modulationPlayback) {
        return "auto_mod";
    }
    if (context.automationPlayback) return "automation";
    if (context.modulationPlayback) return "modulation";
    return "macro_static";
}

FLASHMEM core::state::shared::MidiCcCandidateClass macroCandidateClass(
    const detail_ui::MacroSourceDetailContext& context
) {
    if (context.manualOverride) {
        return core::state::shared::MidiCcCandidateClass::LIVE_MANUAL;
    }
    if (context.automationPlayback || context.modulationPlayback) {
        return core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED;
    }
    return core::state::shared::MidiCcCandidateClass::MACRO_STATIC;
}

FLASHMEM bool isMacroEncoderTurn(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           Config::macroEncoderIndex(event.encoderId, index);
}

FLASHMEM bool isMacroButtonLongPress(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::LONG_PRESS &&
           Config::macroButtonIndex(event.buttonId, index);
}

FLASHMEM bool isMacroButtonPress(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::PRESS &&
           Config::macroButtonIndex(event.buttonId, index);
}

FLASHMEM bool isMacroButtonRelease(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::RELEASE &&
           Config::macroButtonIndex(event.buttonId, index);
}

FLASHMEM bool isButton(const oc::core::input::InputBindingTraceEvent& event,
              Config::ButtonID button,
              oc::core::input::ButtonBindingType type) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonId == static_cast<oc::type::ButtonID>(button) &&
           event.buttonType == type;
}

FLASHMEM bool isEncoder(const oc::core::input::InputBindingTraceEvent& event, Config::EncoderID encoder) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           event.encoderId == static_cast<oc::type::EncoderID>(encoder);
}

FLASHMEM void copyValueLabel(char (&out)[16], const char* value) {
    if (!value) return;
    std::snprintf(out, sizeof(out), "%s", value);
}

FLASHMEM void copyIndexLabel(char (&out)[16], unsigned value) {
    std::snprintf(out, sizeof(out), "%u", value + 1U);
}

FLASHMEM const char* structureTarget(core::state::StructureNavigationFocus focus) {
    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK:
            return "track";
        case core::state::StructureNavigationFocus::STEP:
            return "macro";
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return "page";
    }
}

FLASHMEM const char* structureTarget(core::state::StructureSelectionScope scope) {
    switch (scope) {
        case core::state::StructureSelectionScope::TRACK:
            return "track";
        case core::state::StructureSelectionScope::PAGE:
        default:
            return "page";
    }
}

FLASHMEM void fillSelectedItem(core::validation::ux::SemanticUxContext& out,
                      const core::context::standalone::macro_overlay_presenter::SelectorRenderData& data) {
    out.targetIndex = static_cast<int16_t>(data.selectedIndex);
    if (data.items && data.selectedIndex >= 0 && data.selectedIndex < data.itemCount) {
        out.property = data.items[data.selectedIndex];
        copyValueLabel(out.valueLabel, data.items[data.selectedIndex]);
    }
}

FLASHMEM bool isAddSlot(const core::validation::ux::SemanticUxContext& out) {
    return out.property && std::strcmp(out.property, "add_slot") == 0;
}

FLASHMEM void markNoop(core::validation::ux::SemanticUxContext& out, const char* reason) {
    out.outcome = "noop";
    out.reason = reason;
}

FLASHMEM void markIgnored(core::validation::ux::SemanticUxContext& out, const char* reason) {
    out.effect = "release_ignored";
    out.outcome = "ignored";
    out.reason = reason;
}

FLASHMEM const char* contextualReasonName(
    core::state::contextual::ContextActionReason reason
) {
    switch (reason) {
        case core::state::contextual::ContextActionReason::NONE:
            return nullptr;
        case core::state::contextual::ContextActionReason::NO_ACTION:
            return "no_action";
        case core::state::contextual::ContextActionReason::EMPTY_SELECTION:
            return "empty_selection";
        case core::state::contextual::ContextActionReason::MINIMUM_CARDINALITY:
            return "minimum_cardinality";
        case core::state::contextual::ContextActionReason::EMPTY_CLIPBOARD:
            return "empty_clipboard";
        case core::state::contextual::ContextActionReason::WRONG_PAYLOAD:
            return "wrong_payload";
        case core::state::contextual::ContextActionReason::INVALID_PAYLOAD:
            return "invalid_payload";
        case core::state::contextual::ContextActionReason::ADAPTED:
            return "adapted";
        case core::state::contextual::ContextActionReason::DEFAULTED:
            return "defaulted";
        case core::state::contextual::ContextActionReason::CORRUPT_ASSET:
            return "corrupt_asset";
        case core::state::contextual::ContextActionReason::UNSUPPORTED_VERSION:
            return "unsupported_version";
        case core::state::contextual::ContextActionReason::STALE_TARGET:
            return "stale_target";
        case core::state::contextual::ContextActionReason::SAME_SOURCE_TARGET:
            return "same_source_target";
        case core::state::contextual::ContextActionReason::OUT_OF_RANGE:
            return "out_of_range";
        case core::state::contextual::ContextActionReason::CAPACITY:
            return "capacity";
        case core::state::contextual::ContextActionReason::PENDING:
            return "pending";
        case core::state::contextual::ContextActionReason::NO_ROUTE:
            return "no_route";
        case core::state::contextual::ContextActionReason::INCOMPATIBLE:
            return "incompatible";
        case core::state::contextual::ContextActionReason::HISTORY_UNAVAILABLE:
            return "history_unavailable";
        case core::state::contextual::ContextActionReason::STORAGE_UNAVAILABLE:
            return "storage_unavailable";
        case core::state::contextual::ContextActionReason::ALLOCATION_UNAVAILABLE:
            return "allocation_unavailable";
        case core::state::contextual::ContextActionReason::CONFLICT:
            return "conflict";
        case core::state::contextual::ContextActionReason::READ_ONLY:
            return "read_only";
        case core::state::contextual::ContextActionReason::TRANSPORT_STATE:
            return "transport_state";
        case core::state::contextual::ContextActionReason::FAILED:
            return "failed";
        default:
            return "blocked";
    }
}

FLASHMEM void fillGuardedMacroFeedback(
    core::validation::ux::SemanticUxContext& out,
    const core::state::MacroEditState& edit
) {
    const auto feedback = edit.contextFeedback.get();
    if (!feedback.active) return;

    using Status = core::state::contextual::OperationFeedbackStatus;
    switch (feedback.status) {
        case Status::PREVIEW:
            out.outcome = "preview";
            break;
        case Status::PRESSED:
            out.outcome = "pressed";
            break;
        case Status::ARMED:
            out.outcome = "armed";
            break;
        case Status::QUEUED:
            out.outcome = "queued";
            break;
        case Status::APPLIED:
            out.outcome = "applied";
            break;
        case Status::CANCELLED:
            out.outcome = "cancelled";
            break;
        case Status::BLOCKED:
            out.outcome = "blocked";
            break;
        case Status::WARNING:
            out.outcome = "warning";
            break;
        case Status::CONFLICT:
            out.outcome = "conflict";
            break;
        case Status::FAILED:
            out.outcome = "failed";
            break;
        case Status::NONE:
        default:
            break;
    }
    if (const char* reason = contextualReasonName(feedback.reason)) {
        out.reason = reason;
    }
}

FLASHMEM const char* candidateClassName(
    core::state::shared::MidiCcCandidateClass candidateClass
) {
    switch (candidateClass) {
        case core::state::shared::MidiCcCandidateClass::LIVE_MANUAL:
            return "live_manual";
        case core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE:
            return "cc_lane";
        case core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED:
            return "macro_computed";
        case core::state::shared::MidiCcCandidateClass::MACRO_STATIC:
        default:
            return "macro_static";
    }
}

FLASHMEM void fillMacroResolutionFacts(
    core::validation::ux::SemanticUxContext& out,
    const core::handler::MidiCcGlobalFrameCoordinator* coordinator,
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex,
    core::state::shared::MidiCcCandidateClass localClass
) {
    out.routePolicy = "live_manual>cc_lane>macro_computed>macro_static";
    if (coordinator == nullptr) return;
    const auto telemetryView = coordinator->readTelemetry();
    if (!telemetryView) return;
    const auto& telemetry = *telemetryView;
    const uint16_t address = static_cast<uint16_t>(
        (static_cast<uint16_t>(pages.currentActiveTrack()) *
             core::state::macro::PAGE_COUNT +
         pages.currentActivePage()) *
            core::state::macro::MACRO_COUNT +
        macroIndex
    );
    const size_t destinationCount = telemetry.destinationCount <
            telemetry.destinations.size()
        ? telemetry.destinationCount
        : telemetry.destinations.size();
    for (size_t i = 0; i < destinationCount; ++i) {
        const auto& destination = telemetry.destinations[i];
        bool local = destination.winner.author.candidateClass == localClass &&
            destination.winner.author.stableAddress == address;
        const size_t firstLoser = destination.firstLoser < telemetry.losers.size()
            ? destination.firstLoser
            : telemetry.losers.size();
        const size_t availableLosers = telemetry.losers.size() - firstLoser;
        const size_t loserCount = destination.loserCount < availableLosers
            ? destination.loserCount
            : availableLosers;
        for (size_t loser = 0; !local && loser < loserCount; ++loser) {
            const auto& candidate = telemetry.losers[firstLoser + loser];
            local = candidate.author.candidateClass == localClass &&
                candidate.author.stableAddress == address;
        }
        if (!local) continue;
        // Keep an explicit interaction projection (silent selection, audition,
        // preview) authoritative. Runtime resolution facts complement that UX
        // state; they must not relabel it as a classic-CC projection.
        if (out.projection == nullptr) {
            out.projection = "resolved_classic_cc";
        }
        out.winner = candidateClassName(destination.winner.author.candidateClass);
        out.hasConflict = true;
        out.conflict = destination.conflict;
        out.hasResolvedValue = true;
        out.resolvedValue = destination.finalValue;
        out.hasTargetRoute = true;
        out.targetRoute = destination.destination.identity.channel;
        out.targetRouteValid =
            destination.destination.routeValidity ==
            core::state::shared::MidiCcRouteValidity::VALID;
        return;
    }
}

FLASHMEM const char* guardedMacroEffect(
    const core::state::MacroEditState& edit,
    const char* copyEffect
) {
    const auto feedback = edit.contextFeedback.get();
    if (!feedback.active) return copyEffect;
    switch (feedback.action) {
        case core::state::contextual::ContextActionId::PASTE:
            return "paste";
        case core::state::contextual::ContextActionId::OVERWRITE:
            return "overwrite";
        case core::state::contextual::ContextActionId::REMOVE:
            return "remove";
        default:
            return copyEffect;
    }
}

}  // namespace

FLASHMEM MacroValueUxSurface::MacroValueUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::MacroState& macros,
    core::state::macro::MacroPagesState& pages,
    core::state::macro::MacroUiState& macroUi,
    core::state::MacroEditState& macroEdit
) : active_view_(activeView),
    macros_(macros),
    pages_(pages),
    macro_ui_(macroUi),
    macro_edit_(macroEdit) {}

FLASHMEM bool MacroValueUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get()) {
        return false;
    }

    uint8_t index = 0;
    if (!isMacroEncoderTurn(event, index) || index >= Config::MACRO_COUNT) {
        return false;
    }

    const auto& take = macro_ui_.automationTake;
    if (take.phase != core::state::macro::MacroAutomationTakePhase::IDLE &&
        take.track == pages_.currentActiveTrack() &&
        take.page == pages_.currentActivePage() &&
        (take.candidateMask & static_cast<uint16_t>(1U << index)) != 0U) {
        out.mode = "macro.automation_take";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(index);
        out.property = "Automation";
        out.effect = "record_macro_automation_take_value";
        copyValueLabel(out.valueLabel, macros_.slots[index].displayValue.get());
    } else {
        out.mode = "macro";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(index);
        out.property = "Base";
        out.effect = "edit_macro_base";
        copyValueLabel(out.valueLabel, macros_.slots[index].displayValue.get());
    }
    const auto sourceAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_.currentActiveTrack(),
        .page = pages_.currentActivePage(),
        .macro = index,
    };
    core::state::modulation::ProjectControlMacroSlotView sourceSlot{};
    (void)core::state::modulation::readProjectControlMacroSlot(
        pages_.control,
        sourceAddress,
        sourceSlot
    );
    const bool manualOverride =
        (macro_ui_.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << index)) != 0;
    out.source = macroSourceLabel(
        macroSourceDetailContext(sourceSlot, manualOverride)
    );
    return true;
}

FLASHMEM MacroPerformanceUxSurface::MacroPerformanceUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::macro::MacroUiState& macroUi,
    core::state::MacroEditState& macroEdit
) : active_view_(activeView), macro_ui_(macroUi), macro_edit_(macroEdit) {}

FLASHMEM bool MacroPerformanceUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get()) {
        return false;
    }

    const bool editPress = isButton(
        event,
        Config::ButtonID::LEFT_BOTTOM,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool editRelease = isButton(
        event,
        Config::ButtonID::LEFT_BOTTOM,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool takePress = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool takeRelease = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool cancel = isButton(
        event,
        Config::ButtonID::LEFT_TOP,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool timing = isEncoder(event, Config::EncoderID::NAV) &&
        macro_ui_.performanceOverlayMode.get() ==
            core::state::macro::MacroPerformanceOverlayMode::AUTOMATION_TAKE;
    if (!editPress && !editRelease && !takePress && !takeRelease &&
        !cancel && !timing) {
        return false;
    }

    out.mode = "macro.performance";
    out.targetIndex = -1;
    if (editPress || editRelease) {
        out.target = "macro_editor";
        out.property = "Edit";
        out.effect = editPress
            ? "show_macro_edit_prompt"
            : "close_macro_edit_prompt";
        copyValueLabel(out.valueLabel, editPress ? "Press Macro" : "Closed");
    } else {
        out.target = "automation_take";
        out.property = timing ? "Duration" : "Automation";
        if (takePress) {
            out.effect = "arm_macro_automation_take";
        } else if (timing) {
            out.effect = "select_macro_automation_take_duration";
        } else if (takeRelease) {
            out.effect = macro_ui_.automationTake.timing !=
                    core::state::macro::MacroAutomationTakeTiming::HOLD
                ? "continue_fixed_macro_automation_take"
                : "finish_macro_automation_take";
        } else {
            out.effect = "cancel_macro_performance_action";
        }
        copyValueLabel(
            out.valueLabel,
            core::state::macro::macroAutomationTakeTimingLabel(
                macro_ui_.automationTakeTiming.get()
            )
        );
    }
    return true;
}

FLASHMEM MacroStructureUxSurface::MacroStructureUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::StructureClipboardState& structureClipboard,
    core::state::macro::MacroUiState& macroUi,
    core::state::macro::MacroPagesState& pages,
    core::state::MacroEditState& macroEdit,
    const core::validation::ux::StructureUxTraceState* traceState
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    structure_clipboard_(structureClipboard),
    macro_ui_(macroUi),
    pages_(pages),
    macro_edit_(macroEdit),
    trace_state_(traceState) {}

FLASHMEM bool MacroStructureUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get() ||
        macro_ui_.performanceOverlayMode.get() !=
            core::state::macro::MacroPerformanceOverlayMode::NONE) {
        return false;
    }

    const bool leftTopRelease =
        isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE);
    const bool structureEvent =
        isEncoder(event, Config::EncoderID::NAV) ||
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS) ||
        leftTopRelease ||
        isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS);
    if (!structureEvent) {
        return false;
    }

    const bool selectionActive =
        track_navigation_.selection.active.get() || macro_ui_.pageSelection.active.get();
    if (leftTopRelease && !selectionActive) {
        return false;
    }

    const auto focus = navigation_focus_.get();
    auto scope = core::state::selectionScopeForFocus(focus);
    if (track_navigation_.selection.active.get()) {
        scope = track_navigation_.selection.scope.get();
    } else if (macro_ui_.pageSelection.active.get()) {
        scope = macro_ui_.pageSelection.scope.get();
    }

    out.mode = selectionActive ? "macro.structure_selection" : "macro.structure";
    out.target = selectionActive ? structureTarget(scope) : structureTarget(focus);

    uint8_t index = 0;
    const bool targetMacro =
        !selectionActive && focus == core::state::StructureNavigationFocus::STEP;
    if (targetMacro) {
        index = macro_ui_.focusedMacroSlot.get();
    }
    const bool targetTrack =
        selectionActive ? scope == core::state::StructureSelectionScope::TRACK
                        : focus == core::state::StructureNavigationFocus::TRACK;
    const uint16_t targetMask =
        targetMacro ? pages_.activePageData().activeMacroMask
                    : (targetTrack ? pages_.currentTrackEnabledMask()
                                   : pages_.currentEnabledPageMask());
    const bool canPaste = targetMacro ? (index < core::state::macro::MACRO_COUNT &&
                                         structure_clipboard_.hasMacroSlot())
                        : (targetTrack ? structure_clipboard_.hasMacroTrack()
                                       : structure_clipboard_.hasMacroPage());
    out.targetMask = targetMask;

    if (targetMacro) {
        out.property = pages_.isMacroAddSlot(index) ? "add_slot" : "existing";
    } else if (targetTrack) {
        index = track_navigation_.selection.active.get()
            ? track_navigation_.selection.cursorIndex.get()
            : track_navigation_.previewTrackIndex.get();
        out.property = track_navigation_.previewAddSlot.get() && !selectionActive
            ? "add_slot"
            : (selectionActive ? "selection" : "existing");
    } else {
        index = macro_ui_.pageSelection.active.get()
            ? macro_ui_.pageSelection.cursorIndex.get()
            : macro_ui_.previewPageIndex.get();
        out.property = macro_ui_.previewAddPageSlot.get() && !selectionActive
            ? "add_slot"
            : (selectionActive ? "selection" : "existing");
    }
    out.targetIndex = static_cast<int16_t>(index);
    copyIndexLabel(out.valueLabel, index);

    if (selectionActive) {
        const auto interaction = core::state::macro::buildMacroInteractionContext(
            core::state::macro::MacroInteractionContextSource{
                .pages = pages_,
                .macroUi = macro_ui_,
                .trackNavigation = track_navigation_,
                .structureClipboard = structure_clipboard_,
                .navigationFocus = focus,
                .enabledTrackMask = pages_.currentTrackEnabledMask(),
                .blockingOverlay = false,
                .slotPropertySelecting = false,
            }
        );
        const auto& deleteAction = interaction.selectionDeleteAction;
        const bool deleteAvailable =
            core::state::contextual::canExecute(deleteAction.hold);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "navigate_selection";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "toggle_selection";
        } else if (leftTopRelease) {
            out.effect = "cancel_selection";
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_LEFT,
                       oc::core::input::ButtonBindingType::PRESS
                   )) {
            out.effect = "guard_delete_selection";
            out.outcome = deleteAvailable ? "pressed" : "blocked";
            out.reason = deleteAvailable
                ? nullptr
                : contextualReasonName(deleteAction.hold.reason);
            copyValueLabel(out.valueLabel, deleteAvailable ? "Pressed" : "Disabled");
            out.targetMask = deleteAction.source.item;
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_LEFT,
                       oc::core::input::ButtonBindingType::LONG_PRESS
                   )) {
            out.effect = "delete_selection";
            out.outcome = deleteAvailable ? "applied" : "blocked";
            out.reason = deleteAvailable
                ? nullptr
                : contextualReasonName(deleteAction.hold.reason);
            copyValueLabel(out.valueLabel, deleteAvailable ? "Applied" : "Disabled");
            out.targetMask = deleteAction.source.item;
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_LEFT,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "delete_selection";
            out.outcome = "cancelled";
            out.reason = "early_release";
            copyValueLabel(out.valueLabel, "Cancelled");
            out.targetMask = deleteAction.source.item;
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "duplicate_selection";
        }
        return true;
    }

    if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "preview_structure";
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = "enter_selection";
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = (focus == core::state::StructureNavigationFocus::PAGE &&
                      macro_ui_.previewAddPageSlot.get()) ||
                             (focus == core::state::StructureNavigationFocus::TRACK &&
                              track_navigation_.previewAddSlot.get())
                         ? "create_structure"
                         : "switch_structure_focus";
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = targetMacro ? "arm_remove_macro_slot" : "arm_remove";
        if (isAddSlot(out) ||
            (!targetMacro &&
             core::state::shared::countEnabled(
                 targetMask,
                 targetTrack ? core::state::macro::TRACK_COUNT
                             : core::state::macro::PAGE_COUNT
             ) <= 1U)) {
            markNoop(out, isAddSlot(out) ? "add_slot" : "single_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = targetMacro ? "cancel_remove_macro_slot" : "erase_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomLeftRelease) {
            markIgnored(out, "after_long_press");
        } else if (targetMacro) {
            out.outcome = "cancelled";
            out.reason = "early_release";
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = targetMacro ? "remove_macro_slot" : "remove_structure";
        if (targetMacro && trace_state_ &&
            trace_state_->ignoreNextBottomLeftRelease) {
            out.outcome = "applied";
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = targetMacro
            ? (canPaste ? "arm_paste_macro_slot" : "press_copy_macro_slot")
            : (canPaste ? "arm_paste" : "press_copy_structure");
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = targetMacro ? "copy_macro_slot" : "copy_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomRightRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = targetMacro ? "paste_macro_slot" : "paste_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomRightRelease) {
            out.outcome = "applied";
        } else if (!canPaste) {
            markNoop(out, "clipboard_empty");
        }
    }
    return true;
}

FLASHMEM MacroEditUxSurface::MacroEditUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::MacroEditState& macroEdit,
    core::state::macro::MacroPagesState& pages,
    core::state::macro::MacroUiState& macroUi,
    oc::state::Signal<uint32_t>& configRevision,
    core::state::StructureClipboardState& structureClipboard,
    const core::handler::MidiCcGlobalFrameCoordinator* midiCcCoordinator
) : active_view_(activeView),
    macro_edit_(macroEdit),
    pages_(pages),
    macro_ui_(macroUi),
    config_revision_(configRevision),
    structure_clipboard_(structureClipboard),
    midi_cc_coordinator_(midiCcCoordinator) {
    core::context::standalone::macro_overlay_presenter::initializeStaticItems(static_items_);
}

FLASHMEM bool MacroEditUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    uint8_t openingIndex = 0;
    const bool opening = active_view_.get() == core::ui::ViewType::MACRO &&
        isMacroButtonPress(event, openingIndex) &&
        macro_edit_.visible.get() &&
        macro_edit_.editingIndex.get() == openingIndex;
    if (!opening && !macro_edit_.visible.get()) {
        return false;
    }

    core::context::standalone::macro_overlay_presenter::Source source{
        macro_edit_,
        pages_,
        macro_ui_,
        config_revision_,
        &structure_clipboard_,
        midi_cc_coordinator_,
    };

    if (opening) {
        out.mode = "macro.edit";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(openingIndex);
        out.effect = "open_macro_edit";
        copyIndexLabel(out.valueLabel, openingIndex);
        return true;
    }

    const auto phase = macro_edit_.flowPhase.get();
    if (phase == core::state::MacroEditFlowPhase::EDIT) {
        const auto data = core::context::standalone::macro_overlay_presenter::buildEditRenderData(source);
        const int row = data.selectedIndex;
        const bool destinationRow = row == 0;
        const bool automationRow = row == 1;
        const bool modulationRow = row == 2;
        out.mode = "macro.edit";
        out.target = destinationRow
            ? "macro_destination"
            : (automationRow ? "automation" : "modulation");
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        if (row >= 0 && row < static_cast<int>(data.rows.size())) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = macro_edit_.contextSelectorActive.get()
                ? "select_macro_context_property"
                : (macro_edit_.macroCycleActive.get()
                    ? "cycle_active_macro"
                    : "focus_macro_domain");
            if (data.interactionOverlayVisible) {
                out.property = data.interactionLabel.data();
                copyValueLabel(out.valueLabel, data.interactionValue.data());
            }
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            out.effect = macro_edit_.contextSelectorActive.get()
                ? "edit_macro_context_property"
                : (destinationRow
                    ? "edit_macro_cc"
                    : (automationRow ? "edit_automation_playback"
                                     : "edit_modulation_depth"));
            if (data.interactionOverlayVisible) {
                out.property = data.interactionLabel.data();
                copyValueLabel(out.valueLabel, data.interactionValue.data());
            }
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_macro_config_value";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "close_macro_edit";
        } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = "show_active_macro_cycle";
        } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "close_active_macro_cycle";
        } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = "show_macro_context_properties";
        } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "close_macro_context_properties";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = destinationRow
                ? "arm_remove_macro_slot"
                : (automationRow ? "arm_clear_automation"
                                 : "arm_clear_modulation");
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
            const auto feedback = macro_edit_.contextFeedback.get();
            const bool applied = feedback.active &&
                feedback.status ==
                    core::state::contextual::OperationFeedbackStatus::APPLIED;
            out.effect = applied
                ? (destinationRow ? "remove_macro_slot"
                                  : (automationRow ? "clear_automation"
                                                   : "clear_modulation"))
                : (automationRow ? "toggle_automation_playback"
                                 : (modulationRow
                                        ? "toggle_modulation_playback"
                                        : "cancel_remove_macro_slot"));
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
            const bool canPaste = destinationRow
                ? structure_clipboard_.hasMacroDestination()
                : (automationRow
                       ? structure_clipboard_.hasMacroAutomation()
                       : (structure_clipboard_.hasMacroModulation() ||
                          structure_clipboard_.hasMacroModulationAssignment()));
            out.effect = canPaste
                ? (destinationRow ? "arm_paste_macro_destination"
                                  : (automationRow ? "arm_paste_automation"
                                                   : "arm_paste_modulation"))
                : (destinationRow ? "press_copy_macro_destination"
                                  : (automationRow ? "press_copy_automation"
                                                   : "press_copy_modulation"));
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            const auto feedback = macro_edit_.contextFeedback.get();
            const bool appliedModulator = modulationRow && feedback.active &&
                feedback.action ==
                    core::state::contextual::ContextActionId::APPLY &&
                feedback.status ==
                    core::state::contextual::OperationFeedbackStatus::APPLIED;
            const bool pasted = feedback.active &&
                (feedback.action == core::state::contextual::ContextActionId::PASTE ||
                 feedback.action == core::state::contextual::ContextActionId::OVERWRITE);
            out.effect = appliedModulator
                ? (feedback.source.node == 1U
                       ? "apply_existing_modulator"
                       : (feedback.source.node == 2U
                              ? "apply_adsr_audition"
                              : "apply_lfo_audition"))
                : (pasted
                ? (destinationRow ? "paste_macro_destination"
                                  : (automationRow ? "paste_automation"
                                                   : "paste_modulation"))
                : (destinationRow ? "copy_macro_destination"
                                  : (automationRow ? "copy_automation"
                                                   : "copy_modulation")));
        }
        if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            fillGuardedMacroFeedback(out, macro_edit_);
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::VALUE_SELECTOR) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildEditSelectorRenderData(
                source,
                static_items_
            );
        if (!data.visible) return false;
        out.mode = "macro.edit.selector";
        out.target = "macro_config_value";
        out.property = data.meta;
        fillSelectedItem(out, data);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_macro_config_value";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_macro_config_value";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_macro_config_value";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::PAGE_SELECTOR) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildPageSelectorRenderData(source);
        if (!data.visible) return false;
        out.mode = "macro.page_selector";
        out.target = "page";
        fillSelectedItem(out, data);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_macro_page";
        } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_macro_page";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_macro_page";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::TARGET_SELECTOR) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildTargetSelectorRenderData(
                source,
                static_items_
            );
        if (!data.visible) return false;
        out.mode = "macro.target_selector";
        out.target = "macro";
        fillSelectedItem(out, data);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_macro_target";
        } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_macro_target";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_macro_target";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::AUTOMATION) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildAutomationRenderData(source);
        if (!data.visible) return false;
        const int row = data.selectedIndex;
        const uint8_t macroIndex = macro_edit_.editingIndex.get();
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = pages_.currentActiveTrack(),
            .page = pages_.currentActivePage(),
            .macro = macroIndex,
        };
        core::state::modulation::ProjectControlMacroSlotView slot{};
        (void)core::state::modulation::readProjectControlMacroSlot(
            pages_.control,
            address,
            slot
        );
        const bool manual =
            (macro_ui_.automationManualOverrideMask.get() &
             static_cast<uint16_t>(1U << macroIndex)) != 0;
        const auto context = macroSourceDetailContext(slot, manual);
        const auto layout = detail_ui::buildAutomationDetailLayout(context);
        const auto item = layout.at(static_cast<uint8_t>(row));
        out.mode = "macro.automation_editor";
        out.target = "automation";
        out.targetIndex = static_cast<int16_t>(macroIndex);
        if (row >= 0 && row < static_cast<int>(data.rows.size())) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_macro_automation";
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            switch (item) {
                case detail_ui::AutomationDetailItem::PLAYBACK:
                    out.effect = "edit_automation_playback";
                    break;
                case detail_ui::AutomationDetailItem::LENGTH:
                    out.effect = "edit_automation_length";
                    break;
                case detail_ui::AutomationDetailItem::OFFSET:
                    out.effect = "edit_automation_offset";
                    break;
                default:
                    out.effect = "noop_read_only";
                    break;
            }
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            switch (item) {
                case detail_ui::AutomationDetailItem::RESUME:
                    out.effect = "resume_macro_automation";
                    break;
                case detail_ui::AutomationDetailItem::CONVERT_TO_MODULATION:
                    out.effect = "preview_conversion";
                    break;
                default:
                    out.effect = "activate_automation_property";
                    break;
            }
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "back_macro_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = "arm_clear_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
            const auto feedback = macro_edit_.contextFeedback.get();
            out.effect = feedback.active &&
                    feedback.status ==
                        core::state::contextual::OperationFeedbackStatus::APPLIED
                ? "clear_automation"
                : "toggle_automation_playback";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = structure_clipboard_.hasMacroAutomation()
                ? "arm_paste_automation"
                : "press_copy_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            const auto feedback = macro_edit_.contextFeedback.get();
            out.effect = feedback.active &&
                    (feedback.action == core::state::contextual::ContextActionId::PASTE ||
                     feedback.action == core::state::contextual::ContextActionId::OVERWRITE)
                ? "paste_automation"
                : "copy_automation";
        }
        if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            fillGuardedMacroFeedback(out, macro_edit_);
        }
        out.source = macroSourceLabel(context);
        fillMacroResolutionFacts(
            out,
            midi_cc_coordinator_,
            pages_,
            macroIndex,
            macroCandidateClass(context)
        );
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::MODULATION) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildAutomationRenderData(source);
        if (!data.visible) return false;
        const int row = data.selectedIndex;
        out.mode = "macro.modulation_editor";
        out.target = "modulation";
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = pages_.currentActiveTrack(),
            .page = pages_.currentActivePage(),
            .macro = macro_edit_.editingIndex.get(),
        };
        core::state::modulation::ProjectControlMacroSlotView slot{};
        (void)core::state::modulation::readProjectControlMacroSlot(
            pages_.control,
            address,
            slot
        );
        const bool manual =
            (macro_ui_.automationManualOverrideMask.get() &
             static_cast<uint16_t>(1U << address.macro)) != 0;
        const auto context = macroSourceDetailContext(slot, manual);
        if (!slot.modulationStored) {
            if (row >= 0 && row < data.rowCount) {
                out.property = data.rows[row].key;
                copyValueLabel(out.valueLabel, data.rows[row].value);
            }
            out.source = "none";
            out.projection = "silent_selection";
            if (isEncoder(event, Config::EncoderID::NAV)) {
                out.effect = "focus_modulation_creation";
            } else if (isButton(
                           event,
                           Config::ButtonID::NAV,
                           oc::core::input::ButtonBindingType::RELEASE
                       )) {
                out.effect = row == 0
                    ? "start_lfo_audition"
                    : (row == 1 ? "start_adsr_audition"
                                : "open_existing_modulator_picker");
            } else if (isButton(
                           event,
                           Config::ButtonID::LEFT_TOP,
                           oc::core::input::ButtonBindingType::RELEASE
                       )) {
                const auto feedback = macro_edit_.contextFeedback.get();
                const bool cancelledNew = feedback.active &&
                    feedback.action ==
                        core::state::contextual::ContextActionId::CANCEL &&
                    feedback.status == core::state::contextual::
                        OperationFeedbackStatus::CANCELLED;
                out.effect = cancelledNew
                    ? (feedback.source.node == 2U
                           ? "cancel_adsr_audition"
                           : "cancel_lfo_audition")
                    : "back_macro_modulation";
                if (cancelledNew) {
                    fillGuardedMacroFeedback(out, macro_edit_);
                }
            } else if (isButton(
                           event,
                           Config::ButtonID::BOTTOM_LEFT,
                           oc::core::input::ButtonBindingType::RELEASE
                       )) {
                const auto feedback = macro_edit_.contextFeedback.get();
                out.effect = feedback.active &&
                        feedback.status ==
                            core::state::contextual::OperationFeedbackStatus::APPLIED
                    ? "clear_modulation"
                    : "noop_empty_modulation";
                fillGuardedMacroFeedback(out, macro_edit_);
            } else if (isButton(
                           event,
                           Config::ButtonID::BOTTOM_RIGHT,
                           oc::core::input::ButtonBindingType::PRESS
                       )) {
                out.effect = structure_clipboard_.hasMacroModulationAssignment()
                    ? "arm_paste_modulation_assignment"
                    : "noop_empty_modulation_copy";
                fillGuardedMacroFeedback(out, macro_edit_);
            } else if (isButton(
                           event,
                           Config::ButtonID::BOTTOM_RIGHT,
                           oc::core::input::ButtonBindingType::RELEASE
                       )) {
                const auto feedback = macro_edit_.contextFeedback.get();
                const bool pasted = feedback.active &&
                    (feedback.action ==
                         core::state::contextual::ContextActionId::PASTE ||
                     feedback.action ==
                         core::state::contextual::ContextActionId::OVERWRITE) &&
                    feedback.status == core::state::contextual::
                        OperationFeedbackStatus::APPLIED;
                out.effect = pasted ? "paste_modulation_assignment"
                                    : "noop_empty_modulation_copy";
                fillGuardedMacroFeedback(out, macro_edit_);
            } else {
                out.effect = "noop_empty_modulation";
            }
            fillMacroResolutionFacts(
                out,
                midi_cc_coordinator_,
                pages_,
                address.macro,
                macroCandidateClass(context)
            );
            return true;
        }
        const auto destination =
            core::state::modulation::projectControlDestination(address);
        const uint16_t assignmentCount = slot.modulationCount;
        const int firstAssignmentRow = 1;
        const int addSourceRow = firstAssignmentRow + assignmentCount;
        const bool allRow = row == 0;
        const bool addRow = row == addSourceRow;
        const int targetOrdinal = row - firstAssignmentRow;
        const auto& graph = pages_.control.authored.modulation;
        const core::state::modulation::ModulationBindingState* binding = nullptr;
        int ordinal = 0;
        if (!allRow && !addRow && targetOrdinal >= 0) {
            for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
                const auto& candidate = graph.outputBindings[index];
                if (candidate.destination != destination) continue;
                if (ordinal++ == targetOrdinal) {
                    binding = &candidate;
                    break;
                }
            }
        }
        const auto* modulator = binding != nullptr
            ? core::state::modulation::findProjectModulator(
                  graph,
                  binding->sourceId
              )
            : nullptr;
        if (allRow) {
            out.property = "all_modulation";
            out.source = "aggregate";
            const uint16_t scaleQ15 =
                core::state::modulation::projectModulationDestinationScaleQ15(
                    graph,
                    destination
                );
            const unsigned percent = static_cast<unsigned>(std::lround(
                static_cast<float>(scaleQ15) * 100.0f /
                static_cast<float>(core::state::modulation::
                    PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15)
            ));
            std::snprintf(
                out.valueLabel,
                sizeof(out.valueLabel),
                "%u%% %s",
                percent,
                slot.activeModulationCount > 0U ? "On" : "Off"
            );
        } else if (addRow) {
            out.property = "add_source";
            out.source = "none";
            copyValueLabel(out.valueLabel, "Add");
        } else if (binding != nullptr && modulator != nullptr) {
            out.property = modulator->name.data();
            out.source = modulatorSourceLabel(modulator->kind);
            out.mappingIndex = static_cast<int16_t>(binding->id.value);
            out.mappingCount = static_cast<int16_t>(assignmentCount);
            const int depth = static_cast<int>(std::lround(
                std::clamp(
                    static_cast<float>(binding->amountQ15) / 32767.0f,
                    -1.0f,
                    1.0f
                ) * 100.0f
            ));
            std::snprintf(
                out.valueLabel,
                sizeof(out.valueLabel),
                "%+d%%",
                depth
            );
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = allRow ? "focus_all_modulation"
                : (addRow ? "focus_add_modulation_source"
                          : "focus_modulation_assignment");
            out.projection = "silent_selection";
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            out.effect = allRow ? "edit_global_modulation_depth"
                : (binding != nullptr ? "edit_modulation_depth"
                                      : "noop_read_only");
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = addRow ? "open_modulator_creation"
                : (binding != nullptr ? "open_modulator_source"
                                      : "noop_all_modulation");
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "back_macro_modulation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = allRow ? "arm_clear_all_modulation"
                : (binding != nullptr ? "arm_remove_modulation_assignment"
                                      : "noop_add_source");
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
            const auto feedback = macro_edit_.contextFeedback.get();
            const bool applied = feedback.active &&
                feedback.status == core::state::contextual::
                    OperationFeedbackStatus::APPLIED;
            out.effect = applied
                ? (allRow ? "clear_all_modulation"
                          : "remove_modulation_assignment")
                : (allRow ? "toggle_all_modulation"
                          : (binding != nullptr
                                 ? "toggle_modulation_assignment"
                                 : "noop_add_source"));
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = binding == nullptr
                ? "noop_modulation_assignment_copy"
                : (structure_clipboard_.hasMacroModulationAssignment()
                       ? "arm_paste_modulation_assignment"
                       : "press_copy_modulation_assignment");
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            const auto feedback = macro_edit_.contextFeedback.get();
            const bool pasted = feedback.active &&
                (feedback.action ==
                     core::state::contextual::ContextActionId::PASTE ||
                 feedback.action ==
                     core::state::contextual::ContextActionId::OVERWRITE) &&
                feedback.status == core::state::contextual::
                    OperationFeedbackStatus::APPLIED;
            out.effect = binding == nullptr
                ? "noop_modulation_assignment_copy"
                : (pasted ? "paste_modulation_assignment"
                          : "copy_modulation_assignment");
        }
        if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            fillGuardedMacroFeedback(out, macro_edit_);
        }
        fillMacroResolutionFacts(
            out,
            midi_cc_coordinator_,
            pages_,
            address.macro,
            macroCandidateClass(context)
        );
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::MODULATOR_CREATE) {
        const int row = std::min<int>(
            macro_edit_.modulationFocusedRow.get(),
            2
        );
        out.mode = "macro.modulator_creation";
        out.target = "modulation";
        out.targetIndex = static_cast<int16_t>(
            macro_edit_.editingIndex.get()
        );
        out.property = row == 0
            ? "new_lfo"
            : (row == 1 ? "new_adsr" : "use_existing");
        out.source = "none";
        out.projection = "silent_selection";
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_modulation_creation";
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "open_modulator_creation";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "back_modulation_assignments";
        } else {
            out.effect = "browse_modulation_creation";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::MODULATOR_PICKER) {
        const auto& graph = pages_.control.authored.modulation;
        if (graph.sourceCount == 0U) return false;
        const int selected = std::clamp(
            macro_edit_.macroSelector.selectedIndex.get(),
            0,
            static_cast<int>(graph.sourceCount) - 1
        );
        const auto& modulator = graph.sources[static_cast<uint16_t>(selected)];
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = pages_.currentActiveTrack(),
            .page = pages_.currentActivePage(),
            .macro = macro_edit_.editingIndex.get(),
        };
        out.mode = "macro.modulator_picker";
        out.target = "modulator";
        out.targetIndex = static_cast<int16_t>(selected);
        out.source = modulatorSourceLabel(modulator.kind);
        out.projection = "silent_selection";
        out.mappingIndex = static_cast<int16_t>(modulator.id.value);
        out.mappingCount = static_cast<int16_t>(graph.sourceCount);
        out.property = modulator.name.data();
        copyValueLabel(out.valueLabel, modulator.name.data());
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_existing_modulator";
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "start_existing_modulator_audition";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            const auto feedback = macro_edit_.contextFeedback.get();
            const bool cancelled = feedback.active &&
                feedback.action ==
                    core::state::contextual::ContextActionId::CANCEL &&
                feedback.status == core::state::contextual::
                    OperationFeedbackStatus::CANCELLED &&
                feedback.source.node == 1U;
            out.effect = cancelled
                ? "cancel_existing_modulator"
                : "back_modulation_creation";
            if (cancelled) fillGuardedMacroFeedback(out, macro_edit_);
        } else {
            out.effect = "browse_existing_modulator";
        }
        fillMacroResolutionFacts(
            out,
            midi_cc_coordinator_,
            pages_,
            address.macro,
            core::state::shared::MidiCcCandidateClass::MACRO_STATIC
        );
        out.projection = "silent_selection";
        return true;
    }

    if (phase ==
        core::state::MacroEditFlowPhase::EXISTING_MODULATOR_AUDITION) {
        const auto data = core::context::standalone::macro_overlay_presenter::
            buildAutomationRenderData(source);
        const auto& audition = pages_.control.audition;
        if (!data.visible || !audition.active || audition.sourceCreated) {
            return false;
        }
        const auto* modulator = core::state::modulation::findProjectModulator(
            pages_.control.authored.modulation,
            audition.sourceId
        );
        if (modulator == nullptr) return false;
        const int row = data.selectedIndex;
        out.mode = "macro.modulator_audition";
        out.target = "modulator_assignment";
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        out.source = modulatorSourceLabel(modulator->kind);
        out.projection = "audible_audition";
        out.hasOperationGeneration = true;
        out.operationGeneration = audition.generation;
        out.operationStatus = "audition";
        out.mappingIndex = static_cast<int16_t>(audition.sourceId.value);
        out.mappingCount = 1;
        if (row >= 0 && row < data.rowCount) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_assignment_property";
        } else if (isEncoder(event, Config::EncoderID::OPT) && row == 1) {
            out.effect = "edit_existing_modulator_depth";
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "start_existing_modulator_audition";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "cancel_existing_modulator";
            out.outcome = "cancelled";
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_RIGHT,
                       oc::core::input::ButtonBindingType::PRESS
                   )) {
            out.effect = "press_apply_existing_modulator";
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_RIGHT,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "apply_existing_modulator";
            out.outcome = "applied";
        }
        if (isButton(
                event,
                Config::ButtonID::BOTTOM_RIGHT,
                oc::core::input::ButtonBindingType::PRESS
            ) || isButton(
                event,
                Config::ButtonID::BOTTOM_RIGHT,
                oc::core::input::ButtonBindingType::RELEASE
            )) {
            fillGuardedMacroFeedback(out, macro_edit_);
        }
        fillMacroResolutionFacts(
            out,
            midi_cc_coordinator_,
            pages_,
            macro_edit_.editingIndex.get(),
            core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED
        );
        out.projection = "audible_audition";
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::NEW_MODULATOR_AUDITION) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildAutomationRenderData(
                source
            );
        if (!data.visible || !pages_.control.audition.active) return false;
        const auto* modulator = core::state::modulation::findProjectModulator(
            pages_.control.authored.modulation,
            pages_.control.audition.sourceId
        );
        if (modulator == nullptr) return false;
        const bool adsr = modulator->kind ==
            core::state::modulation::ModulatorKind::ADSR;
        const int row = data.selectedIndex;
        out.mode = "macro.modulator_audition";
        out.target = "modulator";
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        out.source = modulatorSourceLabel(modulator->kind);
        out.projection = "audible_audition";
        out.hasOperationGeneration = true;
        out.operationGeneration = pages_.control.audition.generation;
        out.operationStatus = "audition";
        out.mappingIndex = static_cast<int16_t>(
            pages_.control.audition.sourceId.value
        );
        out.mappingCount = 1;
        if (row >= 0 && row < data.rowCount) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = adsr ? "focus_adsr_property" : "focus_lfo_property";
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            if (adsr) {
                static constexpr const char* effects[] = {
                    "edit_adsr_attack",
                    "edit_adsr_decay",
                    "edit_adsr_sustain",
                    "edit_adsr_release",
                    "edit_adsr_depth",
                };
                out.effect = effects[std::clamp(row, 0, 4)];
            } else {
                out.effect = row == 0
                    ? "edit_lfo_shape"
                    : (row == 1 ? "edit_lfo_rate" : "edit_lfo_depth");
            }
        } else if (isButton(
                       event,
                       Config::ButtonID::NAV,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = adsr ? "start_adsr_audition" : "start_lfo_audition";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = adsr ? "cancel_adsr_audition" : "cancel_lfo_audition";
            out.outcome = "cancelled";
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_RIGHT,
                       oc::core::input::ButtonBindingType::PRESS
                   )) {
            out.effect = adsr ? "press_apply_adsr" : "press_apply_lfo";
        } else if (isButton(
                       event,
                       Config::ButtonID::BOTTOM_RIGHT,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = adsr ? "apply_adsr_audition" : "apply_lfo_audition";
            out.outcome = "applied";
        }
        if (isButton(
                event,
                Config::ButtonID::BOTTOM_RIGHT,
                oc::core::input::ButtonBindingType::PRESS
            ) || isButton(
                event,
                Config::ButtonID::BOTTOM_RIGHT,
                oc::core::input::ButtonBindingType::RELEASE
            )) {
            fillGuardedMacroFeedback(out, macro_edit_);
        }
        fillMacroResolutionFacts(
            out,
            midi_cc_coordinator_,
            pages_,
            macro_edit_.editingIndex.get(),
            core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED
        );
        out.projection = "audible_audition";
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::CONVERT_PREVIEW) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildAutomationRenderData(source);
        if (!data.visible) return false;
        out.mode = "macro.modulation_conversion_preview";
        out.target = "modulation";
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        out.property = "Policy";
        copyValueLabel(out.valueLabel, data.rows[0].value);
        out.projection = "non_audible_preview";
        out.source = "automation";
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_conversion_policy";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_conversion_preview";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
            out.effect = macro_edit_.conversionPreview.plan.overwritesModulation
                ? "arm_overwrite_modulation"
                : "press_apply_conversion";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = macro_edit_.conversionPreview.plan.overwritesModulation
                ? guardedMacroEffect(macro_edit_, "cancel_overwrite_modulation")
                : "apply_conversion";
        }
        if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS) ||
            isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            fillGuardedMacroFeedback(out, macro_edit_);
        }
        return true;
    }

    return false;
}

}  // namespace core::context::standalone::ux

#endif

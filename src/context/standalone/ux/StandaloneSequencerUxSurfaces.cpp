#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <cstdio>
#include <cstring>

#include "config/InputIDs.hpp"
#include "config/Timing.hpp"
#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "validation/ux/SemanticUxTraceState.hpp"

namespace core::context::standalone::ux {
namespace interaction_policy = core::handler::sequencer::interaction_policy;
namespace {

using SequencerAction = core::state::sequencer::SequencerInteractionAction;
using SequencerScope = core::state::sequencer::SequencerInteractionScope;

bool isButton(const oc::core::input::InputBindingTraceEvent& event,
              Config::ButtonID button,
              oc::core::input::ButtonBindingType type) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonId == static_cast<oc::type::ButtonID>(button) &&
           event.buttonType == type;
}

bool isEncoder(const oc::core::input::InputBindingTraceEvent& event, Config::EncoderID encoder) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           event.encoderId == static_cast<oc::type::EncoderID>(encoder);
}

bool isMacroButtonRelease(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::RELEASE &&
           Config::macroButtonIndex(event.buttonId, index);
}

bool isMacroEncoderTurn(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           Config::macroEncoderIndex(event.encoderId, index);
}

void copyIndexLabel(char (&out)[16], unsigned value) {
    std::snprintf(out, sizeof(out), "%u", value + 1U);
}

void copyValueLabel(char (&out)[16], const char* value) {
    if (!value) return;
    std::snprintf(out, sizeof(out), "%s", value);
}

bool isAddSlot(const core::validation::ux::SemanticUxContext& out) {
    return out.property && std::strcmp(out.property, "add_slot") == 0;
}

void markNoop(core::validation::ux::SemanticUxContext& out, const char* reason) {
    out.outcome = "noop";
    out.reason = reason;
}

void markIgnored(core::validation::ux::SemanticUxContext& out, const char* reason) {
    out.effect = "release_ignored";
    out.outcome = "ignored";
    out.reason = reason;
}

const char* actionName(SequencerAction action) {
    switch (action) {
        case SequencerAction::MOVE_TRACK:
            return "move_track";
        case SequencerAction::MOVE_PATTERN:
            return "move_pattern";
        case SequencerAction::MOVE_STEP:
            return "move_step";
        case SequencerAction::MOVE_SELECTION_CURSOR:
            return "move_selection_cursor";
        case SequencerAction::SELECT_PATTERN_DIMENSION:
            return "select_pattern_dimension";
        case SequencerAction::SELECT_MUSICAL_PROPERTY:
            return "select_musical_property";
        case SequencerAction::SELECT_STEP_EDITOR_ROW:
            return "select_step_editor_row";
        case SequencerAction::CYCLE_SCOPE:
            return "cycle_scope";
        case SequencerAction::CREATE_PREVIEW_STRUCTURE:
            return "create_preview_structure";
        case SequencerAction::ENTER_SELECTION:
            return "enter_selection";
        case SequencerAction::TOGGLE_SELECTION:
            return "toggle_selection";
        case SequencerAction::OPEN_PATTERN_DIMENSION_SELECTOR:
            return "open_pattern_dimension_selector";
        case SequencerAction::OPEN_MUSICAL_PROPERTY_SELECTOR:
            return "open_musical_property_selector";
        case SequencerAction::APPLY_PATTERN_DIMENSION_SELECTOR:
            return "apply_pattern_dimension_selector";
        case SequencerAction::APPLY_MUSICAL_PROPERTY_SELECTOR:
            return "apply_musical_property_selector";
        case SequencerAction::APPLY_STEP_EDITOR:
            return "apply_step_editor";
        case SequencerAction::CANCEL_TRANSIENT_CONTEXT:
            return "cancel_transient_context";
        case SequencerAction::EDIT_PATTERN_DIMENSION:
            return "edit_pattern_dimension";
        case SequencerAction::EDIT_MUSICAL_PROPERTY_VARIATION:
            return "edit_musical_property_variation";
        case SequencerAction::EDIT_STEP_PROPERTY:
            return "edit_step_property";
        case SequencerAction::EDIT_STEP_LOCAL_RANDOM:
            return "edit_step_local_random";
        case SequencerAction::EDIT_STEP_EDITOR_ROW:
            return "edit_step_editor_row";
        case SequencerAction::OPEN_STEP_EDITOR:
            return "open_step_editor";
        case SequencerAction::TOGGLE_VISIBLE_STEP:
            return "toggle_visible_step";
        case SequencerAction::EDIT_VISIBLE_STEP_PROPERTY:
            return "edit_visible_step_property";
        case SequencerAction::MUTE_CURRENT_TRACK:
            return "mute_current_track";
        case SequencerAction::CLEAR_CURRENT_STRUCTURE:
            return "clear_current_structure";
        case SequencerAction::REMOVE_CURRENT_STRUCTURE:
            return "remove_current_structure";
        case SequencerAction::RESET_CURRENT_STEP_SHALLOW:
            return "reset_current_step_shallow";
        case SequencerAction::RESET_CURRENT_STEP_DEEP:
            return "reset_current_step_deep";
        case SequencerAction::COPY_CURRENT_STEP:
            return "copy_current_step";
        case SequencerAction::PASTE_CURRENT_STEP:
            return "paste_current_step";
        case SequencerAction::CLEAR_STEP_CONTENT:
            return "clear_step_content";
        case SequencerAction::COPY_CURRENT_STRUCTURE:
            return "copy_current_structure";
        case SequencerAction::PASTE_CURRENT_STRUCTURE:
            return "paste_current_structure";
        case SequencerAction::COPY_STEP_CONTENT:
            return "copy_step_content";
        case SequencerAction::PASTE_STEP_CONTENT:
            return "paste_step_content";
        case SequencerAction::RESET_STEP_EDITOR_ROW:
            return "reset_step_editor_row";
        case SequencerAction::REMOVE_STEP_EDITOR_CONTEXT:
            return "remove_step_editor_context";
        case SequencerAction::COPY_STEP_EDITOR_CONTEXT:
            return "copy_step_editor_context";
        case SequencerAction::PASTE_STEP_EDITOR_CONTEXT:
            return "paste_step_editor_context";
        case SequencerAction::MUTE_TRACK_SELECTION:
            return "mute_track_selection";
        case SequencerAction::CLEAR_SELECTION:
            return "clear_selection";
        case SequencerAction::DELETE_SELECTION:
            return "delete_selection";
        case SequencerAction::RESET_STEP_SELECTION_SHALLOW:
            return "reset_step_selection_shallow";
        case SequencerAction::RESET_STEP_SELECTION_DEEP:
            return "reset_step_selection_deep";
        case SequencerAction::COPY_SELECTION:
            return "copy_selection";
        case SequencerAction::PASTE_SELECTION:
            return "paste_selection";
        case SequencerAction::COPY_STEP_SELECTION:
            return "copy_step_selection";
        case SequencerAction::PASTE_STEP_SELECTION:
            return "paste_step_selection";
        case SequencerAction::NONE:
        default:
            return nullptr;
    }
}

const char* armActionName(SequencerAction action) {
    switch (action) {
        case SequencerAction::REMOVE_CURRENT_STRUCTURE:
            return "arm_remove_current_structure";
        case SequencerAction::RESET_CURRENT_STEP_DEEP:
            return "arm_reset_current_step_deep";
        case SequencerAction::PASTE_CURRENT_STRUCTURE:
            return "arm_paste_current_structure";
        case SequencerAction::PASTE_CURRENT_STEP:
            return "arm_paste_current_step";
        case SequencerAction::DELETE_SELECTION:
            return "arm_delete_selection";
        case SequencerAction::RESET_STEP_SELECTION_DEEP:
            return "arm_reset_step_selection_deep";
        case SequencerAction::PASTE_SELECTION:
            return "arm_paste_selection";
        case SequencerAction::PASTE_STEP_SELECTION:
            return "arm_paste_step_selection";
        case SequencerAction::CLEAR_CURRENT_STRUCTURE:
            return "arm_clear_current_structure";
        case SequencerAction::COPY_CURRENT_STRUCTURE:
            return "arm_copy_current_structure";
        case SequencerAction::CLEAR_STEP_CONTENT:
            return "arm_clear_step_content";
        case SequencerAction::COPY_STEP_CONTENT:
            return "arm_copy_step_content";
        case SequencerAction::REMOVE_STEP_EDITOR_CONTEXT:
            return "arm_remove_step_editor_context";
        case SequencerAction::COPY_STEP_EDITOR_CONTEXT:
            return "arm_copy_step_editor_context";
        case SequencerAction::PASTE_STEP_EDITOR_CONTEXT:
            return "arm_paste_step_editor_context";
        case SequencerAction::MUTE_CURRENT_TRACK:
            return "arm_mute_current_track";
        case SequencerAction::MUTE_TRACK_SELECTION:
            return "arm_mute_track_selection";
        case SequencerAction::CLEAR_SELECTION:
            return "arm_clear_selection";
        case SequencerAction::COPY_SELECTION:
            return "arm_copy_selection";
        case SequencerAction::COPY_STEP_SELECTION:
            return "arm_copy_step_selection";
        default:
            return actionName(action);
    }
}

bool isPasteAction(SequencerAction action) {
    return action == SequencerAction::PASTE_CURRENT_STRUCTURE ||
           action == SequencerAction::PASTE_SELECTION;
}

const char* contextualReasonName(
    core::state::contextual::ContextActionReason reason
) {
    using Reason = core::state::contextual::ContextActionReason;
    switch (reason) {
        case Reason::EMPTY_CLIPBOARD: return "clipboard_empty";
        case Reason::WRONG_PAYLOAD: return "wrong_payload";
        case Reason::INVALID_PAYLOAD: return "invalid_payload";
        case Reason::SAME_SOURCE_TARGET: return "same_track";
        case Reason::OUT_OF_RANGE: return "out_of_range";
        case Reason::CAPACITY: return "capacity";
        case Reason::PENDING: return "paste_pending";
        case Reason::NO_ROUTE: return "no_route";
        case Reason::HISTORY_UNAVAILABLE: return "history_unavailable";
        case Reason::ALLOCATION_UNAVAILABLE: return "allocation_unavailable";
        case Reason::NONE: return nullptr;
        default: return "blocked";
    }
}

void fillTrackTransferFacts(
    const core::state::ClipboardTransferPlan& plan,
    core::validation::ux::SemanticUxContext& out
) {
    out.sourceMask = plan.sourceMask;
    out.targetMask = plan.targetMask;
    out.createMask = plan.createMask;
    out.overwriteMask = plan.overwriteMask;
    out.routePolicy = "preserve_destination";
    if (plan.count > 0) {
        out.hasTargetRoute = true;
        out.targetRoute = plan.entries[0].targetMidiChannel;
        out.targetRouteValid = plan.entries[0].targetRouteValid;
    }
}

bool isSelectionScope(SequencerScope scope) {
    return scope == SequencerScope::TRACK_SELECTION ||
           scope == SequencerScope::PATTERN_SELECTION ||
           scope == SequencerScope::STEP_SELECTION;
}

const char* modeForScope(SequencerScope scope) {
    switch (scope) {
        case SequencerScope::TRACK:
            return "sequencer.track";
        case SequencerScope::PATTERN:
            return "sequencer.pattern";
        case SequencerScope::STEP:
            return "sequencer.step";
        case SequencerScope::CHILD_PATTERN:
            return "sequencer.child_pattern";
        case SequencerScope::PATTERN_DIMENSION_SELECTOR:
            return "sequencer.quick_controls";
        case SequencerScope::MUSICAL_PROPERTY_SELECTOR:
            return "sequencer.property_selector";
        case SequencerScope::TRACK_SELECTION:
        case SequencerScope::PATTERN_SELECTION:
            return "sequencer.structure_selection";
        case SequencerScope::STEP_SELECTION:
            return "sequencer.step_selection";
        case SequencerScope::STEP_EDITOR:
            return "sequencer.step_edit";
    }
    return "sequencer";
}

core::state::StructureSelectionScope selectionScopeForPolicyScope(SequencerScope scope) {
    switch (scope) {
        case SequencerScope::TRACK_SELECTION:
            return core::state::StructureSelectionScope::TRACK;
        case SequencerScope::STEP_SELECTION:
            return core::state::StructureSelectionScope::STEP;
        case SequencerScope::PATTERN_SELECTION:
        default:
            return core::state::StructureSelectionScope::PAGE;
    }
}

bool policyScopeTargetsTrack(SequencerScope scope) {
    return scope == SequencerScope::TRACK ||
           scope == SequencerScope::TRACK_SELECTION;
}

bool policyScopeTargetsStep(SequencerScope scope) {
    return scope == SequencerScope::STEP ||
           scope == SequencerScope::STEP_SELECTION ||
           scope == SequencerScope::STEP_EDITOR;
}

const char* targetForPolicyScope(SequencerScope scope) {
    if (policyScopeTargetsTrack(scope)) return "track";
    if (policyScopeTargetsStep(scope)) return "step";
    return "pattern";
}

SequencerAction structureActionForEvent(
    const core::state::sequencer::SequencerInteractionPolicy& policy,
    const oc::core::input::InputBindingTraceEvent& event
) {
    if (isEncoder(event, Config::EncoderID::NAV)) return policy.navTurn;
    if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
        return policy.navTap;
    }
    if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        return policy.navLongPress;
    }
    if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        return policy.leftTopTap;
    }
    if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
        return policy.bottomLeftHold != SequencerAction::NONE
            ? policy.bottomLeftHold
            : policy.bottomLeftTap;
    }
    if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
        return policy.bottomLeftTap;
    }
    if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        return policy.bottomLeftHold;
    }
    if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
        return policy.bottomRightHold != SequencerAction::NONE
            ? policy.bottomRightHold
            : policy.bottomRightTap;
    }
    if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
        return policy.bottomRightTap;
    }
    if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        return policy.bottomRightHold;
    }
    return SequencerAction::NONE;
}

uint16_t sequencerPageMask(const core::state::sequencer::SequencerState& sequencer) {
    const uint8_t count = sequencer.activePageCount();
    if (count >= 16U) return 0xffffU;
    return static_cast<uint16_t>((1U << count) - 1U);
}

const char* structureTarget(core::state::StructureSelectionScope scope) {
    switch (scope) {
        case core::state::StructureSelectionScope::STEP:
            return "step";
        case core::state::StructureSelectionScope::TRACK:
            return "track";
        case core::state::StructureSelectionScope::PAGE:
        default:
            return "page";
    }
}

bool fillResolvedStepUxContext(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    uint8_t step,
    core::state::sequencer::StepProperty property,
    core::validation::ux::SemanticUxContext& out
) {
    const auto context =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            tracks.projectScaleSettings(),
            property
        );
    const auto touchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    const bool stepInlineEditActive =
        sequencer.stepInlineFeedback.visible.get() && touchedMask.test(step);
    const auto resolved = core::state::sequencer::buildSequencerResolvedStepDisplayState(
        context,
        step,
        stepInlineEditActive
    );
    if (!resolved.valid) return false;

    const auto values = core::state::sequencer::sequencerResolvedStepDisplayValues(resolved);

    out.hasStepOn = true;
    out.stepOn = resolved.enabled;
    out.hasResolvedStep = true;
    out.resolvedNote = values.note;
    out.resolvedVelocity = values.velocity;
    out.resolvedGate = values.gate;
    out.resolvedNudge = values.nudge;
    out.resolvedProbability = resolved.probability;
    out.resolvedVariationVisible = resolved.variation.visible;
    core::state::sequencer::formatStepPropertyValue(
        out.valueLabel,
        sizeof(out.valueLabel),
        property,
        values.note,
        values.velocity,
        values.gate,
        values.nudge,
        resolved.probability
    );
    return true;
}

}  // namespace

SequencerPropertySelectorUxSurface::SequencerPropertySelectorUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::sequencer::SequencerState& sequencer
) : active_view_(activeView), sequencer_(sequencer) {}

bool SequencerPropertySelectorUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    const bool opening =
        isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::PRESS);
    if (active_view_.get() != core::ui::ViewType::SEQUENCER ||
        (!opening && !sequencer_.stepPropertyInlineSelector.selecting.get())) {
        return false;
    }

    out.mode = "sequencer.property_selector";
    out.target = "property";
    out.property = core::state::sequencer::stepPropertyName(sequencer_.activeStepProperty.get());
    std::snprintf(
        out.valueLabel,
        sizeof(out.valueLabel),
        "%u",
        static_cast<unsigned>(
            sequencer_.variationRangeForProperty(sequencer_.activeStepProperty.get())
        )
    );
    if (opening) {
        out.effect = "open_property_selector";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_property";
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        out.effect = "edit_variation_range";
    } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_property";
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "cancel_property";
    }
    return true;
}

SequencerQuickControlsUxSurface::SequencerQuickControlsUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::sequencer::SequencerState& sequencer
) : active_view_(activeView), sequencer_(sequencer) {}

bool SequencerQuickControlsUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        return false;
    }

    const bool opening = isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::PRESS);
    const bool feedbackVisible = sequencer_.patternQuickControls.feedbackVisible.get();
    if (!opening && !sequencer_.patternQuickControls.selecting.get() && !feedbackVisible) {
        return false;
    }

    const auto item = sequencer_.patternQuickControls.focusedItem.get();
    out.mode = "sequencer.quick_controls";
    out.target = "pattern";
    out.property = core::state::sequencer::quickControlLabel(item);

    if (opening) {
        out.effect = "open_quick_controls";
    } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = "open_history_layer";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_quick_control";
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        out.effect = "edit_quick_control";
    } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_quick_controls";
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = sequencer_.patternQuickControls.physicalHoldActive.get()
            ? "undo_history"
            : "cancel_quick_controls";
    } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE) &&
               sequencer_.patternQuickControls.physicalHoldActive.get()) {
        out.effect = "redo_history";
    }
    return true;
}

SequencerStructureUxSurface::SequencerStructureUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::StructureClipboardState& structureClipboard,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::validation::ux::StructureUxTraceState* traceState
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    structure_clipboard_(structureClipboard),
    sequencer_(sequencer),
    tracks_(tracks),
    trace_state_(traceState) {}

bool SequencerStructureUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
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

    const auto policy = interaction_policy::build(
        sequencer_,
        track_navigation_,
        navigation_focus_.get()
    );
    const auto action = structureActionForEvent(policy, event);
    if (action == SequencerAction::NONE) {
        return false;
    }

    const bool selectionActive = isSelectionScope(policy.scope);
    auto scope = selectionActive
        ? selectionScopeForPolicyScope(policy.scope)
        : core::state::selectionScopeForFocus(navigation_focus_.get());
    if (selectionActive && track_navigation_.selection.active.get()) {
        scope = track_navigation_.selection.scope.get();
    } else if (selectionActive && sequencer_.structureUi.pageSelection.active.get()) {
        scope = sequencer_.structureUi.pageSelection.scope.get();
    }

    out.mode = modeForScope(policy.scope);
    out.target = selectionActive ? structureTarget(scope) : targetForPolicyScope(policy.scope);

    uint8_t index = 0;
    if (scope == core::state::StructureSelectionScope::STEP && selectionActive) {
        const auto& selection = sequencer_.structureUi.stepSelection;
        const uint8_t step = selection.cursorStep.get();
        out.mode = "sequencer.step_selection";
        out.target = "step";
        out.targetStep = static_cast<int16_t>(step);
        out.property = selection.selected(step) ? "selected" : "cursor";
        copyIndexLabel(out.valueLabel, step);

        out.effect = isButton(
            event,
            Config::ButtonID::BOTTOM_RIGHT,
            oc::core::input::ButtonBindingType::PRESS
        )
            ? armActionName(action)
            : actionName(action);
        return true;
    }

    const bool targetTrack =
        selectionActive ? scope == core::state::StructureSelectionScope::TRACK
                        : policyScopeTargetsTrack(policy.scope);
    const uint16_t targetMask = targetTrack ? tracks_.currentEnabledMask() : sequencerPageMask(sequencer_);
    out.targetMask = targetMask;

    if (targetTrack) {
        index = track_navigation_.selection.active.get()
            ? track_navigation_.selection.cursorIndex.get()
            : (track_navigation_.previewAddSlot.get()
                   ? track_navigation_.previewTrackIndex.get()
                   : tracks_.activeTrackIndex());
        out.property = track_navigation_.previewAddSlot.get() && !selectionActive
            ? "add_slot"
            : (selectionActive ? "selection" : "existing");
    } else {
        index = sequencer_.structureUi.pageSelection.active.get()
            ? sequencer_.structureUi.pageSelection.cursorIndex.get()
            : sequencer_.structureUi.previewPageIndex.get();
        out.property = sequencer_.structureUi.previewAddPageSlot.get() && !selectionActive
            ? "add_slot"
            : (selectionActive ? "selection" : "existing");
    }
    out.targetIndex = static_cast<int16_t>(index);
    copyIndexLabel(out.valueLabel, index);

    core::state::ClipboardTransferPlan trackTransferPlan{};
    core::state::contextual::ContextActionSpec trackTransferAction{};
    bool canPaste = selectionActive
        ? structure_clipboard_.hasSequencerPageSelection()
        : structure_clipboard_.hasSequencerPage();
    if (targetTrack) {
        trackTransferPlan = core::state::buildSequencerTrackClipboardTransferPlan(
            structure_clipboard_,
            tracks_,
            index,
            0,
            &sequencer_
        );
        const uint16_t selectedEnabledMask = static_cast<uint16_t>(
            track_navigation_.selection.selectedMask.get() &
            tracks_.currentEnabledMask()
        );
        trackTransferAction =
            core::state::sequencer::buildSequencerTrackTransferActionSpec(
                trackTransferPlan,
                index,
                selectionActive ? selectedEnabledMask != 0
                                : !track_navigation_.previewAddSlot.get(),
                static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            );
        canPaste = core::state::contextual::canExecute(trackTransferAction.hold);
    }

    if (selectionActive) {
        out.effect = isButton(
            event,
            Config::ButtonID::BOTTOM_LEFT,
            oc::core::input::ButtonBindingType::PRESS
        ) || isButton(
            event,
            Config::ButtonID::BOTTOM_RIGHT,
            oc::core::input::ButtonBindingType::PRESS
        )
            ? armActionName(action)
            : actionName(action);
        if (targetTrack && isPasteAction(action)) {
            fillTrackTransferFacts(trackTransferPlan, out);
            if (!canPaste) {
                markNoop(out, contextualReasonName(trackTransferAction.hold.reason));
            } else if (trackTransferAction.hold.availability ==
                       core::state::contextual::ContextActionAvailability::WARNING) {
                out.outcome = "warning";
                out.reason = contextualReasonName(trackTransferAction.hold.reason);
            }
        }
        return true;
    }

    if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = armActionName(action);
        const bool mutableSingleTrack =
            targetTrack && action == SequencerAction::MUTE_CURRENT_TRACK;
        if (isAddSlot(out) ||
            (!mutableSingleTrack &&
             core::state::shared::countEnabled(
                 targetMask,
                 targetTrack
                     ? core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
                     : core::state::sequencer::SequencerState::PAGE_COUNT
             ) <= 1U)) {
            markNoop(out, isAddSlot(out) ? "add_slot" : "single_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = actionName(action);
        if (trace_state_ && trace_state_->ignoreNextBottomLeftRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = actionName(action);
        if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = armActionName(action);
        if (targetTrack && isPasteAction(action)) {
            fillTrackTransferFacts(trackTransferPlan, out);
        }
        if (isPasteAction(action) && !canPaste) {
            markNoop(
                out,
                targetTrack
                    ? contextualReasonName(trackTransferAction.hold.reason)
                    : "clipboard_empty"
            );
        } else if (targetTrack && isPasteAction(action) &&
                   trackTransferAction.hold.availability ==
                       core::state::contextual::ContextActionAvailability::WARNING) {
            out.outcome = "warning";
            out.reason = contextualReasonName(trackTransferAction.hold.reason);
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = actionName(action);
        if (trace_state_ && trace_state_->ignoreNextBottomRightRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = actionName(action);
        if (targetTrack && isPasteAction(action)) {
            fillTrackTransferFacts(trackTransferPlan, out);
        }
        if (isPasteAction(action) && !canPaste) {
            markNoop(
                out,
                targetTrack
                    ? contextualReasonName(trackTransferAction.hold.reason)
                    : "clipboard_empty"
            );
        }
    } else {
        out.effect = actionName(action);
    }
    return true;
}

SequencerStepGridUxSurface::SequencerStepGridUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    sequencer_(sequencer),
    tracks_(tracks) {}

bool SequencerStepGridUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        return false;
    }

    uint8_t index = 0;
    const bool macroButton = isMacroButtonRelease(event, index);
    const bool macroEncoder = isMacroEncoderTurn(event, index);
    const bool focusedEncoder = isEncoder(event, Config::EncoderID::OPT);
    if (!macroButton && !macroEncoder && !focusedEncoder) {
        return false;
    }

    const auto policy = interaction_policy::build(
        sequencer_,
        track_navigation_,
        navigation_focus_.get()
    );
    const auto action = focusedEncoder
        ? policy.optTurn
        : (macroEncoder ? policy.macroTurn : policy.macroTap);
    if (action != SequencerAction::EDIT_STEP_PROPERTY &&
        action != SequencerAction::EDIT_VISIBLE_STEP_PROPERTY &&
        action != SequencerAction::TOGGLE_VISIBLE_STEP &&
        action != SequencerAction::TOGGLE_SELECTION) {
        return false;
    }

    uint8_t step = 0;
    if (focusedEncoder) {
        const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
        if (len == 0 || sequencer_.focusedStep.get() >= len) {
            return false;
        }
        step = sequencer_.focusedStep.get();
    } else {
        if (!core::state::sequencer::resolveActiveContentStepInPage(
                sequencer_,
                sequencer_.page.get(),
                index,
                step
            )) {
            return false;
        }
    }

    const auto property = sequencer_.activeStepProperty.get();
    out.mode = modeForScope(policy.scope);
    out.target = "step";
    out.targetStep = static_cast<int16_t>(step);
    out.property = action == SequencerAction::TOGGLE_SELECTION
        ? (sequencer_.structureUi.stepSelection.selected(step) ? "selected" : "cursor")
        : core::state::sequencer::stepPropertyName(property);
    out.effect = actionName(action);
    fillResolvedStepUxContext(sequencer_, tracks_, step, property, out);
    return true;
}

SequencerStepEditUxSurface::SequencerStepEditUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    sequencer_(sequencer),
    tracks_(tracks) {}

bool SequencerStepEditUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        return false;
    }

    uint8_t openingIndex = 0;
    const bool opening = event.domain == oc::core::input::InputBindingTraceDomain::Button &&
                         event.buttonType == oc::core::input::ButtonBindingType::LONG_PRESS &&
                         Config::macroButtonIndex(event.buttonId, openingIndex);
    if (!opening && !sequencer_.stepEdit.visible.get()) {
        return false;
    }

    const auto policy = interaction_policy::build(
        sequencer_,
        track_navigation_,
        navigation_focus_.get()
    );

    if (opening) {
        if (policy.macroLongPress != SequencerAction::OPEN_STEP_EDITOR) {
            return false;
        }
        uint8_t step = 0;
        if (!core::state::sequencer::resolveActiveContentStepInPage(
                sequencer_,
                sequencer_.page.get(),
                openingIndex,
                step
            )) {
            return false;
        }
        out.mode = "sequencer.step_edit";
        out.target = "step";
        out.targetStep = static_cast<int16_t>(step);
        out.effect = actionName(policy.macroLongPress);
        fillResolvedStepUxContext(
            sequencer_,
            tracks_,
            step,
            sequencer_.activeStepProperty.get(),
            out
        );
        copyIndexLabel(out.valueLabel, step);
        return true;
    }

    auto data = core::context::standalone::sequencer_overlay_presenter::buildStepEditRenderData({
        sequencer_,
        tracks_,
    });
    if (!data.visible) {
        return false;
    }

    out.mode = "sequencer.step_edit";
    out.target = "step";
    out.targetStep = static_cast<int16_t>(data.stepIndex);
    auto resolvedProperty = sequencer_.activeStepProperty.get();
    if (data.selectedIndex >= 0 && data.selectedIndex < data.rowCount) {
        if (core::state::sequencer::step_edit_rows::isProperty(
                static_cast<uint8_t>(data.selectedIndex)
            )) {
            resolvedProperty = core::state::sequencer::step_edit_rows::propertyForRow(
                static_cast<uint8_t>(data.selectedIndex)
            );
        }
        fillResolvedStepUxContext(
            sequencer_,
            tracks_,
            static_cast<uint8_t>(data.stepIndex),
            resolvedProperty,
            out
        );
        out.property = data.rows[data.selectedIndex].key;
        copyValueLabel(out.valueLabel, data.rows[data.selectedIndex].value);
    }

    uint8_t closeIndex = 0;
    const bool macroClose =
        isMacroButtonRelease(event, closeIndex) &&
        closeIndex == static_cast<uint8_t>(data.stepIndex % core::state::sequencer::SequencerState::STEPS_PER_PAGE);
    if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = actionName(policy.navTurn);
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        out.effect = actionName(policy.optTurn);
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE) ||
               macroClose) {
        out.effect = actionName(policy.navTap);
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = actionName(policy.leftTopTap);
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
        const auto action = policy.bottomLeftHold != SequencerAction::NONE
            ? policy.bottomLeftHold
            : policy.bottomLeftTap;
        if (action == SequencerAction::NONE) return false;
        out.effect = armActionName(action);
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
        if (policy.bottomLeftTap == SequencerAction::NONE) return false;
        out.effect = actionName(policy.bottomLeftTap);
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        if (policy.bottomLeftHold == SequencerAction::NONE) return false;
        out.effect = actionName(policy.bottomLeftHold);
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
        const auto action = policy.bottomRightHold != SequencerAction::NONE
            ? policy.bottomRightHold
            : policy.bottomRightTap;
        if (action == SequencerAction::NONE) return false;
        out.effect = armActionName(action);
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
        if (policy.bottomRightTap == SequencerAction::NONE) return false;
        out.effect = actionName(policy.bottomRightTap);
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        if (policy.bottomRightHold == SequencerAction::NONE) return false;
        out.effect = actionName(policy.bottomRightHold);
    }
    return true;
}

}  // namespace core::context::standalone::ux

#endif

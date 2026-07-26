#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "config/Timing.hpp"
#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackEditorState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerCcLanePropertySelection.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerPatternEditorOps.hpp"
#include "state/sequencer/SequencerPatternRandomizeSession.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/SequencerStepPresetModel.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "validation/ux/SemanticUxTraceState.hpp"
#include "validation/ux/SequencerCcLaneSemanticGesture.hpp"
#include "validation/ux/SequencerTrackTransferSemanticProjection.hpp"

namespace core::context::standalone::ux {
namespace interaction_policy = core::handler::sequencer::interaction_policy;
namespace {

using SequencerAction = core::state::sequencer::SequencerInteractionAction;
using SequencerScope = core::state::sequencer::SequencerInteractionScope;

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

FLASHMEM bool isSemanticStateProjection(
    const oc::core::input::InputBindingTraceEvent& event
) {
    return event.buttonId == std::numeric_limits<oc::type::ButtonID>::max() &&
           event.encoderId == std::numeric_limits<oc::type::EncoderID>::max();
}

FLASHMEM bool ccLaneActionGesture(
    const oc::core::input::InputBindingTraceEvent& event,
    core::state::sequencer::SequencerCcLaneActionSlot& slot,
    core::validation::ux::SequencerCcLaneGesturePhase& phase
) {
    using ButtonType = oc::core::input::ButtonBindingType;
    if (event.domain != oc::core::input::InputBindingTraceDomain::Button ||
        (event.buttonType != ButtonType::PRESS &&
         event.buttonType != ButtonType::RELEASE)) {
        return false;
    }

    if (event.buttonId ==
        static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_LEFT)) {
        slot = core::state::sequencer::SequencerCcLaneActionSlot::BOTTOM_LEFT;
    } else if (event.buttonId ==
               static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_CENTER)) {
        slot = core::state::sequencer::SequencerCcLaneActionSlot::BOTTOM_CENTER;
    } else if (event.buttonId ==
               static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_RIGHT)) {
        slot = core::state::sequencer::SequencerCcLaneActionSlot::BOTTOM_RIGHT;
    } else {
        return false;
    }

    phase = event.buttonType == ButtonType::PRESS
        ? core::validation::ux::SequencerCcLaneGesturePhase::PRESS
        : core::validation::ux::SequencerCcLaneGesturePhase::RELEASE;
    return true;
}

FLASHMEM const char* ccWinnerName(
    core::state::shared::MidiCcCandidateClass candidateClass
) {
    using Candidate = core::state::shared::MidiCcCandidateClass;
    switch (candidateClass) {
        case Candidate::LIVE_MANUAL: return "live_manual";
        case Candidate::SEQUENCER_CC_LANE: return "sequencer_cc_lane";
        case Candidate::MACRO_COMPUTED: return "macro_computed";
        case Candidate::MACRO_STATIC: return "macro_static";
    }
    return nullptr;
}

FLASHMEM bool isMacroButtonRelease(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::RELEASE &&
           Config::macroButtonIndex(event.buttonId, index);
}

FLASHMEM bool isMacroEncoderTurn(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           Config::macroEncoderIndex(event.encoderId, index);
}

FLASHMEM void copyIndexLabel(char (&out)[16], unsigned value) {
    std::snprintf(out, sizeof(out), "%u", value + 1U);
}

FLASHMEM void copyValueLabel(char (&out)[16], const char* value) {
    if (!value) return;
    std::snprintf(out, sizeof(out), "%s", value);
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

FLASHMEM const char* stepPresetActionName(
    core::state::contextual::ContextActionId action
) {
    using Action = core::state::contextual::ContextActionId;
    switch (action) {
        case Action::APPLY: return "apply_step_preset";
        case Action::SAVE: return "save_step_preset";
        case Action::LOAD: return "load_step_presets";
        default: return nullptr;
    }
}

FLASHMEM const char* contextActionReasonName(
    core::state::contextual::ContextActionReason reason
) {
    using Reason = core::state::contextual::ContextActionReason;
    switch (reason) {
        case Reason::NONE: return nullptr;
        case Reason::NO_ACTION: return "no_action";
        case Reason::EMPTY_SELECTION: return "empty_selection";
        case Reason::MINIMUM_CARDINALITY: return "minimum_cardinality";
        case Reason::EMPTY_CLIPBOARD: return "empty_clipboard";
        case Reason::WRONG_PAYLOAD: return "wrong_payload";
        case Reason::INVALID_PAYLOAD: return "invalid_payload";
        case Reason::ADAPTED: return "adapted";
        case Reason::DEFAULTED: return "defaulted";
        case Reason::CORRUPT_ASSET: return "corrupt_asset";
        case Reason::UNSUPPORTED_VERSION: return "unsupported_version";
        case Reason::STALE_TARGET: return "stale_target";
        case Reason::SAME_SOURCE_TARGET: return "same_source_target";
        case Reason::OUT_OF_RANGE: return "out_of_range";
        case Reason::CAPACITY: return "capacity";
        case Reason::PENDING: return "pending";
        case Reason::NO_ROUTE: return "no_route";
        case Reason::INCOMPATIBLE: return "incompatible";
        case Reason::HISTORY_UNAVAILABLE: return "history_unavailable";
        case Reason::STORAGE_UNAVAILABLE: return "storage_unavailable";
        case Reason::ALLOCATION_UNAVAILABLE: return "allocation_unavailable";
        case Reason::CONFLICT: return "conflict";
        case Reason::READ_ONLY: return "read_only";
        case Reason::TRANSPORT_STATE: return "transport_state";
        case Reason::FAILED: return "failed";
    }
    return "unknown";
}

FLASHMEM const char* stepPresetCompatibilityReasonName(
    core::state::sequencer::SequencerStepPresetCompatibility compatibility
) {
    using Compatibility =
        core::state::sequencer::SequencerStepPresetCompatibility;
    switch (compatibility) {
        case Compatibility::UNKNOWN: return "compatibility_unknown";
        case Compatibility::READY: return nullptr;
        case Compatibility::WARNING_ADAPTED: return "adapted";
        case Compatibility::WARNING_DEFAULTED: return "defaulted";
        case Compatibility::BLOCKED_CONTEXT: return "wrong_context";
        case Compatibility::BLOCKED_CAPACITY: return "capacity";
        case Compatibility::CORRUPT: return "corrupt_asset";
        case Compatibility::UNSUPPORTED_VERSION: return "unsupported_version";
        case Compatibility::STORAGE_UNAVAILABLE: return "storage_unavailable";
        case Compatibility::STALE_TARGET: return "stale_target";
    }
    return "compatibility_unknown";
}

FLASHMEM const char* operationOutcomeName(
    core::state::contextual::OperationFeedbackStatus status
) {
    using Status = core::state::contextual::OperationFeedbackStatus;
    switch (status) {
        case Status::PREVIEW: return "preview";
        case Status::PRESSED: return "pressed";
        case Status::ARMED: return "armed";
        case Status::QUEUED: return "queued";
        case Status::APPLIED: return "applied";
        case Status::CANCELLED: return "cancelled";
        case Status::BLOCKED: return "blocked";
        case Status::WARNING: return "warning";
        case Status::CONFLICT: return "conflict";
        case Status::FAILED: return "failed";
        case Status::NONE: return nullptr;
    }
    return nullptr;
}

FLASHMEM const char* guardedActionOutcomeName(
    core::state::contextual::GuardedActionPhase phase
) {
    using Phase = core::state::contextual::GuardedActionPhase;
    switch (phase) {
        case Phase::PRESSED: return "pressed";
        case Phase::ARMED:
        case Phase::COMMITTED: return "armed";
        case Phase::CANCELLED: return "cancelled";
        case Phase::IDLE: return nullptr;
    }
    return nullptr;
}

FLASHMEM const char* actionName(SequencerAction action) {
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
        case SequencerAction::SELECT_STEP_CONTENT_ACTION:
            return "select_step_content_action";
        case SequencerAction::ENTER_SELECTION:
            return "enter_selection";
        case SequencerAction::TOGGLE_SELECTION:
            return "toggle_selection";
        case SequencerAction::OPEN_PATTERN_DIMENSION_SELECTOR:
            return "open_pattern_dimension_selector";
        case SequencerAction::OPEN_MUSICAL_PROPERTY_SELECTOR:
            return "open_musical_property_selector";
        case SequencerAction::OPEN_STEP_CONTENT_SELECTOR:
            return "open_step_content_selector";
        case SequencerAction::APPLY_PATTERN_DIMENSION_SELECTOR:
            return "apply_pattern_dimension_selector";
        case SequencerAction::APPLY_MUSICAL_PROPERTY_SELECTOR:
            return "apply_musical_property_selector";
        case SequencerAction::APPLY_STEP_CONTENT_SELECTOR:
            return "apply_step_content_selector";
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
        case SequencerAction::RESET_STEP_SELECTION_SHALLOW:
            return "reset_step_selection_shallow";
        case SequencerAction::RESET_STEP_SELECTION_DEEP:
            return "reset_step_selection_deep";
        case SequencerAction::COPY_STEP_SELECTION:
            return "copy_step_selection";
        case SequencerAction::PASTE_STEP_SELECTION:
            return "paste_step_selection";
        case SequencerAction::NONE:
        default:
            return nullptr;
    }
}

FLASHMEM const char* stepContentActionName(
    core::state::sequencer::SequencerStepContentAction action
) {
    using Action = core::state::sequencer::SequencerStepContentAction;
    switch (action) {
        case Action::CHORD: return "chord";
        case Action::MICRO_SEQUENCE: return "micro_sequence";
        case Action::CYCLE_STATES: return "cycle_states";
        case Action::COUNT: return nullptr;
    }
    return nullptr;
}

FLASHMEM const char* armActionName(SequencerAction action) {
    switch (action) {
        case SequencerAction::REMOVE_CURRENT_STRUCTURE:
            return "arm_remove_current_structure";
        case SequencerAction::RESET_CURRENT_STEP_DEEP:
            return "arm_reset_current_step_deep";
        case SequencerAction::PASTE_CURRENT_STRUCTURE:
            return "arm_paste_current_structure";
        case SequencerAction::PASTE_CURRENT_STEP:
            return "arm_paste_current_step";
        case SequencerAction::RESET_STEP_SELECTION_DEEP:
            return "arm_reset_step_selection_deep";
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
        case SequencerAction::COPY_STEP_SELECTION:
            return "arm_copy_step_selection";
        default:
            return actionName(action);
    }
}

FLASHMEM bool isPasteAction(SequencerAction action) {
    return action == SequencerAction::PASTE_CURRENT_STRUCTURE;
}

FLASHMEM void fillTrackTransferFacts(
    const core::state::ClipboardTransferPlan& plan,
    const core::state::sequencer::SequencerTrackPasteUiState* lifecycle,
    core::validation::ux::SemanticUxContext& out
) {
    out.sourceMask = plan.sourceMask;
    out.targetMask = plan.targetMask;
    out.createMask = plan.createMask;
    out.overwriteMask = plan.overwriteMask;
    out.routePolicy = "preserve_destination";
    if (plan.hasEntries()) {
        const auto& entry = plan.entry;
        out.mappingIndex = 0;
        out.mappingCount = 1;
        out.sourceTrack = entry.sourceTrack;
        out.targetTrack = entry.targetTrack;
        out.targetKind =
            entry.targetKind == core::state::ClipboardTransferTargetKind::FREE
            ? "free"
            : "overwrite";
        out.inheritedLaneCount = entry.inheritedLaneCount;
        out.pinnedLaneCount = entry.pinnedLaneCount;
        out.hasTargetRoute = true;
        out.targetRoute = entry.targetMidiChannel;
        out.targetRouteValid = entry.targetRouteValid;
    }
    if (lifecycle == nullptr) return;
    using Feedback = core::state::contextual::OperationFeedbackStatus;
    out.operationOrigin = "track_paste";
    if (lifecycle->operationGeneration != 0) {
        out.hasOperationGeneration = true;
        out.operationGeneration = lifecycle->operationGeneration;
    }
    switch (lifecycle->feedback.status) {
        case Feedback::PREVIEW: out.operationStatus = "preview"; break;
        case Feedback::PRESSED: out.operationStatus = "pressed"; break;
        case Feedback::ARMED: out.operationStatus = "armed"; break;
        case Feedback::QUEUED: out.operationStatus = "queued"; break;
        case Feedback::APPLIED: out.operationStatus = "applied"; break;
        case Feedback::CANCELLED: out.operationStatus = "cancelled"; break;
        case Feedback::BLOCKED: out.operationStatus = "blocked"; break;
        case Feedback::WARNING: out.operationStatus = "warning"; break;
        case Feedback::CONFLICT: out.operationStatus = "conflict"; break;
        case Feedback::FAILED: out.operationStatus = "failed"; break;
        case Feedback::NONE:
        default: out.operationStatus = nullptr; break;
    }
    if (lifecycle->activationGeneration != 0) {
        out.activationOrigin = "track_paste";
        out.hasActivationGeneration = true;
        out.activationGeneration = lifecycle->activationGeneration;
    }
}

FLASHMEM bool fillTrackPasteActivationFacts(
    const core::state::sequencer::SequencerTrackActivationTelemetry& telemetry,
    uint32_t expectedGeneration,
    core::validation::ux::SemanticUxContext& out
) {
    using Origin = core::state::sequencer::SequencerTrackActivationOrigin;
    using Status = core::state::sequencer::SequencerTrackActivationStatus;
    if (telemetry.origin != Origin::TRACK_PASTE || telemetry.generation == 0 ||
        telemetry.generation != expectedGeneration) {
        return false;
    }

    switch (telemetry.status) {
        case Status::QUEUED:
            out.outcome = "queued";
            out.reason = "paste_pending";
            break;
        case Status::APPLIED:
            out.outcome = "applied";
            out.reason = nullptr;
            break;
        case Status::CANCELLED:
            out.outcome = "cancelled";
            out.reason = "activation_cancelled";
            break;
        case Status::IDLE:
            return false;
    }

    out.activationOrigin = "track_paste";
    out.hasActivationGeneration = true;
    out.activationGeneration = telemetry.generation;
    return true;
}

FLASHMEM bool isSelectionScope(SequencerScope scope) {
    return scope == SequencerScope::TRACK_SELECTION ||
           scope == SequencerScope::PATTERN_SELECTION ||
           scope == SequencerScope::STEP_SELECTION;
}

FLASHMEM const char* modeForScope(SequencerScope scope) {
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
        case SequencerScope::STEP_CONTENT_SELECTOR:
            return "sequencer.step_content_selector";
        case SequencerScope::TRACK_SELECTION:
            return "sequencer.track_selection";
        case SequencerScope::PATTERN_SELECTION:
            return "sequencer.pattern_selection";
        case SequencerScope::STEP_SELECTION:
            return "sequencer.step_selection";
        case SequencerScope::STEP_EDITOR:
            return "sequencer.step_edit";
    }
    return "sequencer";
}

FLASHMEM bool policyScopeTargetsTrack(SequencerScope scope) {
    return scope == SequencerScope::TRACK ||
           scope == SequencerScope::TRACK_SELECTION;
}

FLASHMEM bool policyScopeTargetsStep(SequencerScope scope) {
    return scope == SequencerScope::STEP ||
           scope == SequencerScope::STEP_SELECTION ||
           scope == SequencerScope::STEP_EDITOR;
}

FLASHMEM const char* targetForPolicyScope(SequencerScope scope) {
    if (policyScopeTargetsTrack(scope)) return "track";
    if (policyScopeTargetsStep(scope)) return "step";
    return "pattern";
}

FLASHMEM const char* sequencerContextTarget(
    core::state::StructureNavigationFocus focus
) {
    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK: return "track";
        case core::state::StructureNavigationFocus::STEP: return "step";
        case core::state::StructureNavigationFocus::PAGE:
        default: return "pattern";
    }
}

FLASHMEM const char* stepContentDraftKindName(
    core::state::sequencer::SequencerStepContentDraftKind kind
) {
    using Kind = core::state::sequencer::SequencerStepContentDraftKind;
    switch (kind) {
        case Kind::CHORD: return "chord";
        case Kind::MICRO_SEQUENCE: return "micro_sequence";
        case Kind::CYCLE_STATES: return "cycle_states";
        case Kind::NONE:
        default: return nullptr;
    }
}

FLASHMEM const char* stepContentDraftExitChoiceName(
    core::state::sequencer::SequencerStepContentDraftExitChoice choice
) {
    using Choice = core::state::sequencer::SequencerStepContentDraftExitChoice;
    switch (choice) {
        case Choice::CONTINUE: return "continue";
        case Choice::DISCARD: return "discard";
        case Choice::SAVE: return "save";
        case Choice::COUNT:
        default: return nullptr;
    }
}

FLASHMEM const char* stepContentDraftFailureName(
    core::state::sequencer::SequencerStepContentDraftFailure failure
) {
    using Failure = core::state::sequencer::SequencerStepContentDraftFailure;
    switch (failure) {
        case Failure::OUT_OF_MEMORY: return "out_of_memory";
        case Failure::HISTORY_UNAVAILABLE: return "history_unavailable";
        case Failure::PUBLISH_FAILED: return "publish_failed";
        case Failure::TRANSITION_BLOCKED: return "transition_blocked";
        case Failure::UNPUBLISHABLE_MUTATION: return "unpublishable_mutation";
        case Failure::NONE:
        default: return nullptr;
    }
}

FLASHMEM void fillActiveStepContentDraftFacts(
    const core::state::sequencer::SequencerState& sequencer,
    core::validation::ux::SemanticUxContext& out
) {
    const auto& draft = sequencer.stepContentDraft;
    if (!draft.active.get()) return;
    out.draftKind = stepContentDraftKindName(draft.kind.get());
    out.hasDraftActive = true;
    out.draftActive = true;
    out.hasDraftDirty = true;
    out.draftDirty = draft.modified();
    out.exitChoice = draft.exitPromptVisible.get()
        ? stepContentDraftExitChoiceName(draft.exitChoice.get())
        : nullptr;
    out.draftFailure = stepContentDraftFailureName(draft.failure);
    out.action = draft.exitPromptVisible.get()
        ? stepContentDraftExitChoiceName(draft.exitChoice.get())
        : "apply";
}

FLASHMEM bool isStepContentDraftTraceEvent(
    const oc::core::input::InputBindingTraceEvent& event
) {
    return isEncoder(event, Config::EncoderID::NAV) ||
           isEncoder(event, Config::EncoderID::OPT) ||
           isButton(event, Config::ButtonID::NAV,
                    oc::core::input::ButtonBindingType::RELEASE) ||
           isButton(event, Config::ButtonID::LEFT_TOP,
                    oc::core::input::ButtonBindingType::RELEASE) ||
           isButton(event, Config::ButtonID::BOTTOM_RIGHT,
                    oc::core::input::ButtonBindingType::PRESS) ||
           isButton(event, Config::ButtonID::BOTTOM_RIGHT,
                    oc::core::input::ButtonBindingType::RELEASE) ||
           isButton(event, Config::ButtonID::BOTTOM_RIGHT,
                    oc::core::input::ButtonBindingType::LONG_PRESS);
}

FLASHMEM SequencerAction structureActionForEvent(
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
    if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
        return policy.leftCenterPress;
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

FLASHMEM uint16_t sequencerPageMask(const core::state::sequencer::SequencerState& sequencer) {
    const uint8_t count = sequencer.activePageCount();
    if (count >= 16U) return 0xffffU;
    return static_cast<uint16_t>((1U << count) - 1U);
}

FLASHMEM bool fillResolvedStepUxContext(
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

FLASHMEM SequencerPropertySelectorUxSurface::SequencerPropertySelectorUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::sequencer::SequencerState& sequencer
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    sequencer_(sequencer) {}

FLASHMEM bool SequencerPropertySelectorUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        return false;
    }

    using ButtonType = oc::core::input::ButtonBindingType;
    const bool navPress = isButton(event, Config::ButtonID::NAV, ButtonType::PRESS);
    const bool navHold = isButton(event, Config::ButtonID::NAV, ButtonType::LONG_PRESS);
    const bool navRelease = isButton(event, Config::ButtonID::NAV, ButtonType::RELEASE);
    const bool navTurn = isEncoder(event, Config::EncoderID::NAV);
    const bool contextSelectorEvent = navPress || navHold || navRelease || navTurn;
    const bool mainContextAvailable =
        !sequencer_.structureUi.stepSelection.active.get() &&
        !sequencer_.stepEdit.visible.get() &&
        !sequencer_.stepContentSelector.selecting.get() &&
        !sequencer_.stepPropertyInlineSelector.selecting.get() &&
        !sequencer_.patternQuickControls.selecting.get() &&
        !sequencer_.ccLaneUi.visible();

    const auto writeContextSelectorRelease = [this, &out]() {
        out.mode = "sequencer.context_selector";
        out.target = sequencerContextTarget(context_selector_target_);
        out.targetIndex = static_cast<int16_t>(context_selector_target_);
        out.property = "context";
        if (context_selector_rotated_) {
            out.effect = "apply_context";
            out.projection = "applied";
            out.outcome = "applied";
        } else if (context_selector_held_) {
            out.effect = "dismiss_context_preview";
            out.projection = "applied";
            out.outcome = "dismissed";
        } else if (context_selector_target_ ==
                   core::state::StructureNavigationFocus::STEP) {
            out.effect = "open_step_editor";
            out.projection = "applied";
            out.outcome = "applied";
            out.targetStep = static_cast<int16_t>(sequencer_.focusedStep.get());
        } else if (context_selector_target_ ==
                   core::state::StructureNavigationFocus::PAGE) {
            out.effect = "open_pattern_editor";
            out.projection = "applied";
            out.outcome = "applied";
        } else {
            out.effect = "open_track_editor";
            out.projection = "applied";
            out.outcome = "applied";
        }
        return true;
    };

    // A completed release is projected more than once by the recorder: once
    // after dispatch and once for the named capture. Keep only that release
    // projection cached; the first different input retires it without
    // classifying the new input as a selector gesture.
    if (context_selector_release_cached_ && navRelease) {
        return writeContextSelectorRelease();
    }
    if (context_selector_release_cached_) {
        context_selector_release_cached_ = false;
        context_selector_rotated_ = false;
        context_selector_held_ = false;
    }

    if (navPress && mainContextAvailable) {
        context_selector_seen_ = true;
        context_selector_rotated_ = false;
        context_selector_held_ = false;
        context_selector_release_cached_ = false;
        context_selector_target_ = navigation_focus_.get();
    }
    if (contextSelectorEvent && context_selector_seen_) {
        if (sequencer_.contextSelector.visible) {
            context_selector_target_ = sequencer_.contextSelector.previewFocus;
        }
        if (navTurn) context_selector_rotated_ = true;
        if (navHold) context_selector_held_ = true;

        out.mode = "sequencer.context_selector";
        out.target = sequencerContextTarget(context_selector_target_);
        out.targetIndex = static_cast<int16_t>(context_selector_target_);
        out.property = "context";

        if (!navRelease) {
            out.effect = context_selector_rotated_
                ? "preview_context"
                : (context_selector_held_ ? "inspect_context" : "show_context");
            out.projection = "preview";
            out.outcome = "preview";
            return true;
        }

        const bool postDispatchRelease =
            !sequencer_.contextSelector.visible ||
            sequencer_.contextSelector.feedback !=
                core::state::sequencer::SequencerContextSelectorFeedback::NONE;
        if (postDispatchRelease && context_selector_rotated_) {
            context_selector_target_ = navigation_focus_.get();
        }
        const bool wroteRelease = writeContextSelectorRelease();
        if (postDispatchRelease) {
            context_selector_seen_ = false;
            context_selector_release_cached_ = true;
        }
        return wroteRelease;
    }

    const bool leftBottomPress = isButton(
        event,
        Config::ButtonID::LEFT_BOTTOM,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool leftCenterPress = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool stepFocus = navigation_focus_.get() ==
        core::state::StructureNavigationFocus::STEP;
    const bool contentOpening = leftBottomPress && stepFocus &&
        !sequencer_.structureUi.stepSelection.active.get();
    if (contentOpening || sequencer_.stepContentSelector.selecting.get()) {
        const auto action = sequencer_.stepContentSelector.focusedAction.get();
        out.mode = "sequencer.step_content_selector";
        out.target = "step";
        out.targetStep = static_cast<int16_t>(sequencer_.focusedStep.get());
        out.property = stepContentActionName(action);
        copyValueLabel(out.valueLabel, out.property);
        if (contentOpening) {
            out.effect = "open_step_content_selector";
        } else if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_step_content_action";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_BOTTOM,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "apply_step_content_selector";
        } else if (isButton(
                       event,
                       Config::ButtonID::LEFT_TOP,
                       oc::core::input::ButtonBindingType::RELEASE
                   )) {
            out.effect = "cancel_step_content_selector";
        }
        return true;
    }

    const bool opening = leftBottomPress || leftCenterPress;
    if (!opening && !sequencer_.stepPropertyInlineSelector.selecting.get()) {
        return false;
    }

    out.mode = "sequencer.property_selector";
    out.target = "property";
    const int selected = sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const bool state =
        core::state::sequencer::sequencerPropertySelectionIsState(selected);
    const bool ccLanes = selected >=
        core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT;
    out.property = state
        ? "state"
        : (ccLanes
               ? "cc_lanes"
               : core::state::sequencer::stepPropertyName(
                     sequencer_.activeStepProperty.get()
                 ));
    if (state) {
        const uint8_t length =
            core::state::sequencer::activeContentLength(sequencer_);
        const uint8_t step = length == 0
            ? 0
            : std::min<uint8_t>(
                  sequencer_.focusedStep.get(),
                  static_cast<uint8_t>(length - 1U)
              );
        copyValueLabel(
            out.valueLabel,
            length > 0 && core::state::sequencer::activeContentStepEnabled(
                sequencer_,
                step
            ) ? "On" : "Off"
        );
    } else {
        std::snprintf(
            out.valueLabel,
            sizeof(out.valueLabel),
            "%u",
            static_cast<unsigned>(ccLanes
                ? (core::state::sequencer::sequencerCcLaneView(sequencer_.pattern)
                    ? core::state::sequencer::sequencerCcLaneCount(
                        *core::state::sequencer::sequencerCcLaneView(
                            sequencer_.pattern
                        )
                    )
                    : 0U)
                : sequencer_.variationRangeForProperty(
                      sequencer_.activeStepProperty.get()
                  ))
        );
    }
    if (opening) {
        out.effect = "open_property_selector";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_property";
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        out.effect = ccLanes
            ? "noop"
            : (state ? "edit_step_state" : "edit_variation_range");
        if (ccLanes) {
            out.outcome = "noop";
            out.reason = "enter_with_nav";
        }
    } else if (ccLanes &&
               isButton(event, Config::ButtonID::NAV,
                        oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "enter_cc_lane_selector";
    } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE) ||
               isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
        if (ccLanes) {
            const auto* bank =
                core::state::sequencer::sequencerCcLaneView(sequencer_.pattern);
            const bool add =
                core::state::sequencer::sequencerPropertySelectionIsAdd(
                    bank,
                    selected
                );
            out.effect = add ? "create_cc_lane" : "open_cc_lane";
            out.outcome = "applied";
        } else {
            out.effect = "apply_property";
        }
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "cancel_property";
    }
    return true;
}

FLASHMEM SequencerStepPresetUxSurface::SequencerStepPresetUxSurface(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackActivationQueue* trackActivations
) : sequencer_(sequencer), track_activations_(trackActivations) {}

FLASHMEM bool SequencerStepPresetUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    namespace seq = core::state::sequencer;
    namespace contextual = core::state::contextual;

    const auto& picker = sequencer_.stepPresetPicker;
    if (!picker.visible.get()) return false;

    const auto feedback = picker.operationFeedback.get();
    const bool feedbackIsSave = feedback.active &&
        feedback.action == contextual::ContextActionId::SAVE;
    const bool saveMode = feedbackIsSave ||
        (!feedback.active &&
         picker.mode.get() == seq::SequencerStepPresetPickerMode::SAVE);
    if (picker.detailVisible.get()) {
        out.mode = saveMode
            ? "sequencer.step_preset.save.detail"
            : "sequencer.step_preset.load.detail";
    } else {
        out.mode = saveMode
            ? "sequencer.step_preset.save"
            : "sequencer.step_preset.load";
    }

    const bool focusedAsset = picker.entryCount.get() > 0 &&
        !picker.selectedItemIsNewAsset() &&
        picker.existingEntryIndexForSelectedItem() < picker.entryCount.get();
    const auto spec = seq::buildSequencerStepPresetActionSpec(
        saveMode,
        picker.selectedItemIsNewAsset(),
        focusedAsset,
        picker.frozenTarget,
        picker.descriptor
    );
    const auto variant = contextual::hasHoldAction(spec)
        ? spec.hold
        : spec.tap;
    const auto action = feedback.active ? feedback.action : variant.action;

    out.target = "step";
    out.targetIndex = static_cast<int16_t>(picker.selectedIndex.get());
    if (picker.frozenTarget.valid) {
        out.targetStep = static_cast<int16_t>(picker.frozenTarget.stepIndex);
    }
    // Step presets deliberately describe content, never a destination MIDI
    // route. The frozen destination therefore remains authoritative on apply.
    out.routePolicy = "preserve_destination";
    out.projection = "preview";
    if (picker.descriptor.valid && picker.descriptor.technicalId[0] != '\0') {
        out.source = picker.descriptor.technicalId;
    } else if (focusedAsset) {
        out.source = picker.entryId(picker.existingEntryIndexForSelectedItem());
    } else if (saveMode && picker.selectedItemIsNewAsset()) {
        out.source = "new_step_preset";
    } else {
        out.source = "no_step_preset";
    }

    if (picker.detailVisible.get() && picker.descriptor.valid) {
        switch (picker.detailFocus.get()) {
            case 0:
                out.property = "target_context";
                copyValueLabel(out.valueLabel, picker.frozenTarget.contextLabel);
                break;
            case 1:
                out.property = "content";
                copyValueLabel(out.valueLabel, picker.descriptor.contentSummary);
                break;
            case 2:
                out.property = "impact";
                copyValueLabel(
                    out.valueLabel,
                    picker.descriptor.footprint == seq::SequencerStepPresetFootprint::REPLACE
                        ? "replace"
                        : "add"
                );
                break;
            case 3:
                out.property = "pitch_policy";
                copyValueLabel(
                    out.valueLabel,
                    picker.descriptor.mixedPitchPolicy
                        ? "mixed"
                        : (picker.descriptor.scalePolicy ==
                               seq::SequencerStepPresetScalePolicy::SCALE_RELATIVE
                            ? "scale_relative"
                            : "chromatic")
                );
                break;
            case 4:
            default:
                out.property = "preview_state";
                std::snprintf(
                    out.valueLabel,
                    sizeof(out.valueLabel),
                    "%u/%u",
                    static_cast<unsigned>(picker.previewStateIndex.get() + 1U),
                    static_cast<unsigned>(
                        picker.descriptor.previewStateCount > 0
                            ? picker.descriptor.previewStateCount
                            : 1U
                    )
                );
                break;
        }
    } else if (picker.descriptor.valid) {
        // Technical IDs are validated slugs and therefore safe for the
        // allocation-free NDJSON recorder. The user-facing semantic name is
        // already visible in the capture and may contain arbitrary UTF-8.
        out.property = "asset_id";
        copyValueLabel(out.valueLabel, out.source);
    } else if (picker.selectedItemIsNewAsset()) {
        out.property = "new_asset";
        copyValueLabel(out.valueLabel, "unsaved");
    } else {
        out.property = "asset_id";
        copyValueLabel(out.valueLabel, out.source);
    }

    if (feedback.active) {
        out.outcome = operationOutcomeName(feedback.status);
        out.reason = contextActionReasonName(feedback.reason);
    } else {
        out.outcome = guardedActionOutcomeName(picker.actionGuard.get().phase);
        out.reason = contextActionReasonName(variant.reason);
        if (!out.outcome) {
            switch (variant.availability) {
                case contextual::ContextActionAvailability::AVAILABLE:
                    out.outcome = "ready";
                    break;
                case contextual::ContextActionAvailability::WARNING:
                    out.outcome = "warning";
                    break;
                case contextual::ContextActionAvailability::DISABLED:
                    out.outcome = "blocked";
                    break;
            }
        }
    }
    if (feedback.active && picker.operationActivationGeneration != 0 &&
        picker.frozenTarget.valid && track_activations_ != nullptr) {
        using FeedbackStatus = contextual::OperationFeedbackStatus;
        using ActivationStatus = seq::SequencerTrackActivationStatus;
        const auto telemetry = track_activations_->telemetry(
            picker.frozenTarget.trackIndex
        );
        const bool terminalMatches =
            (feedback.status == FeedbackStatus::QUEUED &&
             telemetry.status == ActivationStatus::QUEUED) ||
            (feedback.status == FeedbackStatus::APPLIED &&
             telemetry.status == ActivationStatus::APPLIED) ||
            (feedback.status == FeedbackStatus::CANCELLED &&
             telemetry.status == ActivationStatus::CANCELLED);
        if (terminalMatches &&
            telemetry.generation == picker.operationActivationGeneration &&
            telemetry.origin == seq::SequencerTrackActivationOrigin::STEP_PRESET) {
            out.activationOrigin = "step_preset";
            out.hasActivationGeneration = true;
            out.activationGeneration = telemetry.generation;
        }
    }
    if (!out.reason && picker.descriptor.valid) {
        out.reason = stepPresetCompatibilityReasonName(
            picker.descriptor.compatibility
        );
    }
    out.hasConflict = true;
    out.conflict = feedback.active &&
        (feedback.status == contextual::OperationFeedbackStatus::CONFLICT ||
         feedback.reason == contextual::ContextActionReason::CONFLICT);

    using ButtonType = oc::core::input::ButtonBindingType;
    const bool actionGesture =
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, ButtonType::PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, ButtonType::RELEASE) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, ButtonType::LONG_PRESS);
    if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = picker.detailVisible.get()
            ? "focus_step_preset_detail"
            : "select_step_preset_asset";
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        if (picker.detailVisible.get() && picker.detailFocus.get() == 4U) {
            out.effect = "select_step_preset_preview_state";
        } else {
            out.effect = "noop";
            out.outcome = "noop";
            out.reason = "preview_state_not_focused";
        }
    } else if (isButton(event, Config::ButtonID::NAV, ButtonType::RELEASE)) {
        out.effect = picker.detailVisible.get()
            ? "show_step_preset_details"
            : "hide_step_preset_details";
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, ButtonType::RELEASE) ||
               isButton(event, Config::ButtonID::LEFT_CENTER, ButtonType::RELEASE) ||
               isButton(event, Config::ButtonID::BOTTOM_LEFT, ButtonType::RELEASE)) {
        out.effect = "close_step_preset_picker";
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_CENTER,
                   ButtonType::RELEASE
               )) {
        out.effect = saveMode
            ? "show_step_preset_save_mode"
            : "show_step_preset_load_mode";
    } else if (actionGesture) {
        out.effect = stepPresetActionName(action);
    }
    return true;
}

FLASHMEM SequencerCcLaneUxSurface::SequencerCcLaneUxSurface(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectNavigationState& projectNavigation,
    const core::state::project::ProjectTrackState& projectTracks,
    const core::handler::MidiCcGlobalFrameCoordinator* midiCcCoordinator
) : sequencer_(sequencer),
    tracks_(tracks),
    project_navigation_(projectNavigation),
    project_tracks_(projectTracks),
    midi_cc_coordinator_(midiCcCoordinator) {}

FLASHMEM bool SequencerCcLaneUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    namespace seq = core::state::sequencer;
    const auto& ui = sequencer_.ccLaneUi;
    seq::SequencerCcLaneActionSlot actionSlot =
        seq::SequencerCcLaneActionSlot::COUNT;
    core::validation::ux::SequencerCcLaneGesturePhase actionPhase =
        core::validation::ux::SequencerCcLaneGesturePhase::PRESS;
    const bool actionGesture = ccLaneActionGesture(event, actionSlot, actionPhase);
    if (actionGesture &&
        actionPhase == core::validation::ux::SequencerCcLaneGesturePhase::PRESS &&
        ui.visible()) {
        gesture_specs_[static_cast<size_t>(actionSlot)] = ui.action(actionSlot);
    }
    if (!ui.visible()) {
        if (!actionGesture ||
            actionPhase != core::validation::ux::SequencerCcLaneGesturePhase::RELEASE ||
            !ui.operationFeedback.get().active) {
            return false;
        }
        const auto semantic =
            core::validation::ux::classifySequencerCcLaneGesture(
                gesture_specs_[static_cast<size_t>(actionSlot)],
                ui.actionGuard.get(),
                ui.operationFeedback.get(),
                actionPhase
            );
        out.mode = "sequencer.cc_lane.closed";
        out.target = "cc_lane";
        out.targetIndex = ui.focusedLane;
        out.source = "sequencer_cc_lane";
        out.effect = semantic.effect;
        out.outcome = semantic.outcome;
        out.reason = core::validation::ux::sequencerCcLaneSemanticReasonName(
            semantic.reason
        );
        out.hasConflict = true;
        out.conflict = ui.laneConflict || ui.macroConflict;
        out.hasTargetRoute = true;
        out.targetRouteValid = ui.routeValid;
        if (!out.reason && !ui.routeValid) out.reason = "no_route";
        return true;
    }

    switch (ui.mode) {
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
            out.mode = "sequencer.cc_lane.selector";
            break;
        case seq::SequencerCcLaneUiMode::LANE_GRID:
            out.mode = "sequencer.cc_lane.grid";
            break;
        case seq::SequencerCcLaneUiMode::TRANSITION_PICKER:
            out.mode = "sequencer.cc_lane.transition_picker";
            break;
        case seq::SequencerCcLaneUiMode::LANE_SETTINGS:
            out.mode = "sequencer.cc_lane.settings";
            break;
        case seq::SequencerCcLaneUiMode::CLOSED:
            return false;
    }

    out.target = "cc_lane";
    out.targetIndex = ui.focusedLane;
    out.projection = ui.liveProjection ? "live" : "preview";
    out.source = "sequencer_cc_lane";
    out.hasConflict = true;
    out.conflict = ui.laneConflict || ui.macroConflict;
    out.hasTargetRoute = true;
    out.targetRouteValid = ui.routeValid;

    const seq::SequencerCcLane* lane = nullptr;
    const auto* bank = seq::sequencerCcLaneView(sequencer_.pattern);
    if (bank && ui.focusedLane < bank->lanes.size() &&
        bank->lanes[ui.focusedLane].occupied) {
        lane = &bank->lanes[ui.focusedLane];
    }
    const auto& destination =
        ui.mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS
            ? ui.draft.destination
            : (lane ? lane->destination : ui.draft.destination);
    out.controller = destination.controller;
    if (ui.focusedLane < project_navigation_.ccLaneDefaultControllers.size()) {
        out.defaultController =
            project_navigation_.ccLaneDefaultControllers[ui.focusedLane];
    }
    out.routePolicy = destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED
        ? "pinned" : "inherit_track";
    out.targetRoute = destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED
        ? destination.pinnedChannel
        : core::state::project::projectTrackMidiChannel(
              project_tracks_,
              tracks_.activeTrackIndex()
          );

    // Winner is meaningful only for a real collision. A preview describes the
    // authored lane's future arbitration, so it must stay on preflight even if
    // another author already appears in stopped-runtime telemetry. Once the
    // lane is Live, prefer the singular runtime arbiter's observed winner.
    if (out.conflict) {
        const core::state::shared::MidiCcDestinationIdentity targetIdentity{
            .port = destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED
                ? destination.pinnedPort
                : core::handler::MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
            .channel = out.targetRoute,
            .controller = destination.controller,
        };
        if (ui.liveProjection && midi_cc_coordinator_ != nullptr) {
            const auto telemetryView = midi_cc_coordinator_->readTelemetry();
            const size_t destinationCount = telemetryView &&
                    telemetryView->destinationCount <
                        telemetryView->destinations.size()
                ? telemetryView->destinationCount
                : (telemetryView ? telemetryView->destinations.size() : 0U);
            for (size_t i = 0; i < destinationCount; ++i) {
                if (!core::state::shared::sameMidiCcDestinationIdentity(
                        telemetryView->destinations[i].destination.identity,
                        targetIdentity
                    )) {
                    continue;
                }
                out.winner = ccWinnerName(
                    telemetryView->destinations[i].winner.author.candidateClass
                );
                out.winnerSource = "runtime_telemetry";
                break;
            }
        }
        if (!out.winner) {
            if (ui.laneConflict) {
                out.winner = "existing_cc_lane";
                out.winnerSource = "preflight";
            } else if (ui.macroConflict) {
                const bool committedLane = lane != nullptr;
                out.winner = committedLane ? "sequencer_cc_lane" : "macro";
                out.winnerSource = "preflight";
            }
        }
    }
    out.property = "cc_value";
    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID) {
        out.targetStep = ui.focusedStep;
        out.hasAuthoredValue = ui.hasAuthoredValue;
        out.authoredValue = ui.authoredValue;
        out.hasResolvedValue = ui.hasResolvedValue;
        out.resolvedValue = ui.resolvedValue;
        std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s",
                      ui.hasAuthoredValue ? "authored" : "--");
    } else if (ui.mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER) {
        out.property = "cc_transition";
        out.targetStep = ui.transitionStep;
        out.hasAuthoredValue = true;
        out.authoredValue = static_cast<int32_t>(ui.selectedTransition);
    } else if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS) {
        switch (ui.focusedField) {
            case seq::SequencerCcLaneDraftField::CONTROLLER: out.property = "controller"; break;
            case seq::SequencerCcLaneDraftField::ROUTE_POLICY: out.property = "route_policy"; break;
            case seq::SequencerCcLaneDraftField::PINNED_CHANNEL: out.property = "channel"; break;
            case seq::SequencerCcLaneDraftField::MINIMUM: out.property = "minimum"; break;
            case seq::SequencerCcLaneDraftField::MAXIMUM: out.property = "maximum"; break;
            case seq::SequencerCcLaneDraftField::INITIAL: out.property = "initial"; break;
            case seq::SequencerCcLaneDraftField::ADVANCED: out.property = "advanced"; break;
            case seq::SequencerCcLaneDraftField::COUNT: break;
        }
    }

    uint8_t macroEncoderIndex = 0;
    const bool macroEncoder = isMacroEncoderTurn(event, macroEncoderIndex);
    uint8_t macroButtonIndex = 0;
    const bool macroButtonRelease =
        isMacroButtonRelease(event, macroButtonIndex);

    if (macroEncoder) {
        out.effect = ui.mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER
            ? "select_cc_transition"
            : "edit_visible_cc_event";
    } else if (macroButtonRelease) {
        out.effect = ui.transitionAppliedFeedback
            ? "apply_cc_transition"
            : "toggle_visible_cc_event";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID
            ? "focus_cc_step"
            : (ui.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR
                ? "select_cc_lane"
                : (ui.mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER
                    ? "select_cc_transition" : "focus_cc_field"));
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR) {
            out.effect = "noop";
            out.outcome = "noop";
            out.reason = "no_lane_selected_edit";
        } else {
            out.effect = ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID
                ? "edit_cc_event" : "edit_cc_draft";
        }
    } else if (actionGesture) {
        const auto& gestureSpec = actionPhase ==
                core::validation::ux::SequencerCcLaneGesturePhase::RELEASE
            ? gesture_specs_[static_cast<size_t>(actionSlot)]
            : ui.action(actionSlot);
        const auto semantic =
            core::validation::ux::classifySequencerCcLaneGesture(
                gestureSpec,
                ui.actionGuard.get(),
                ui.operationFeedback.get(),
                actionPhase
            );
        out.effect = semantic.effect;
        out.outcome = semantic.outcome;
        out.reason =
            core::validation::ux::sequencerCcLaneSemanticReasonName(
                semantic.reason
            );
    } else if (isButton(event, Config::ButtonID::NAV,
                        oc::core::input::ButtonBindingType::RELEASE)) {
        if (ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID) {
            out.effect = ui.transitionAppliedFeedback
                ? "apply_cc_transition"
                : "toggle_cc_event";
        } else if (ui.mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER) {
            out.effect = "apply_cc_transition";
        } else if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS &&
                   ui.focusedField == seq::SequencerCcLaneDraftField::ADVANCED) {
            out.effect = "toggle_cc_advanced";
        } else {
            out.effect = "enter_cc_lane";
        }
    } else if (isButton(event, Config::ButtonID::LEFT_TOP,
                        oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "back_cc_lane";
    } else if (isButton(event, Config::ButtonID::LEFT_CENTER,
                        oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = "open_property_selector_from_cc_lane";
    }
    const auto operation = ui.operationFeedback.get();
    if (isButton(event, Config::ButtonID::LEFT_BOTTOM,
                 oc::core::input::ButtonBindingType::RELEASE) &&
        operation.active &&
        operation.action == core::state::contextual::ContextActionId::CREATE &&
        operation.status ==
            core::state::contextual::OperationFeedbackStatus::APPLIED) {
        out.effect = "create_cc_lane";
        out.outcome = "applied";
    }
    if (!out.reason) {
        if (!ui.routeValid) out.reason = "no_route";
        else if (ui.laneConflict) out.reason = "lane_duplicate";
        else if (ui.macroConflict) out.reason = "macro_conflict";
    }
    (void)tracks_;
    return true;
}

FLASHMEM SequencerQuickControlsUxSurface::SequencerQuickControlsUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::sequencer::SequencerState& sequencer
) : active_view_(activeView), sequencer_(sequencer) {}

FLASHMEM bool SequencerQuickControlsUxSurface::captureSemanticUxContext(
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
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_quick_control";
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        out.effect = "edit_quick_control";
    } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_quick_controls";
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "cancel_quick_controls";
    } else {
        return false;
    }
    return true;
}

namespace {

FLASHMEM const char* patternEditorFieldSemanticName(
    core::state::sequencer::SequencerPatternEditorField field
) {
    using Field = core::state::sequencer::SequencerPatternEditorField;
    switch (field) {
        case Field::LENGTH: return "length";
        case Field::DIVISION: return "division";
        case Field::SWING: return "swing";
        case Field::NUDGE: return "pattern_nudge";
        case Field::PLAY_START: return "play_start";
        case Field::LOOP_START: return "loop_start";
        case Field::LOOP_END: return "loop_end";
        case Field::COUNT:
        default: return "pattern";
    }
}

FLASHMEM const char* patternRandomizeFieldSemanticName(
    core::state::sequencer::SequencerPatternRandomizeField field
) {
    using Field = core::state::sequencer::SequencerPatternRandomizeField;
    switch (field) {
        case Field::PROPERTY: return "property";
        case Field::AMOUNT: return "amount";
        case Field::RANGE: return "range";
        case Field::SCOPE: return "scope";
        case Field::COUNT:
        default: return "randomize";
    }
}

FLASHMEM const char* patternRandomizePropertySemanticName(
    core::state::sequencer::SequencerPatternRandomizeProperty property
) {
    using Property = core::state::sequencer::SequencerPatternRandomizeProperty;
    switch (property) {
        case Property::NOTE: return "note";
        case Property::VELOCITY: return "velocity";
        case Property::GATE: return "gate";
        case Property::NUDGE: return "nudge";
        case Property::PROBABILITY: return "chance";
    }
    return "note";
}

}  // namespace

FLASHMEM SequencerPatternEditorUxSurface::SequencerPatternEditorUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerPatternRandomizeSession& randomize
) : active_view_(activeView), sequencer_(sequencer), randomize_(randomize) {}

FLASHMEM bool SequencerPatternEditorUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER ||
        !sequencer_.patternEditor.active.get()) {
        return false;
    }

    const bool navTurn = isEncoder(event, Config::EncoderID::NAV);
    const bool optTurn = isEncoder(event, Config::EncoderID::OPT);
    const bool windowPress = isButton(
        event, Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool windowRelease = isButton(
        event, Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool layerPress = isButton(
        event, Config::ButtonID::LEFT_BOTTOM,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool layerRelease = isButton(
        event, Config::ButtonID::LEFT_BOTTOM,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool back = isButton(
        event, Config::ButtonID::LEFT_TOP,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool reroll = isButton(
        event, Config::ButtonID::BOTTOM_LEFT,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool apply = isButton(
        event, Config::ButtonID::BOTTOM_RIGHT,
        oc::core::input::ButtonBindingType::RELEASE
    );
    if (!navTurn && !optTurn && !windowPress &&
        !windowRelease && !layerPress && !layerRelease && !back && !reroll &&
        !apply) {
        return false;
    }

    out.mode = randomize_.active
        ? "sequencer.pattern_editor.randomize"
        : "sequencer.pattern_editor";
    out.target = "pattern";
    out.targetIndex = sequencer_.patternEditor.ownerTrack;
    out.targetPage = static_cast<int16_t>(
        sequencer_.patternEditor.windowStart /
            core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    out.targetStep = sequencer_.patternEditor.windowStart;
    out.targetCount = sequencer_.pattern.length.get();
    out.hasDraftActive = true;
    out.draftActive = randomize_.active;

    if (randomize_.active) {
        out.property = patternRandomizeFieldSemanticName(randomize_.focusedField);
        out.source = patternRandomizePropertySemanticName(randomize_.draft.property);
        out.projection = "preview";
        std::snprintf(
            out.valueLabel, sizeof(out.valueLabel), "%u/%u",
            static_cast<unsigned>(randomize_.summary.changedCount),
            static_cast<unsigned>(randomize_.summary.eligibleCount)
        );
    } else {
        out.property = patternEditorFieldSemanticName(
            sequencer_.patternEditor.focusedField
        );
        std::snprintf(
            out.valueLabel, sizeof(out.valueLabel), "%d",
            static_cast<int>(core::state::sequencer::patternEditorFieldValue(
                sequencer_, sequencer_.patternEditor.focusedField
            ))
        );
    }

    if (navTurn) {
        out.effect = randomize_.active
            ? "select_pattern_randomize_field"
            : (sequencer_.patternEditor.navigationMode ==
                       core::state::sequencer::SequencerPatternEditorNavigationMode::WINDOWS
                   ? "move_pattern_window"
                   : (sequencer_.patternEditor.navigationMode ==
                              core::state::sequencer::SequencerPatternEditorNavigationMode::LAYERS
                          ? "select_pattern_layer"
                          : "select_pattern_field"));
    } else if (optTurn) {
        out.effect = randomize_.active
            ? "preview_pattern_randomize"
            : "edit_pattern_field";
    } else if (reroll && !randomize_.active) {
        out.effect = "open_pattern_randomize";
        out.outcome = "preview";
    } else if (windowPress) {
        out.effect = "open_pattern_windows";
    } else if (windowRelease) {
        out.effect = "apply_pattern_window";
    } else if (layerPress) {
        out.effect = "open_pattern_layers";
    } else if (layerRelease) {
        out.effect = "apply_pattern_layer";
    } else if (reroll && randomize_.active) {
        out.effect = "reroll_pattern_randomize";
        out.outcome = "preview";
    } else if (apply && randomize_.active) {
        out.effect = "apply_pattern_randomize";
        out.outcome = randomize_.summary.changedCount > 0U
            ? "applied"
            : "no_change";
    } else if (back) {
        out.effect = randomize_.active
            ? "cancel_pattern_randomize"
            : "close_pattern_editor";
        if (randomize_.active) out.outcome = "cancelled";
    } else {
        return false;
    }
    return true;
}

FLASHMEM ProjectTrackEditorUxSurface::ProjectTrackEditorUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::project::ProjectTrackEditorState& editor,
    core::state::project::ProjectTrackState& tracks
) : active_view_(activeView),
    editor_(editor),
    tracks_(tracks) {}

FLASHMEM bool ProjectTrackEditorUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    using ButtonType = oc::core::input::ButtonBindingType;
    if (active_view_.get() != core::ui::ViewType::SEQUENCER ||
        !editor_.active) {
        editor_state_seen_ = false;
        return false;
    }

    const uint8_t track = editor_.trackIndex;
    const uint8_t property = static_cast<uint8_t>(editor_.selectedProperty);
    const bool opening = !editor_state_seen_;
    const bool trackChanged = editor_state_seen_ && track != observed_track_;
    const bool propertyChanged =
        editor_state_seen_ && property != observed_property_;
    const auto syncObservedState = [this, track, property]() {
        editor_state_seen_ = true;
        observed_track_ = track;
        observed_property_ = property;
    };

    const bool trackPress = isButton(
        event, Config::ButtonID::LEFT_CENTER, ButtonType::PRESS
    );
    const bool trackRelease = isButton(
        event, Config::ButtonID::LEFT_CENTER, ButtonType::RELEASE
    );
    const bool propertyPress = isButton(
        event, Config::ButtonID::LEFT_BOTTOM, ButtonType::PRESS
    );
    const bool propertyRelease = isButton(
        event, Config::ButtonID::LEFT_BOTTOM, ButtonType::RELEASE
    );
    const bool navTurn = isEncoder(event, Config::EncoderID::NAV);
    const bool navRelease = isButton(
        event, Config::ButtonID::NAV, ButtonType::RELEASE
    );
    const bool optTurn = isEncoder(event, Config::EncoderID::OPT);
    const bool mute = isButton(
        event, Config::ButtonID::BOTTOM_LEFT, ButtonType::RELEASE
    );
    const bool solo = isButton(
        event, Config::ButtonID::BOTTOM_CENTER, ButtonType::RELEASE
    );
    const bool back = isButton(
        event, Config::ButtonID::LEFT_TOP, ButtonType::RELEASE
    );
    const bool projection = isSemanticStateProjection(event);

    if (!trackPress && !trackRelease && !propertyPress && !propertyRelease &&
        !navTurn && !navRelease && !optTurn && !mute && !solo &&
        !back && !projection) {
        syncObservedState();
        return false;
    }

    out.mode = "sequencer.track_editor";
    out.target = "track";
    out.targetIndex = track;
    out.targetTrack = track;
    out.projection = "applied";
    out.property = editor_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::DELAY
        ? "delay"
        : "channel";
    if (editor_.selectedProperty ==
        core::state::project::ProjectTrackEditorProperty::DELAY) {
        std::snprintf(
            out.valueLabel,
            sizeof(out.valueLabel),
            "%+d ms",
            static_cast<int>(core::state::project::projectTrackDelayMs(
                tracks_, track
            ))
        );
    } else {
        out.hasAuthoredValue = true;
        out.authoredValue = core::state::project::projectTrackMidiChannel(
            tracks_, track
        );
        std::snprintf(
            out.valueLabel,
            sizeof(out.valueLabel),
            "USB CH %u",
            static_cast<unsigned>(
                core::state::project::projectTrackMidiChannel(tracks_, track) + 1U
            )
        );
    }

    if (trackPress) {
        out.effect = "open_track_selector";
        out.projection = "preview";
    } else if (propertyPress) {
        out.effect = "open_track_property_selector";
        out.projection = "preview";
    } else if (navTurn && trackChanged) {
        out.effect = "switch_track";
    } else if (navTurn && propertyChanged) {
        out.effect = "select_track_property";
    } else if (navTurn) {
        // Dispatch tracing captures this surface once before the handler and
        // once after it. Leave the pre-effect empty so the post-state delta
        // above supplies the durable semantic action.
        out.effect = nullptr;
    } else if (optTurn) {
        out.effect = "edit_track_property";
    } else if (trackRelease) {
        out.effect = "apply_track";
    } else if (propertyRelease) {
        out.effect = "apply_track_property";
    } else if (mute) {
        out.property = "mute";
        out.effect = "toggle_track_mute";
        out.hasAuthoredValue = true;
        out.authoredValue = core::state::project::projectTrackMuted(
            tracks_, track
        ) ? 1U : 0U;
        std::snprintf(
            out.valueLabel, sizeof(out.valueLabel), "%s",
            out.authoredValue != 0U ? "ON" : "OFF"
        );
    } else if (solo) {
        out.property = "solo";
        out.effect = "toggle_track_solo";
        out.hasAuthoredValue = true;
        out.authoredValue = core::state::project::projectTrackSoloed(
            tracks_, track
        ) ? 1U : 0U;
        std::snprintf(
            out.valueLabel, sizeof(out.valueLabel), "%s",
            out.authoredValue != 0U ? "ON" : "OFF"
        );
    } else if (back) {
        out.effect = "close_track_editor";
    } else if (navRelease && opening) {
        out.effect = "open_track_editor";
    } else {
        out.effect = "inspect_track";
    }
    syncObservedState();
    return true;
}

FLASHMEM SequencerStructureUxSurface::SequencerStructureUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::StructureClipboardState& structureClipboard,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    const core::state::sequencer::SequencerTrackActivationQueue* trackActivations,
    const core::validation::ux::StructureUxTraceState* traceState
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    structure_clipboard_(structureClipboard),
    sequencer_(sequencer),
    tracks_(tracks),
    project_tracks_(projectTracks),
    track_activations_(trackActivations),
    trace_state_(traceState) {}

FLASHMEM bool SequencerStructureUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        return false;
    }

    const bool leftTopRelease =
        isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE);
    const bool stateProjection = isSemanticStateProjection(event);
    const bool trackPasteDetailsEvent =
        isButton(
            event,
            Config::ButtonID::BOTTOM_CENTER,
            oc::core::input::ButtonBindingType::PRESS
        ) ||
        isButton(
            event,
            Config::ButtonID::BOTTOM_CENTER,
            oc::core::input::ButtonBindingType::RELEASE
        );
    const bool structureEvent =
        stateProjection ||
        trackPasteDetailsEvent ||
        isEncoder(event, Config::EncoderID::NAV) ||
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS) ||
        leftTopRelease ||
        isButton(event, Config::ButtonID::LEFT_CENTER,
                 oc::core::input::ButtonBindingType::RELEASE) ||
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
    if (action == SequencerAction::NONE && !stateProjection &&
        !trackPasteDetailsEvent) {
        return false;
    }

    const bool selectionActive = isSelectionScope(policy.scope);
    out.mode = modeForScope(policy.scope);
    out.target = selectionActive ? "step" : targetForPolicyScope(policy.scope);

    uint8_t index = 0;
    if (selectionActive) {
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

    const bool targetTrack = policyScopeTargetsTrack(policy.scope);
    const uint16_t targetMask = targetTrack ? tracks_.currentEnabledMask() : sequencerPageMask(sequencer_);
    out.targetMask = targetMask;

    if (targetTrack) {
        index = track_navigation_.previewAddSlot.get()
            ? track_navigation_.previewTrackIndex.get()
            : tracks_.activeTrackIndex();
        out.property = track_navigation_.previewAddSlot.get()
            ? "add_slot"
            : "existing";
    } else {
        index = sequencer_.structureUi.previewPageIndex.get();
        out.property = sequencer_.structureUi.previewAddPageSlot.get()
            ? "add_slot"
            : "existing";
    }
    out.targetIndex = static_cast<int16_t>(index);
    copyIndexLabel(out.valueLabel, index);

    core::state::ClipboardTransferPlan trackTransferPlan{};
    core::state::contextual::ContextActionSpec trackTransferAction{};
    const auto* trackPasteLifecycle = targetTrack
        ? &sequencer_.structureUi.trackPaste
        : nullptr;
    bool canPaste = structure_clipboard_.hasSequencerPage();
    if (targetTrack) {
        trackTransferPlan = core::state::buildSequencerTrackClipboardTransferPlan(
            structure_clipboard_,
            tracks_,
            project_tracks_,
            index,
            track_activations_ != nullptr
                ? track_activations_->pendingTrackMask()
                : 0
        );
        if (trackPasteLifecycle->feedback.active &&
            trackPasteLifecycle->plan.hasEntries()) {
            trackTransferPlan = trackPasteLifecycle->plan;
        }
        trackTransferAction =
            core::state::sequencer::buildSequencerTrackTransferActionSpec(
                trackTransferPlan,
                index,
                !track_navigation_.previewAddSlot.get(),
                static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            );
        canPaste = core::state::contextual::canExecute(trackTransferAction.hold);
    }

    const bool trackPasteLifecycleEvent =
        targetTrack && isPasteAction(action) &&
        (isButton(
             event,
             Config::ButtonID::BOTTOM_RIGHT,
             oc::core::input::ButtonBindingType::PRESS
         ) ||
         isButton(
             event,
             Config::ButtonID::BOTTOM_RIGHT,
             oc::core::input::ButtonBindingType::RELEASE
         ) ||
         isButton(
             event,
             Config::ButtonID::BOTTOM_RIGHT,
             oc::core::input::ButtonBindingType::LONG_PRESS
         ));
    bool projectedTrackPasteActivation = false;
    if (trackPasteLifecycleEvent && track_activations_ != nullptr &&
        trackPasteLifecycle->activationGeneration != 0 &&
        trackTransferPlan.hasEntries()) {
        const uint8_t activationTarget =
            trackTransferPlan.entry.targetTrack;
        if (activationTarget <
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
            projectedTrackPasteActivation = fillTrackPasteActivationFacts(
                track_activations_->telemetry(activationTarget),
                trackPasteLifecycle->activationGeneration,
                out
            );
        }
    }

    const bool projectedTrackPasteLifecycle =
        targetTrack && trackPasteLifecycle->feedback.active &&
        trackTransferPlan.hasEntries();
    if (projectedTrackPasteLifecycle) {
        fillTrackTransferFacts(trackTransferPlan, trackPasteLifecycle, out);
        if (track_activations_ != nullptr &&
            trackPasteLifecycle->activationGeneration != 0) {
            const uint8_t target = trackTransferPlan.entry.targetTrack;
            if (target < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
                projectedTrackPasteActivation = fillTrackPasteActivationFacts(
                    track_activations_->telemetry(target),
                    trackPasteLifecycle->activationGeneration,
                    out
                ) || projectedTrackPasteActivation;
            }
        }
        if (!projectedTrackPasteActivation) {
            out.outcome = operationOutcomeName(trackPasteLifecycle->feedback.status);
        }

        using Feedback = core::state::contextual::OperationFeedbackStatus;
        const auto status = trackPasteLifecycle->feedback.status;
        if (status == Feedback::PRESSED || status == Feedback::ARMED) {
            out.effect = "arm_paste_current_structure";
            return true;
        }
        if (status == Feedback::QUEUED || status == Feedback::APPLIED) {
            out.effect = "paste_current_structure";
            return true;
        }
        if (status == Feedback::CANCELLED) {
            out.effect = "cancel_track_paste";
            return true;
        }
        if (stateProjection || trackPasteLifecycle->detailVisible) {
            out.effect = trackPasteLifecycle->detailVisible
                ? "inspect_track_paste_details"
                : "inspect_track_paste_summary";
            return true;
        }
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
            fillTrackTransferFacts(trackTransferPlan, trackPasteLifecycle, out);
        }
        if (isPasteAction(action) && !canPaste) {
            markNoop(
                out,
                targetTrack
                    ? core::validation::ux::sequencerTrackTransferSemanticReason(
                          trackTransferAction.hold.reason
                      )
                    : "clipboard_empty"
            );
        } else if (targetTrack && isPasteAction(action) &&
                   trackTransferAction.hold.availability ==
                       core::state::contextual::ContextActionAvailability::WARNING) {
            out.outcome = "warning";
            out.reason = core::validation::ux::sequencerTrackTransferSemanticReason(
                trackTransferAction.hold.reason
            );
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
            fillTrackTransferFacts(trackTransferPlan, trackPasteLifecycle, out);
        }
        if (!projectedTrackPasteActivation && isPasteAction(action) && !canPaste) {
            markNoop(
                out,
                targetTrack
                    ? core::validation::ux::sequencerTrackTransferSemanticReason(
                          trackTransferAction.hold.reason
                      )
                    : "clipboard_empty"
            );
        }
    } else {
        out.effect = actionName(action);
    }
    return true;
}

FLASHMEM SequencerStepGridUxSurface::SequencerStepGridUxSurface(
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

FLASHMEM bool SequencerStepGridUxSurface::captureSemanticUxContext(
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
    const bool stateProperty = sequencer_.stepStatePropertyActive.get() &&
        action != SequencerAction::TOGGLE_SELECTION;
    out.property = action == SequencerAction::TOGGLE_SELECTION
        ? (sequencer_.structureUi.stepSelection.selected(step) ? "selected" : "cursor")
        : (stateProperty
               ? "state"
               : core::state::sequencer::stepPropertyName(property));
    out.effect = actionName(action);
    fillResolvedStepUxContext(sequencer_, tracks_, step, property, out);
    if (stateProperty) {
        copyValueLabel(
            out.valueLabel,
            core::state::sequencer::activeContentStepEnabled(sequencer_, step)
                ? "On"
                : "Off"
        );
    }
    fillActiveStepContentDraftFacts(sequencer_, out);
    return true;
}

FLASHMEM SequencerStepEditUxSurface::SequencerStepEditUxSurface(
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

FLASHMEM bool SequencerStepEditUxSurface::captureSemanticUxContext(
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

    namespace seq = core::state::sequencer;
    const auto& draft = sequencer_.stepContentDraft;
    const bool draftActive = draft.active.get();
    const bool bottomRightRelease = isButton(
        event,
        Config::ButtonID::BOTTOM_RIGHT,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool navRelease = isButton(
        event,
        Config::ButtonID::NAV,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool leftTopRelease = isButton(
        event,
        Config::ButtonID::LEFT_TOP,
        oc::core::input::ButtonBindingType::RELEASE
    );

    if (draftActive) {
        draft_trace_seen_ = true;
        draft_trace_kind_ = static_cast<uint8_t>(draft.kind.get());
        draft_trace_dirty_ = draft.modified();
        draft_trace_exit_choice_ = static_cast<uint8_t>(draft.exitChoice.get());
        if (draft.exitPromptVisible.get()) {
            draft_trace_action_ = stepContentDraftExitChoiceName(
                draft.exitChoice.get()
            );
        } else if (leftTopRelease && !draft_trace_dirty_) {
            draft_trace_action_ = "discard";
        } else {
            draft_trace_action_ = "apply";
        }
    } else if (draft_trace_seen_) {
        const bool matchingPublishedTransition =
            (draft_trace_action_ != nullptr &&
             std::strcmp(draft_trace_action_, "apply") == 0 &&
             bottomRightRelease) ||
            (draft_trace_action_ != nullptr &&
             (std::strcmp(draft_trace_action_, "save") == 0 ||
              std::strcmp(draft_trace_action_, "continue") == 0 ||
              std::strcmp(draft_trace_action_, "discard") == 0) &&
             (navRelease || leftTopRelease));
        if (!matchingPublishedTransition) {
            draft_trace_seen_ = false;
            draft_trace_kind_ = 0;
            draft_trace_dirty_ = false;
            draft_trace_exit_choice_ = 0;
            draft_trace_action_ = nullptr;
        }
    }

    const auto fillDraftFacts = [this, &draft, draftActive](
        core::validation::ux::SemanticUxContext& context
    ) {
        if (!draftActive && !draft_trace_seen_) return;
        context.draftKind = stepContentDraftKindName(
            draftActive
                ? draft.kind.get()
                : static_cast<seq::SequencerStepContentDraftKind>(
                      draft_trace_kind_
                  )
        );
        context.hasDraftActive = true;
        context.draftActive = draftActive;
        if (!draftActive && draft_trace_action_ != nullptr) {
            context.hasPublished = true;
            context.published =
                std::strcmp(draft_trace_action_, "apply") == 0 ||
                std::strcmp(draft_trace_action_, "save") == 0;
        }
        context.hasDraftDirty = true;
        context.draftDirty = draftActive ? draft.modified() : draft_trace_dirty_;
        if (draftActive && draft.exitPromptVisible.get()) {
            context.exitChoice = stepContentDraftExitChoiceName(
                draft.exitChoice.get()
            );
        } else if (!draftActive && draft_trace_action_ != nullptr &&
                   std::strcmp(draft_trace_action_, "apply") != 0) {
            context.exitChoice = stepContentDraftExitChoiceName(
                static_cast<seq::SequencerStepContentDraftExitChoice>(
                    draft_trace_exit_choice_
                )
            );
        }
        context.draftFailure = draftActive
            ? stepContentDraftFailureName(draft.failure)
            : nullptr;
        context.action = draftActive
            ? (draft.exitPromptVisible.get()
                   ? stepContentDraftExitChoiceName(draft.exitChoice.get())
                   : draft_trace_action_)
            : draft_trace_action_;
    };

    const bool draftTraceEvent = isStepContentDraftTraceEvent(event);
    if (!opening && !sequencer_.stepEdit.visible.get() &&
        !(draft_trace_seen_ && draftTraceEvent)) {
        return false;
    }

    if (!opening && !sequencer_.stepEdit.visible.get() && draft_trace_seen_) {
        out.mode = draft.exitPromptVisible.get()
            ? "sequencer.step_content_draft.exit"
            : "sequencer.step_content_draft";
        out.target = stepContentDraftKindName(
            draftActive
                ? draft.kind.get()
                : static_cast<seq::SequencerStepContentDraftKind>(
                      draft_trace_kind_
                  )
        );
        out.targetStep = static_cast<int16_t>(
            draftActive ? draft.ownerStep : sequencer_.focusedStep.get()
        );
        fillDraftFacts(out);

        if (draft.exitPromptVisible.get() &&
            isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_draft_exit_choice";
            out.projection = "preview";
            out.outcome = "preview";
        } else if (draft.exitPromptVisible.get() && navRelease) {
            out.effect = "resolve_draft_exit";
        } else if (leftTopRelease) {
            out.effect = draftActive && draft_trace_dirty_
                ? "request_draft_exit"
                : "discard_step_content_draft";
        } else if (bottomRightRelease) {
            out.effect = "apply_step_content_draft";
        } else if (navRelease) {
            out.effect = draftActive
                ? "edit_step_content_draft"
                : "resolve_draft_exit";
        } else {
            return false;
        }

        if (!draftActive) {
            out.outcome = out.action != nullptr &&
                              (std::strcmp(out.action, "apply") == 0 ||
                               std::strcmp(out.action, "save") == 0)
                ? "applied"
                : "discarded";
            out.projection = "published";
        } else if (draft.failure !=
                   seq::SequencerStepContentDraftFailure::NONE) {
            out.outcome = "failed";
            out.reason = out.draftFailure;
            out.projection = "draft";
        } else if (!out.projection) {
            out.projection = "draft";
        }
        return true;
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
        out.targetCount = static_cast<int16_t>(
            core::state::sequencer::activeContentLength(sequencer_)
        );
        out.targetPage = static_cast<int16_t>(
            core::state::sequencer::activeContentPageForStep(step)
        );
        out.effect = actionName(policy.macroLongPress);
        fillResolvedStepUxContext(
            sequencer_,
            tracks_,
            step,
            sequencer_.activeStepProperty.get(),
            out
        );
        copyIndexLabel(out.valueLabel, step);
        fillDraftFacts(out);
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
    out.targetCount = static_cast<int16_t>(
        core::state::sequencer::activeContentLength(sequencer_)
    );
    out.targetPage = static_cast<int16_t>(
        core::state::sequencer::activeContentPageForStep(
            static_cast<uint8_t>(data.stepIndex)
        )
    );
    fillDraftFacts(out);
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

        const uint8_t selectedRow = static_cast<uint8_t>(data.selectedIndex);
        const bool stepContentRow =
            core::state::sequencer::step_edit_rows::isChord(selectedRow) ||
            core::state::sequencer::step_edit_rows::isContext(selectedRow);
        if (stepContentRow && !draftActive) {
            // "Edit" is the presenter contract for content that already lives
            // in the published graph; every other value is absent or blocked.
            out.hasPublished = true;
            out.published = data.rows[data.selectedIndex].value != nullptr &&
                std::strcmp(data.rows[data.selectedIndex].value, "Edit") == 0;
        }
    }

    uint8_t closeIndex = 0;
    const bool macroClose =
        isMacroButtonRelease(event, closeIndex) &&
        closeIndex == static_cast<uint8_t>(data.stepIndex % core::state::sequencer::SequencerState::STEPS_PER_PAGE);
    const bool leftCenterPress = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::PRESS
    );
    const bool leftCenterRelease = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::RELEASE
    );
    if (leftCenterPress &&
        core::state::sequencer::isRootContentView(sequencer_) &&
        !sequencer_.stepEdit.chordEditor.active.get()) {
        step_retarget_seen_ = true;
        out.effect = "arm_step_retarget";
        out.projection = "preview";
    } else if (draft.exitPromptVisible.get() &&
               isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_draft_exit_choice";
        out.projection = "preview";
        out.outcome = "preview";
    } else if (draft.exitPromptVisible.get() && navRelease) {
        out.effect = "resolve_draft_exit";
    } else if (draft_trace_seen_ && bottomRightRelease) {
        out.effect = "apply_step_content_draft";
        out.projection = draftActive ? "draft" : "published";
        if (!draftActive) out.outcome = "applied";
        if (draftActive && draft.failure !=
            seq::SequencerStepContentDraftFailure::NONE) {
            out.outcome = "failed";
            out.reason = out.draftFailure;
        }
    } else if (draft_trace_seen_ && leftTopRelease) {
        out.effect = draftActive && draft_trace_dirty_
            ? "request_draft_exit"
            : "discard_step_content_draft";
        out.projection = draftActive ? "draft" : "published";
        if (!draftActive) out.outcome = "discarded";
    } else if (draft_trace_seen_ && navRelease && !draftActive) {
        out.effect = "resolve_draft_exit";
        out.projection = "published";
        out.outcome = out.action != nullptr &&
                          std::strcmp(out.action, "save") == 0
            ? "applied"
            : "discarded";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = step_retarget_seen_
            ? "retarget_step"
            : actionName(policy.navTurn);
        if (step_retarget_seen_) {
            out.projection = "applied";
            out.outcome = "applied";
        }
    } else if (leftCenterRelease && step_retarget_seen_) {
        step_retarget_seen_ = false;
        out.effect = "finish_step_retarget";
        out.projection = "applied";
        out.outcome = "applied";
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
        if (policy.bottomRightTap ==
            SequencerAction::COPY_STEP_EDITOR_CONTEXT) {
            out.action = "copy";
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        if (policy.bottomRightHold == SequencerAction::NONE) return false;
        out.effect = actionName(policy.bottomRightHold);
    }
    return true;
}

}  // namespace core::context::standalone::ux

#endif

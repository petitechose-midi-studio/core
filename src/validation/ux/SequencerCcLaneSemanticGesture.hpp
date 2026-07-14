#pragma once

#include "state/contextual/ContextActionSpec.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"

namespace core::validation::ux {

enum class SequencerCcLaneGesturePhase : uint8_t {
    PRESS = 0,
    RELEASE,
};

struct SequencerCcLaneSemanticGesture {
    const char* effect = nullptr;
    const char* outcome = nullptr;
    core::state::contextual::ContextActionReason reason =
        core::state::contextual::ContextActionReason::NONE;
};

namespace sequencer_cc_lane_semantic_detail {

using ActionId = core::state::contextual::ContextActionId;

constexpr bool isCcLaneAction(ActionId action) {
    return action == ActionId::CREATE || action == ActionId::APPLY ||
           action == ActionId::REMOVE || action == ActionId::CLEAR ||
           action == ActionId::OPEN_SETTINGS;
}

constexpr const char* appliedEffect(ActionId action) {
    switch (action) {
        case ActionId::CREATE: return "create_cc_lane";
        case ActionId::APPLY: return "apply_cc_lane_settings";
        case ActionId::REMOVE: return "remove_cc_lane";
        case ActionId::CLEAR: return "clear_cc_event";
        case ActionId::OPEN_SETTINGS: return "open_cc_lane_settings";
        default: return "cc_lane_action";
    }
}

constexpr const char* pressedEffect(ActionId action) {
    switch (action) {
        case ActionId::CREATE: return "press_create_cc_lane";
        case ActionId::APPLY: return "press_apply_cc_lane_settings";
        case ActionId::REMOVE: return "press_remove_cc_lane";
        case ActionId::CLEAR: return "press_clear_cc_event";
        case ActionId::OPEN_SETTINGS: return "press_open_cc_lane_settings";
        default: return "press_cc_lane_action";
    }
}

constexpr const char* armedEffect(ActionId action) {
    switch (action) {
        case ActionId::CREATE: return "arm_create_cc_lane";
        case ActionId::APPLY: return "arm_apply_cc_lane_settings";
        case ActionId::REMOVE: return "arm_remove_cc_lane";
        case ActionId::CLEAR: return "arm_clear_cc_event";
        case ActionId::OPEN_SETTINGS: return "arm_open_cc_lane_settings";
        default: return "arm_cc_lane_action";
    }
}

constexpr const char* cancelledEffect(ActionId action) {
    switch (action) {
        case ActionId::CREATE: return "cancel_create_cc_lane";
        case ActionId::APPLY: return "cancel_apply_cc_lane_settings";
        case ActionId::REMOVE: return "cancel_remove_cc_lane";
        case ActionId::CLEAR: return "cancel_clear_cc_event";
        case ActionId::OPEN_SETTINGS: return "cancel_open_cc_lane_settings";
        default: return "cancel_cc_lane_action";
    }
}

constexpr const char* requestEffect(ActionId action) {
    switch (action) {
        case ActionId::CREATE: return "request_create_cc_lane";
        case ActionId::APPLY: return "request_apply_cc_lane_settings";
        case ActionId::REMOVE: return "request_remove_cc_lane";
        case ActionId::CLEAR: return "request_clear_cc_event";
        case ActionId::OPEN_SETTINGS: return "request_open_cc_lane_settings";
        default: return "request_cc_lane_action";
    }
}

}  // namespace sequencer_cc_lane_semantic_detail

/**
 * Classifies one bottom-strip gesture without consulting rendered widgets.
 *
 * A release is allowed to describe a mutation only when it is an executable
 * tap or when its guarded hold already reached COMMITTED.  This deliberately
 * keeps early releases and incomplete holds out of semantic UX evidence.
 */
constexpr SequencerCcLaneSemanticGesture classifySequencerCcLaneGesture(
    const core::state::contextual::ContextActionSpec& spec,
    const core::state::contextual::GuardedActionState& guard,
    const core::state::contextual::OperationFeedbackState& feedback,
    SequencerCcLaneGesturePhase phase
) {
    namespace contextual = core::state::contextual;
    using namespace sequencer_cc_lane_semantic_detail;

    const bool guarded = contextual::requiresGuard(spec) &&
        contextual::hasHoldAction(spec);
    const ActionId action = guarded ? spec.hold.action : spec.tap.action;
    const auto& variant = guarded ? spec.hold : spec.tap;

    if (phase == SequencerCcLaneGesturePhase::PRESS) {
        // Capture records intentionally reuse the originating physical event
        // while projecting the current UI state. If the held gesture has
        // progressed since that press, report the current guarded phase rather
        // than freezing every later capture at `pressed`.
        if (guarded) {
            if (guard.phase == contextual::GuardedActionPhase::ARMED) {
                return {armedEffect(action), "armed", variant.reason};
            }
            if (guard.phase == contextual::GuardedActionPhase::COMMITTED) {
                return {appliedEffect(action), "applied", variant.reason};
            }
            if (guard.phase == contextual::GuardedActionPhase::CANCELLED) {
                return {cancelledEffect(action), "cancelled", variant.reason};
            }
        }
        if (!isCcLaneAction(action)) {
            return {"press_cc_lane_action", "noop",
                    contextual::ContextActionReason::NO_ACTION};
        }
        if (!contextual::canExecute(variant)) {
            return {requestEffect(action), "blocked", variant.reason};
        }
        return {pressedEffect(action), "pressed", variant.reason};
    }

    // Post-dispatch capture sees terminal feedback after the guard has been
    // reset.  Prefer that exact result when it belongs to a CC-lane action.
    const bool terminalFeedbackMatches = feedback.action == action ||
        (!contextual::hasTapAction(spec) && !contextual::hasHoldAction(spec));
    if (guard.phase == contextual::GuardedActionPhase::IDLE &&
        feedback.active && terminalFeedbackMatches &&
        isCcLaneAction(feedback.action)) {
        switch (feedback.status) {
            case contextual::OperationFeedbackStatus::APPLIED:
                return {appliedEffect(feedback.action), "applied", feedback.reason};
            case contextual::OperationFeedbackStatus::CANCELLED:
                return {cancelledEffect(feedback.action), "cancelled", feedback.reason};
            case contextual::OperationFeedbackStatus::BLOCKED:
                return {requestEffect(feedback.action), "blocked", feedback.reason};
            case contextual::OperationFeedbackStatus::FAILED:
                return {requestEffect(feedback.action), "failed", feedback.reason};
            default:
                break;
        }
    }

    if (guarded) {
        switch (guard.phase) {
            case contextual::GuardedActionPhase::PRESSED:
                return {cancelledEffect(action), "cancelled",
                        contextual::ContextActionReason::NO_ACTION};
            case contextual::GuardedActionPhase::ARMED:
                return {armedEffect(action), "armed", variant.reason};
            case contextual::GuardedActionPhase::COMMITTED:
                return {appliedEffect(action), "applied", variant.reason};
            case contextual::GuardedActionPhase::CANCELLED:
                return {cancelledEffect(action), "cancelled", variant.reason};
            case contextual::GuardedActionPhase::IDLE:
                // A guarded action with no active guard cannot mutate.
                return {cancelledEffect(action), "cancelled",
                        contextual::ContextActionReason::NO_ACTION};
        }
    }

    if (!contextual::hasTapAction(spec)) {
        return {"cc_lane_action", "noop",
                contextual::ContextActionReason::NO_ACTION};
    }
    if (!contextual::canExecute(spec.tap)) {
        return {requestEffect(spec.tap.action), "blocked", spec.tap.reason};
    }
    return {appliedEffect(spec.tap.action), "applied", spec.tap.reason};
}

constexpr const char* sequencerCcLaneSemanticReasonName(
    core::state::contextual::ContextActionReason reason
) {
    using Reason = core::state::contextual::ContextActionReason;
    switch (reason) {
        case Reason::NONE: return nullptr;
        case Reason::NO_ACTION: return "hold_incomplete";
        case Reason::EMPTY_SELECTION: return "empty_selection";
        case Reason::MINIMUM_CARDINALITY: return "minimum_cardinality";
        case Reason::NO_ROUTE: return "no_route";
        case Reason::CONFLICT: return "conflict";
        case Reason::PENDING: return "pending";
        case Reason::ALLOCATION_UNAVAILABLE: return "allocation_unavailable";
        case Reason::TRANSPORT_STATE: return "transport_state";
        case Reason::FAILED: return "failed";
        default: return "blocked";
    }
}

}  // namespace core::validation::ux

#pragma once

#include "state/contextual/ContextActionSpec.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::context::standalone::cc_lane_overlay_visuals {

inline const core::state::contextual::ContextActionVariant& visibleVariant(
    const core::state::contextual::ContextActionSpec& spec,
    const core::state::contextual::GuardedActionState& guard,
    const core::state::contextual::OperationFeedbackState& feedback
) {
    namespace contextual = core::state::contextual;
    const bool holdFeedback = contextual::hasHoldAction(spec) &&
        feedback.active && feedback.action == spec.hold.action;
    const bool holdGesture = holdFeedback &&
        (guard.phase == contextual::GuardedActionPhase::PRESSED ||
         guard.phase == contextual::GuardedActionPhase::ARMED);
    if (holdFeedback || holdGesture) return spec.hold;
    return contextual::hasAction(spec.tap) ? spec.tap : spec.hold;
}

inline core::ui::ContextActionStripVisualState stripVisual(
    const core::state::contextual::ContextActionVariant& action,
    const core::state::contextual::GuardedActionState& guard,
    const core::state::contextual::OperationFeedbackState& feedback
) {
    namespace contextual = core::state::contextual;
    using Availability = contextual::ContextActionAvailability;
    using Phase = contextual::GuardedActionPhase;
    using Status = contextual::OperationFeedbackStatus;
    using Visual = core::ui::ContextActionStripVisualState;

    if (!contextual::hasAction(action)) return Visual::HIDDEN;
    if (action.availability == Availability::DISABLED) return Visual::DISABLED;
    const bool feedbackMatches = feedback.active &&
        feedback.action == action.action;
    if (feedbackMatches) {
        if (feedback.status == Status::APPLIED) return Visual::APPLIED;
        if (feedback.status == Status::CANCELLED) return Visual::CANCELLED;
        if (guard.phase == Phase::PRESSED) return Visual::PRESSED;
        if (guard.phase == Phase::ARMED) return Visual::ARMED;
        if (guard.phase == Phase::COMMITTED) return Visual::APPLIED;
        if (guard.phase == Phase::CANCELLED) return Visual::CANCELLED;
    }
    return Visual::ACTIVE;
}

inline core::state::contextual::ContextActionVisualPolicy projectedVisualPolicy(
    const core::state::contextual::ContextActionVariant& action,
    const core::state::contextual::OperationFeedbackState& feedback
) {
    namespace contextual = core::state::contextual;
    using Icon = contextual::ContextIconId;
    using Status = contextual::OperationFeedbackStatus;
    using Tone = contextual::ContextTone;

    if (!feedback.active || feedback.action != action.action) {
        return action.visual;
    }
    switch (feedback.status) {
        case Status::FAILED:
        case Status::BLOCKED:
            return {.icon = Icon::ERROR, .tone = Tone::RED};
        case Status::WARNING:
            return {.icon = Icon::WARNING, .tone = Tone::AMBER};
        case Status::CONFLICT:
            return {.icon = Icon::CONFLICT, .tone = Tone::RED};
        case Status::QUEUED:
            return {.icon = Icon::QUEUED, .tone = Tone::AMBER};
        case Status::APPLIED:
            return {.icon = Icon::APPLIED, .tone = Tone::GREEN};
        default:
            return action.visual;
    }
}

}  // namespace core::context::standalone::cc_lane_overlay_visuals

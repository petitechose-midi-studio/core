#pragma once

#include "state/contextual/ContextActionSpec.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::context_action_visual_projection {

inline const core::state::contextual::ContextActionVariant& visibleVariant(
    const core::state::contextual::ContextActionSpec& spec,
    const core::state::contextual::GuardedActionState& guard,
    const core::state::contextual::OperationFeedbackState& feedback
) {
    namespace contextual = core::state::contextual;
    const bool holdFeedback = contextual::hasHoldAction(spec) &&
        feedback.active && feedback.action == spec.hold.action;
    const bool holdGesture = contextual::hasHoldAction(spec) &&
        (!feedback.active || feedback.action == spec.hold.action) &&
        (guard.phase == contextual::GuardedActionPhase::PRESSED ||
         guard.phase == contextual::GuardedActionPhase::ARMED ||
         guard.phase == contextual::GuardedActionPhase::COMMITTED);
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
        switch (feedback.status) {
            case Status::QUEUED: return Visual::ARMED;
            case Status::APPLIED: return Visual::APPLIED;
            case Status::CANCELLED: return Visual::CANCELLED;
            case Status::BLOCKED:
            case Status::CONFLICT:
            case Status::FAILED: return Visual::CANCELLED;
            case Status::PRESSED:
            case Status::ARMED:
            case Status::WARNING:
            case Status::PREVIEW:
            case Status::NONE: break;
        }
    }
    // GuardedActionState intentionally carries no action id. Active feedback
    // is therefore the authority that scopes a shared guard to its action.
    if (feedback.active && !feedbackMatches) return Visual::ACTIVE;
    if (guard.phase == Phase::PRESSED) return Visual::PRESSED;
    if (guard.phase == Phase::ARMED ||
        guard.phase == Phase::COMMITTED) {
        return Visual::ARMED;
    }
    if (guard.phase == Phase::CANCELLED) return Visual::CANCELLED;
    if (feedbackMatches) {
        if (feedback.status == Status::PRESSED) return Visual::PRESSED;
        if (feedback.status == Status::ARMED) return Visual::ARMED;
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

inline core::ui::ContextActionStripTone stripTone(
    core::state::contextual::ContextTone tone
) {
    using Tone = core::state::contextual::ContextTone;
    using StripTone = core::ui::ContextActionStripTone;
    switch (tone) {
        case Tone::GREEN: return StripTone::POSITIVE;
        case Tone::BLUE: return StripTone::CONSTRUCTIVE;
        case Tone::AMBER: return StripTone::WARNING;
        case Tone::RED: return StripTone::DESTRUCTIVE;
        case Tone::DEFAULT:
        case Tone::NEUTRAL:
        default: return StripTone::NEUTRAL;
    }
}

inline const char* iconGlyph(
    core::state::contextual::ContextIconId icon
) {
    using Icon = core::state::contextual::ContextIconId;
    switch (icon) {
        case Icon::CREATE:
        case Icon::APPLY:
            return ::standalone::icons::ACTION_APPLY;
        case Icon::ENTER:
            return ::standalone::icons::ACTION_VALIDATE;
        case Icon::EDIT:
            return ::standalone::icons::SETTINGS_GEAR;
        case Icon::SAVE:
            return ::standalone::icons::STORAGE;
        case Icon::LOAD:
            return ::standalone::icons::ACTION_APPLY;
        case Icon::CLEAR:
            return ::standalone::icons::ACTION_CLEAR;
        case Icon::REMOVE:
            return ::standalone::icons::ACTION_REMOVE;
        case Icon::ROUTE_INHERITED:
            return ::standalone::icons::ROUTING;
        case Icon::ROUTE_PINNED:
            return ::standalone::icons::ROUTE_PIN;
        case Icon::CONFLICT:
            return ::standalone::icons::STATUS_CONFLICT;
        case Icon::ERROR:
        case Icon::NO_ROUTE:
            return ::standalone::icons::STATUS_ERROR;
        case Icon::QUEUED:
            return ::standalone::icons::STATUS_QUEUED;
        case Icon::APPLIED:
            return ::standalone::icons::ACTION_VALIDATE;
        case Icon::WARNING:
            return ::standalone::icons::STATUS_WARNING;
        case Icon::PREVIEW:
            return ::standalone::icons::STATUS_PREVIEW;
        case Icon::HOLD:
            return ::standalone::icons::ACTION_OVERWRITE;
        default:
            return ::standalone::icons::MIDI_CC;
    }
}

inline const char* feedbackIconGlyph(
    const core::state::contextual::OperationFeedbackState& feedback,
    core::state::contextual::ContextIconId projectedIcon
) {
    using Status =
        core::state::contextual::OperationFeedbackStatus;
    switch (feedback.status) {
        case Status::PRESSED:
        case Status::ARMED:
            return ::standalone::icons::ACTION_OVERWRITE;
        case Status::CANCELLED:
            return ::standalone::icons::ACTION_CANCEL;
        case Status::PREVIEW:
            return ::standalone::icons::STATUS_PREVIEW;
        default:
            return iconGlyph(projectedIcon);
    }
}

}  // namespace core::ui::context_action_visual_projection

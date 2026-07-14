#include "handler/macro/MacroGuardedActionWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

namespace core::handler::macro {

namespace contextual = core::state::contextual;

FLASHMEM bool MacroGuardedActionWorkflow::begin(
    core::state::MacroEditState& state,
    core::state::MacroContextButton button,
    contextual::ContextActionId holdAction,
    contextual::ContextEntityRef source,
    contextual::ContextEntityRef target,
    uint32_t nowMs,
    uint16_t durationMs
) {
    auto guard = state.contextGuard.get();
    if (contextual::guardedActionTerminal(guard)) {
        contextual::resetGuardedAction(guard);
    }
    if (!contextual::beginGuardedActionPress(guard, nowMs, durationMs)) {
        return false;
    }
    state.contextGuard.set(guard);
    state.contextButton.set(button);
    if (holdAction == contextual::ContextActionId::NONE) {
        state.contextFeedback.set({});
        return true;
    }

    auto feedback = state.contextFeedback.get();
    contextual::setOperationFeedback(
        feedback,
        holdAction,
        source,
        target,
        contextual::OperationFeedbackStatus::PRESSED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        nowMs
    );
    state.contextFeedback.set(feedback);
    return true;
}

FLASHMEM bool MacroGuardedActionWorkflow::update(
    core::state::MacroEditState& state,
    uint32_t nowMs
) {
    auto feedback = state.contextFeedback.get();
    if (contextual::updateOperationFeedback(feedback, nowMs)) {
        state.contextFeedback.set(feedback);
    }

    auto guard = state.contextGuard.get();
    if (!state.contextFeedback.get().active &&
        contextual::guardedActionTerminal(guard)) {
        contextual::resetGuardedAction(guard);
        state.contextGuard.set(guard);
        state.contextButton.set(core::state::MacroContextButton::NONE);
        return false;
    }

    if (guard.phase == contextual::GuardedActionPhase::PRESSED) {
        const uint32_t elapsed = nowMs - guard.pressedAtMs;
        if (elapsed < Config::Timing::LATCH_THRESHOLD_MS) return false;
        if (!state.contextFeedback.get().active) {
            contextual::cancelGuardedAction(guard);
            state.contextGuard.set(guard);
            return false;
        }
        const uint32_t pressedAtMs = guard.pressedAtMs;
        if (!contextual::armGuardedAction(guard, pressedAtMs)) return false;
        feedback = state.contextFeedback.get();
        contextual::setOperationFeedback(
            feedback,
            feedback.action,
            feedback.source,
            feedback.target,
            contextual::OperationFeedbackStatus::ARMED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::MANUAL,
            nowMs
        );
        state.contextFeedback.set(feedback);
    }

    if (guard.phase == contextual::GuardedActionPhase::ARMED) {
        (void)contextual::updateGuardedAction(guard, nowMs);
    }
    state.contextGuard.set(guard);
    return guard.phase == contextual::GuardedActionPhase::COMMITTED;
}

FLASHMEM contextual::GuardedActionRelease MacroGuardedActionWorkflow::release(
    core::state::MacroEditState& state,
    core::state::MacroContextButton button,
    uint32_t nowMs
) {
    if (state.contextButton.get() != button) {
        return contextual::GuardedActionRelease::NONE;
    }

    auto guard = state.contextGuard.get();
    if (guard.phase == contextual::GuardedActionPhase::PRESSED &&
        (nowMs - guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        if (state.contextFeedback.get().active) {
            const uint32_t pressedAtMs = guard.pressedAtMs;
            (void)contextual::armGuardedAction(guard, pressedAtMs);
            (void)contextual::updateGuardedAction(guard, nowMs);
        } else {
            (void)contextual::cancelGuardedAction(guard);
        }
    }

    const auto result = contextual::releaseGuardedAction(guard, nowMs);
    state.contextGuard.set(guard);
    if (result == contextual::GuardedActionRelease::TAP) {
        state.contextFeedback.set({});
        state.contextButton.set(core::state::MacroContextButton::NONE);
    } else if (result == contextual::GuardedActionRelease::CANCELLED) {
        auto feedback = state.contextFeedback.get();
        contextual::setOperationFeedback(
            feedback,
            feedback.action,
            feedback.source,
            feedback.target,
            contextual::OperationFeedbackStatus::CANCELLED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
        );
        state.contextFeedback.set(feedback);
    }
    return result;
}

FLASHMEM void MacroGuardedActionWorkflow::complete(
    core::state::MacroEditState& state,
    bool applied,
    uint32_t nowMs
) {
    auto feedback = state.contextFeedback.get();
    contextual::setOperationFeedback(
        feedback,
        feedback.action,
        feedback.source,
        feedback.target,
        applied ? contextual::OperationFeedbackStatus::APPLIED
                : contextual::OperationFeedbackStatus::FAILED,
        applied ? contextual::ContextActionReason::NONE
                : contextual::ContextActionReason::FAILED,
        applied ? contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION
                : contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
        nowMs,
        applied ? Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS : 0U
    );
    state.contextFeedback.set(feedback);
    state.contextGuard.set({});
}

FLASHMEM void MacroGuardedActionWorkflow::cancel(
    core::state::MacroEditState& state,
    uint32_t nowMs
) {
    auto guard = state.contextGuard.get();
    const bool cancelledActive = contextual::cancelGuardedAction(guard);
    if (!cancelledActive &&
        guard.phase == contextual::GuardedActionPhase::COMMITTED) {
        // COMMITTED only means that visible progress reached 100%. The Macro
        // mutation is deliberately deferred until physical release so the
        // owning overlay cannot close and reroute that release to Structure.
        guard.phase = contextual::GuardedActionPhase::CANCELLED;
    } else if (!cancelledActive) {
        return;
    }
    state.contextGuard.set(guard);
    auto feedback = state.contextFeedback.get();
    contextual::setOperationFeedback(
        feedback,
        feedback.action,
        feedback.source,
        feedback.target,
        contextual::OperationFeedbackStatus::CANCELLED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
    );
    state.contextFeedback.set(feedback);
}

}  // namespace core::handler::macro

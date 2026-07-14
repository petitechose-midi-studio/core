#include "state/contextual/OperationFeedbackState.hpp"

namespace core::state::contextual {

void clearOperationFeedback(OperationFeedbackState& state) {
    state = OperationFeedbackState{};
}

void setOperationFeedback(
    OperationFeedbackState& state,
    ContextActionId action,
    ContextEntityRef source,
    ContextEntityRef target,
    OperationFeedbackStatus status,
    ContextActionReason reason,
    OperationFeedbackExpiryPolicy expiryPolicy,
    uint32_t nowMs,
    uint32_t durationMs
) {
    if (status == OperationFeedbackStatus::NONE) {
        clearOperationFeedback(state);
        return;
    }

    state.active = true;
    state.action = action;
    state.source = source;
    state.target = target;
    state.status = status;
    state.reason = reason;
    state.expiryPolicy = expiryPolicy;
    state.shownAtMs = nowMs;
    state.durationMs = durationMs;
}

bool updateOperationFeedback(OperationFeedbackState& state, uint32_t nowMs) {
    if (!state.active ||
        state.expiryPolicy != OperationFeedbackExpiryPolicy::AFTER_DURATION) {
        return false;
    }

    if ((nowMs - state.shownAtMs) < state.durationMs) {
        return false;
    }

    clearOperationFeedback(state);
    return true;
}

bool dismissOperationFeedbackOnMeaningfulInput(OperationFeedbackState& state) {
    if (!state.active || state.expiryPolicy !=
        OperationFeedbackExpiryPolicy::ON_NEXT_MEANINGFUL_INPUT) {
        return false;
    }
    clearOperationFeedback(state);
    return true;
}

bool resolveOperationFeedback(OperationFeedbackState& state) {
    if (!state.active ||
        state.expiryPolicy != OperationFeedbackExpiryPolicy::WHEN_RESOLVED) {
        return false;
    }
    clearOperationFeedback(state);
    return true;
}

bool acknowledgeOperationFeedback(OperationFeedbackState& state) {
    if (!state.active || state.expiryPolicy !=
        OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT) {
        return false;
    }
    clearOperationFeedback(state);
    return true;
}

}  // namespace core::state::contextual

#include "state/contextual/OperationFeedbackState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::contextual {

FLASHMEM bool operator==(
    const OperationFeedbackState& lhs,
    const OperationFeedbackState& rhs
) {
    return lhs.active == rhs.active && lhs.action == rhs.action &&
           lhs.source == rhs.source && lhs.target == rhs.target &&
           lhs.status == rhs.status && lhs.reason == rhs.reason &&
           lhs.expiryPolicy == rhs.expiryPolicy &&
           lhs.shownAtMs == rhs.shownAtMs &&
           lhs.durationMs == rhs.durationMs;
}

FLASHMEM bool operator!=(
    const OperationFeedbackState& lhs,
    const OperationFeedbackState& rhs
) {
    return !(lhs == rhs);
}

FLASHMEM void clearOperationFeedback(OperationFeedbackState& state) {
    state = OperationFeedbackState{};
}

FLASHMEM void setOperationFeedback(
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

FLASHMEM bool updateOperationFeedback(OperationFeedbackState& state, uint32_t nowMs) {
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

FLASHMEM bool dismissOperationFeedbackOnMeaningfulInput(OperationFeedbackState& state) {
    if (!state.active || state.expiryPolicy !=
        OperationFeedbackExpiryPolicy::ON_NEXT_MEANINGFUL_INPUT) {
        return false;
    }
    clearOperationFeedback(state);
    return true;
}

FLASHMEM bool resolveOperationFeedback(OperationFeedbackState& state) {
    if (!state.active ||
        state.expiryPolicy != OperationFeedbackExpiryPolicy::WHEN_RESOLVED) {
        return false;
    }
    clearOperationFeedback(state);
    return true;
}

FLASHMEM bool acknowledgeOperationFeedback(OperationFeedbackState& state) {
    if (!state.active || state.expiryPolicy !=
        OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT) {
        return false;
    }
    clearOperationFeedback(state);
    return true;
}

}  // namespace core::state::contextual

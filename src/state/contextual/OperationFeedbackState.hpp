#pragma once

#include <cstdint>

#include "state/contextual/ContextActionSpec.hpp"

namespace core::state::contextual {

enum class OperationFeedbackStatus : uint8_t {
    NONE = 0,
    PREVIEW,
    PRESSED,
    ARMED,
    QUEUED,
    APPLIED,
    CANCELLED,
    BLOCKED,
    WARNING,
    CONFLICT,
    FAILED,
};

enum class OperationFeedbackExpiryPolicy : uint8_t {
    MANUAL = 0,
    AFTER_DURATION,
    ON_NEXT_MEANINGFUL_INPUT,
    WHEN_RESOLVED,
    ON_ACKNOWLEDGEMENT,
};

struct OperationFeedbackState {
    bool active = false;
    ContextActionId action = ContextActionId::NONE;
    ContextEntityRef source{};
    ContextEntityRef target{};
    OperationFeedbackStatus status = OperationFeedbackStatus::NONE;
    ContextActionReason reason = ContextActionReason::NONE;
    OperationFeedbackExpiryPolicy expiryPolicy =
        OperationFeedbackExpiryPolicy::MANUAL;
    uint32_t shownAtMs = 0;
    uint32_t durationMs = 0;
};

void clearOperationFeedback(OperationFeedbackState& state);

void setOperationFeedback(
    OperationFeedbackState& state,
    ContextActionId action,
    ContextEntityRef source,
    ContextEntityRef target,
    OperationFeedbackStatus status,
    ContextActionReason reason,
    OperationFeedbackExpiryPolicy expiryPolicy,
    uint32_t nowMs,
    uint32_t durationMs = 0
);

/** Expires only AFTER_DURATION feedback and reports whether it was cleared. */
bool updateOperationFeedback(OperationFeedbackState& state, uint32_t nowMs);

bool dismissOperationFeedbackOnMeaningfulInput(OperationFeedbackState& state);
bool resolveOperationFeedback(OperationFeedbackState& state);
bool acknowledgeOperationFeedback(OperationFeedbackState& state);

}  // namespace core::state::contextual

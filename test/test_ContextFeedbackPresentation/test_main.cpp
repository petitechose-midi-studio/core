#include <cassert>
#include <cstring>
#include <iostream>

#include "ui/strip/ContextFeedbackPresentation.hpp"

namespace contextual = core::state::contextual;
using Action = contextual::ContextActionId;
using Status = contextual::OperationFeedbackStatus;
using Reason = contextual::ContextActionReason;

int main() {
    contextual::OperationFeedbackState feedback{};
    contextual::setOperationFeedback(feedback, Action::PASTE, {}, {}, Status::BLOCKED,
        Reason::EMPTY_CLIPBOARD, contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
        100U);
    const auto original = feedback;
    assert(std::strcmp(core::ui::contextActionFeedbackLabel(feedback), "Blocked") == 0);
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Clipboard empty") == 0);
    assert(feedback == original); // Presentation cannot acknowledge a failed operation.

    feedback.reason = Reason::HISTORY_UNAVAILABLE;
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "History unavailable") == 0);
    feedback.status = Status::PRESSED;
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Holding") == 0);
    assert(core::ui::contextActionFeedbackLabel(feedback) == nullptr);
    feedback.status = Status::ARMED;
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Armed") == 0);
    feedback.status = Status::CANCELLED;
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Cancelled") == 0);
    feedback.active = false;
    assert(core::ui::contextActionFeedbackText(feedback)[0] == '\0');
    assert(core::ui::contextActionFeedbackLabel(feedback) == nullptr);

    contextual::setOperationFeedback(feedback, Action::LOAD, {}, {}, Status::QUEUED,
        Reason::PENDING, contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED, 100U);
    assert(!contextual::updateOperationFeedback(feedback, 500000U));
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Operation pending") == 0);
    assert(contextual::resolveOperationFeedback(feedback));
    assert(core::ui::contextActionFeedbackText(feedback)[0] == '\0');

    contextual::setOperationFeedback(feedback, Action::COPY, {}, {}, Status::APPLIED,
        Reason::NONE, contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, 100U, 300U);
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Copied") == 0);
    feedback.action = Action::LOAD;
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Loaded") == 0);
    feedback.reason = Reason::ADAPTED;
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Adapted to target") == 0);
    assert(contextual::updateOperationFeedback(feedback, 400U));
    assert(core::ui::contextActionFeedbackText(feedback)[0] == '\0');

    // An absent cause must never turn into a guessed storage or memory failure.
    feedback = {.active = true, .action = Action::SAVE, .status = Status::FAILED};
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Failed") == 0);
    feedback.reason = static_cast<Reason>(255U);
    assert(std::strcmp(core::ui::contextActionFeedbackText(feedback), "Failed") == 0);
    feedback.status = Status::NONE;
    assert(core::ui::contextActionFeedbackText(feedback)[0] == '\0');
    std::cout << "Context feedback presentation preserves causes and owner lifetime\n";
}

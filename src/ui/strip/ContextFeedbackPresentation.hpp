#pragma once

#include "state/contextual/OperationFeedbackState.hpp"

namespace core::ui {

/**
 * Shared words for operation outcomes, independent of LVGL. The owner supplies
 * the real reason and lifetime; these functions neither infer admission nor
 * expire feedback. Recovery gestures belong to the active domain policy.
 */
inline const char* contextActionReasonLabel(core::state::contextual::ContextActionReason reason) {
    using Reason = core::state::contextual::ContextActionReason;
    switch (reason) {
        case Reason::EMPTY_SELECTION: return "Nothing selected";
        case Reason::MINIMUM_CARDINALITY: return "Minimum item count reached";
        case Reason::EMPTY_CLIPBOARD: return "Clipboard empty";
        case Reason::WRONG_PAYLOAD: return "Different content type";
        case Reason::INVALID_PAYLOAD: return "Invalid content";
        case Reason::ADAPTED: return "Adapted to target";
        case Reason::CORRUPT_ASSET: return "Unreadable content";
        case Reason::UNSUPPORTED_VERSION: return "Unsupported version";
        case Reason::STALE_TARGET: return "Target changed";
        case Reason::SAME_SOURCE_TARGET: return "Source is the destination";
        case Reason::OUT_OF_RANGE: return "Outside target range";
        case Reason::CAPACITY: return "Not enough room";
        case Reason::PENDING: return "Operation pending";
        case Reason::NO_ROUTE: return "No MIDI route";
        case Reason::INCOMPATIBLE: return "Incompatible target";
        case Reason::HISTORY_UNAVAILABLE: return "History unavailable";
        case Reason::STORAGE_UNAVAILABLE: return "Storage unavailable";
        case Reason::ALLOCATION_UNAVAILABLE: return "Not enough memory";
        case Reason::CONFLICT: return "Conflicting changes";
        case Reason::READ_ONLY: return "Read-only target";
        case Reason::TRANSPORT_STATE: return "Unavailable in this transport state";
        case Reason::FAILED: return "Operation failed";
        case Reason::NONE:
        case Reason::NO_ACTION: return "";
    }
    return "";
}

/** Short result for a command label. Press/hold keep the command's own words. */
inline const char* contextActionFeedbackLabel(
    const core::state::contextual::OperationFeedbackState& feedback
) {
    using Status = core::state::contextual::OperationFeedbackStatus;
    using Action = core::state::contextual::ContextActionId;
    if (!feedback.active) return nullptr;
    switch (feedback.status) {
        case Status::QUEUED: return "Queued";
        case Status::APPLIED:
            switch (feedback.action) {
                case Action::COPY: return "Copied";
                case Action::PASTE: return "Pasted";
                case Action::SAVE: return "Saved";
                case Action::LOAD: return "Loaded";
                case Action::CREATE: return "Created";
                case Action::RENAME: return "Renamed";
                case Action::MOVE: return "Moved";
                case Action::DELETE_ASSET: return "Deleted";
                case Action::REMOVE: return "Removed";
                case Action::CLEAR: return "Cleared";
                case Action::RESET: return "Reset";
                default: return "Applied";
            }
        case Status::CANCELLED: return "Cancelled";
        case Status::BLOCKED: return "Blocked";
        case Status::CONFLICT: return "Conflict";
        case Status::FAILED: return "Failed";
        default: return nullptr;
    }
}

/** Full-width explanation. A recorded cause takes precedence over a status. */
inline const char* contextActionFeedbackText(
    const core::state::contextual::OperationFeedbackState& feedback
) {
    using Status = core::state::contextual::OperationFeedbackStatus;
    if (!feedback.active) return "";
    switch (feedback.status) {
        case Status::NONE: return "";
        case Status::PRESSED: return "Holding";
        case Status::ARMED: return "Armed";
        case Status::CANCELLED: return "Cancelled";
        default: break;
    }
    if (const char* reason = contextActionReasonLabel(feedback.reason); reason[0]) return reason;
    if (const char* label = contextActionFeedbackLabel(feedback)) return label;
    return feedback.status == Status::PREVIEW ? "Preview" : "Warning";
}

}  // namespace core::ui

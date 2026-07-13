#include "state/sequencer/SequencerTrackTransferAction.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace contextual = core::state::contextual;

FLASHMEM contextual::ContextActionReason contextualReasonForTrackTransfer(
    core::state::ClipboardTransferReason reason
) {
    using TransferReason = core::state::ClipboardTransferReason;
    switch (reason) {
        case TransferReason::NONE:
            return contextual::ContextActionReason::NONE;
        case TransferReason::EMPTY_CLIPBOARD:
            return contextual::ContextActionReason::EMPTY_CLIPBOARD;
        case TransferReason::WRONG_PAYLOAD:
            return contextual::ContextActionReason::WRONG_PAYLOAD;
        case TransferReason::INVALID_PAYLOAD:
            return contextual::ContextActionReason::INVALID_PAYLOAD;
        case TransferReason::SAME_TRACK:
            return contextual::ContextActionReason::SAME_SOURCE_TARGET;
        case TransferReason::OUT_OF_RANGE:
            return contextual::ContextActionReason::OUT_OF_RANGE;
        case TransferReason::CAPACITY:
            return contextual::ContextActionReason::CAPACITY;
        case TransferReason::PASTE_PENDING:
            return contextual::ContextActionReason::PENDING;
        case TransferReason::NO_ROUTE:
            return contextual::ContextActionReason::NO_ROUTE;
        case TransferReason::HISTORY_UNAVAILABLE:
            return contextual::ContextActionReason::HISTORY_UNAVAILABLE;
        case TransferReason::ALLOCATION_UNAVAILABLE:
            return contextual::ContextActionReason::ALLOCATION_UNAVAILABLE;
        default:
            return contextual::ContextActionReason::FAILED;
    }
}

FLASHMEM contextual::ContextActionSpec buildSequencerTrackTransferActionSpec(
    const core::state::ClipboardTransferPlan& plan,
    uint8_t focusedTrack,
    bool copyAvailable,
    uint16_t guardDurationMs
) {
    contextual::ContextActionSpec spec{};
    spec.scope = contextual::ContextScope::TRACK;
    spec.source = {
        .kind = contextual::ContextEntityKind::TRACK,
        .track = focusedTrack,
        .item = focusedTrack,
    };
    spec.target = {
        .kind = contextual::ContextEntityKind::TRACK,
        .track = plan.firstTarget,
        .item = plan.targetMask,
    };

    spec.tap.action = contextual::ContextActionId::COPY;
    spec.tap.impact = contextual::ContextActionImpact::NON_MUTATING;
    spec.tap.availability = copyAvailable
        ? contextual::ContextActionAvailability::AVAILABLE
        : contextual::ContextActionAvailability::DISABLED;
    spec.tap.reason = copyAvailable
        ? contextual::ContextActionReason::NONE
        : contextual::ContextActionReason::NO_ACTION;
    spec.tap.visual = {
        contextual::ContextIconId::COPY,
        contextual::ContextTone::NEUTRAL,
    };

    spec.hold.action = contextual::ContextActionId::PASTE;
    spec.hold.impact = plan.overwriteMask != 0
        ? contextual::ContextActionImpact::OVERWRITE
        : contextual::ContextActionImpact::CONSTRUCTIVE;
    switch (plan.availability) {
        case core::state::ClipboardTransferAvailability::READY:
            spec.hold.availability = contextual::ContextActionAvailability::AVAILABLE;
            break;
        case core::state::ClipboardTransferAvailability::WARNING:
            spec.hold.availability = contextual::ContextActionAvailability::WARNING;
            break;
        case core::state::ClipboardTransferAvailability::DISABLED:
        default:
            spec.hold.availability = contextual::ContextActionAvailability::DISABLED;
            break;
    }
    spec.hold.reason = contextualReasonForTrackTransfer(plan.reason);
    spec.hold.visual = {
        contextual::ContextIconId::PASTE,
        plan.overwriteMask != 0 ||
                plan.availability == core::state::ClipboardTransferAvailability::WARNING
            ? contextual::ContextTone::AMBER
            : contextual::ContextTone::GREEN,
    };
    spec.guard = {
        contextual::ContextGuardKind::HOLD,
        guardDurationMs,
    };
    return spec;
}

}  // namespace core::state::sequencer

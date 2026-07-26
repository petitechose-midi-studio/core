#include "ui/sequencer/SequencerTrackPastePreflightViewModel.hpp"

#include <cstdarg>
#include <cstdio>

#include <config/PlatformCompat.hpp>

namespace core::ui::sequencer {
namespace {

namespace contextual = core::state::contextual;
using ActivationStatus = core::state::sequencer::SequencerTrackActivationStatus;
using ActivationOrigin = core::state::sequencer::SequencerTrackActivationOrigin;
using FeedbackStatus = contextual::OperationFeedbackStatus;
using Reason = core::state::ClipboardTransferReason;

template <size_t Size>
FLASHMEM void format(std::array<char, Size>& out, const char* pattern, ...) {
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(out.data(), out.size(), pattern, args);
    va_end(args);
}

FLASHMEM const char* reasonLabel(Reason reason) {
    switch (reason) {
        case Reason::EMPTY_CLIPBOARD: return "Clipboard empty";
        case Reason::WRONG_PAYLOAD: return "Clipboard is not Tracks";
        case Reason::INVALID_PAYLOAD: return "Clipboard invalid";
        case Reason::SAME_TRACK: return "Same Track";
        case Reason::OUT_OF_RANGE: return "Target range unavailable";
        case Reason::CAPACITY: return "Content capacity exceeded";
        case Reason::PASTE_PENDING: return "Track update pending";
        case Reason::NO_ROUTE: return "No route - content stays silent";
        case Reason::HISTORY_UNAVAILABLE: return "Undo history unavailable";
        case Reason::ALLOCATION_UNAVAILABLE: return "Memory unavailable";
        case Reason::NONE:
        default:
            return "Paste unavailable";
    }
}

FLASHMEM void formatSummaryMapping(
    std::array<char, 272>& out,
    const core::state::ClipboardTransferPlan& plan,
    uint8_t fallbackTarget
) {
    if (!plan.hasEntries()) {
        format(
            out,
            "T%u>?",
            static_cast<unsigned>(fallbackTarget + 1U)
        );
        return;
    }
    const auto& entry = plan.entry;
    if (entry.targetRouteValid) {
        format(
            out,
            "T%u>T%u/C%u",
            static_cast<unsigned>(entry.sourceTrack + 1U),
            static_cast<unsigned>(entry.targetTrack + 1U),
            static_cast<unsigned>(entry.targetMidiChannel + 1U)
        );
    } else {
        format(
            out,
            "T%u>T%u/--",
            static_cast<unsigned>(entry.sourceTrack + 1U),
            static_cast<unsigned>(entry.targetTrack + 1U)
        );
    }
}
struct ActivationProjection {
    bool exact = false;
    bool queued = false;
    bool fullyApplied = false;
    bool cancelled = false;
};

FLASHMEM ActivationProjection activationProjection(
    const SequencerTrackPasteProjection& projection,
    const std::array<
        core::state::sequencer::SequencerTrackActivationTelemetry,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>& telemetry
) {
    ActivationProjection out{};
    if (projection.activationGeneration == 0 ||
        !projection.plan.hasEntries()) {
        return out;
    }

    const uint8_t target = projection.plan.entry.targetTrack;
    if (target >= telemetry.size()) return out;
    const auto& entry = telemetry[target];
    if (entry.generation != projection.activationGeneration ||
        entry.origin != ActivationOrigin::TRACK_PASTE) {
        return out;
    }

    out.exact = true;
    out.queued = entry.status == ActivationStatus::QUEUED;
    out.fullyApplied = entry.status == ActivationStatus::APPLIED;
    out.cancelled = entry.status == ActivationStatus::CANCELLED;
    if (!out.queued && !out.fullyApplied && !out.cancelled) return {};
    return out;
}
FLASHMEM void projectFocusedMapping(
    SequencerTrackPastePreflightViewModel& out,
    const SequencerTrackPasteProjection& projection,
    const std::array<
        core::state::sequencer::SequencerTrackActivationTelemetry,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>& telemetry
) {
    if (!projection.plan.hasEntries()) return;
    const auto& entry = projection.plan.entry;
    out.mappingIndex = 0;
    out.mappingCount = 1;
    out.sourceTrack = entry.sourceTrack;
    out.targetTrack = entry.targetTrack;
    out.inheritedLaneCount = entry.inheritedLaneCount;
    out.pinnedLaneCount = entry.pinnedLaneCount;
    out.targetKind = entry.targetKind;
    out.targetRouteValid = entry.targetRouteValid;
    out.targetMidiChannel = entry.targetMidiChannel;
    if (projection.activationGeneration != 0 && entry.targetTrack < telemetry.size()) {
        const auto& activation = telemetry[entry.targetTrack];
        if (activation.generation == projection.activationGeneration &&
            activation.origin == ActivationOrigin::TRACK_PASTE) {
            out.activationOrigin = activation.origin;
            out.activationStatus = activation.status;
        }
    }
}
FLASHMEM void formatSummary(
    SequencerTrackPastePreflightViewModel& out,
    const SequencerTrackPasteProjection& projection
) {
    const auto& entry = projection.plan.entry;
    format(out.header, "Track paste | 1 Track");
    formatSummaryMapping(out.mapping, projection.plan, projection.targetTrack);
    format(
        out.footprint,
        "%s | Mute kept",
        entry.targetKind == core::state::ClipboardTransferTargetKind::FREE
            ? "Free"
            : "Overwrite"
    );
    if (entry.targetRouteValid) {
        format(out.route, "Route | target channel kept live");
    } else {
        format(out.route, "Route | target stays silent");
    }
    format(
        out.laneBindings,
        "CC | %u inherit target | %u pinned",
        static_cast<unsigned>(entry.inheritedLaneCount),
        static_cast<unsigned>(entry.pinnedLaneCount)
    );
}
FLASHMEM void formatDetail(
    SequencerTrackPastePreflightViewModel& out,
    const SequencerTrackPasteProjection& projection
) {
    if (!projection.plan.hasEntries()) return;
    const auto& entry = projection.plan.entry;
    format(out.header, "Track paste | 1/1");
    if (entry.targetRouteValid) {
        format(
            out.mapping,
            "T%u -> T%u | Ch%u",
            static_cast<unsigned>(entry.sourceTrack + 1U),
            static_cast<unsigned>(entry.targetTrack + 1U),
            static_cast<unsigned>(entry.targetMidiChannel + 1U)
        );
        format(
            out.route,
            "Route | target Ch%u checked live",
            static_cast<unsigned>(entry.targetMidiChannel + 1U)
        );
    } else {
        format(
            out.mapping,
            "T%u -> T%u | No route",
            static_cast<unsigned>(entry.sourceTrack + 1U),
            static_cast<unsigned>(entry.targetTrack + 1U)
        );
        format(out.route, "Route | target stays silent");
    }
    format(
        out.footprint,
        "%s | Mute kept",
        entry.targetKind == core::state::ClipboardTransferTargetKind::FREE
            ? "Free"
            : "Overwrite"
    );
    format(
        out.laneBindings,
        "CC | %u inherit target | %u pinned",
        static_cast<unsigned>(entry.inheritedLaneCount),
        static_cast<unsigned>(entry.pinnedLaneCount)
    );
}
}  // namespace

FLASHMEM SequencerTrackPastePreflightViewModel
buildSequencerTrackPastePreflightViewModel(
    const SequencerTrackPasteProjection& projection,
    bool pasteHoldActive,
    const std::array<
        core::state::sequencer::SequencerTrackActivationTelemetry,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>& telemetry
) {
    SequencerTrackPastePreflightViewModel out{};
    const auto activation = activationProjection(projection, telemetry);
    out.activationGeneration = projection.activationGeneration;
    out.operationGeneration = projection.operationGeneration;
    out.operationStatus = projection.feedback.status;
    projectFocusedMapping(out, projection, telemetry);

    FeedbackStatus status = projection.feedback.status;
    if (status == FeedbackStatus::NONE && pasteHoldActive) {
        status = FeedbackStatus::ARMED;
    } else if (status == FeedbackStatus::NONE && projection.plan.canCommit()) {
        status = FeedbackStatus::PREVIEW;
    }
    if (status == FeedbackStatus::QUEUED && activation.exact) {
        if (activation.cancelled) status = FeedbackStatus::CANCELLED;
        else if (activation.fullyApplied) status = FeedbackStatus::APPLIED;
    }

    switch (status) {
        case FeedbackStatus::PREVIEW:
            // A valid clipboard is persistent state, not a reason to keep a
            // large card over the sequencer. The compact action strip remains
            // available at rest; the card is opt-in through Details and is
            // otherwise reserved for the active gesture and its outcome.
            out.visible = projection.plan.canCommit() && projection.detailVisible;
            out.phase = SequencerTrackPastePreflightPhase::READY;
            out.tone = projection.plan.overwriteMask != 0 ||
                    projection.plan.availability ==
                        core::state::ClipboardTransferAvailability::WARNING
                ? SequencerTrackPastePreflightTone::WARNING
                : SequencerTrackPastePreflightTone::CONSTRUCTIVE;
            format(out.detail, "Hold Paste | Details");
            break;
        case FeedbackStatus::PRESSED:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::HOLDING;
            out.tone = SequencerTrackPastePreflightTone::NEUTRAL;
            format(out.detail, "Release | Copy");
            break;
        case FeedbackStatus::ARMED:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::HOLDING;
            out.tone = projection.plan.overwriteMask != 0
                ? SequencerTrackPastePreflightTone::WARNING
                : SequencerTrackPastePreflightTone::CONSTRUCTIVE;
            format(
                out.detail,
                "Hold to paste | %u%% | release cancels",
                static_cast<unsigned>(projection.guard.progressPermille / 10U)
            );
            break;
        case FeedbackStatus::QUEUED:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::QUEUED;
            out.tone = SequencerTrackPastePreflightTone::WARNING;
            format(out.detail, "Next loop | audio pending");
            break;
        case FeedbackStatus::APPLIED:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::APPLIED;
            out.tone = SequencerTrackPastePreflightTone::SUCCESS;
            format(out.detail, "Applied | editor and audio aligned");
            break;
        case FeedbackStatus::CANCELLED:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::CANCELLED;
            out.tone = SequencerTrackPastePreflightTone::NEUTRAL;
            format(out.detail, "Cancelled | no changes");
            break;
        case FeedbackStatus::BLOCKED:
        case FeedbackStatus::FAILED:
        case FeedbackStatus::CONFLICT:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::BLOCKED;
            out.tone = SequencerTrackPastePreflightTone::ERROR;
            format(out.detail, "%s", reasonLabel(projection.plan.reason));
            break;
        case FeedbackStatus::WARNING:
            out.visible = true;
            out.phase = SequencerTrackPastePreflightPhase::READY;
            out.tone = SequencerTrackPastePreflightTone::WARNING;
            format(out.detail, "%s", reasonLabel(projection.plan.reason));
            break;
        case FeedbackStatus::NONE:
        default:
            return out;
    }

    if (!out.visible) return out;
    if (projection.detailVisible) {
        formatDetail(out, projection);
    } else {
        formatSummary(out, projection);
    }
    return out;
}

FLASHMEM bool shouldShowSequencerTrackPasteAppliedConfirmation(
    const SequencerTrackPastePreflightViewModel& model,
    uint32_t dismissedGeneration
) {
    return model.visible &&
           model.phase == SequencerTrackPastePreflightPhase::APPLIED &&
           model.operationGeneration != 0 &&
           model.operationGeneration != dismissedGeneration;
}

}  // namespace core::ui::sequencer

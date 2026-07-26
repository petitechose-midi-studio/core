#include "state/StructureClipboardPastePlan.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::state {

namespace {

using TrackBank = core::state::sequencer::SequencerTrackBankState;

FLASHMEM ClipboardTransferPlan disabledTrackPlan(
    ClipboardTransferPlan plan,
    ClipboardTransferReason reason
) {
    plan.availability = ClipboardTransferAvailability::DISABLED;
    plan.reason = reason;
    return plan;
}

FLASHMEM const core::state::sequencer::SequencerCcLaneBank* sourceCcLanes(
    const StructureClipboardState& clipboard,
    uint8_t clipboardIndex
) {
    return clipboard.kind.get() == StructureClipboardKind::SEQUENCER_TRACK &&
            clipboardIndex == 0
        ? clipboard.sequencerCcLanes.get()
        : nullptr;
}

FLASHMEM void countLanePolicies(
    const core::state::sequencer::SequencerCcLaneBank* bank,
    uint8_t& inherited,
    uint8_t& pinned
) {
    inherited = 0;
    pinned = 0;
    if (bank == nullptr) return;
    for (const auto& lane : bank->lanes) {
        if (!lane.occupied) continue;
        if (lane.destination.routePolicy ==
            core::state::sequencer::SequencerCcLaneRoutePolicy::PINNED) {
            ++pinned;
        } else {
            ++inherited;
        }
    }
}

}  // namespace

FLASHMEM ClipboardTransferPlan buildSequencerTrackClipboardTransferPlan(
    const StructureClipboardState& clipboard,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    uint8_t targetTrack,
    uint16_t pendingTrackMask
) {
    ClipboardTransferPlan plan;
    plan.payloadKind = clipboard.kind.get();
    plan.clipboardRevision = clipboard.revision.get();

    if (plan.payloadKind == StructureClipboardKind::NONE) {
        return disabledTrackPlan(plan, ClipboardTransferReason::EMPTY_CLIPBOARD);
    }
    if (plan.payloadKind != StructureClipboardKind::SEQUENCER_TRACK) {
        return disabledTrackPlan(plan, ClipboardTransferReason::WRONG_PAYLOAD);
    }
    if (clipboard.sequencerTrackSource >= TrackBank::TRACK_COUNT) {
        return disabledTrackPlan(plan, ClipboardTransferReason::INVALID_PAYLOAD);
    }
    if (targetTrack >= TrackBank::TRACK_COUNT) {
        return disabledTrackPlan(plan, ClipboardTransferReason::OUT_OF_RANGE);
    }

    const uint8_t sourceTrack = clipboard.sequencerTrackSource;
    const uint16_t sourceBit = core::state::shared::slotBit(sourceTrack);
    const uint16_t targetBit = core::state::shared::slotBit(targetTrack);
    const bool destinationEnabled = tracks.isTrackEnabled(targetTrack);
    const uint8_t destinationChannel =
        core::state::project::projectTrackMidiChannel(projectTracks, targetTrack);
    uint8_t inheritedLaneCount = 0;
    uint8_t pinnedLaneCount = 0;
    countLanePolicies(
        sourceCcLanes(clipboard, 0),
        inheritedLaneCount,
        pinnedLaneCount
    );

    plan.sourceMask = sourceBit;
    plan.targetMask = targetBit;
    plan.createMask = destinationEnabled ? 0 : targetBit;
    plan.overwriteMask = destinationEnabled ? targetBit : 0;
    plan.bindingPolicy = static_cast<uint8_t>(
        CLIPBOARD_TRANSFER_PRESERVE_ROUTE |
        CLIPBOARD_TRANSFER_PRESERVE_MUTE |
        CLIPBOARD_TRANSFER_PRESERVE_SLOT
    );
    if (inheritedLaneCount > 0) {
        plan.bindingPolicy = static_cast<uint8_t>(
            plan.bindingPolicy | CLIPBOARD_TRANSFER_REBIND_INHERITED
        );
    }
    if (pinnedLaneCount > 0) {
        plan.bindingPolicy = static_cast<uint8_t>(
            plan.bindingPolicy | CLIPBOARD_TRANSFER_PRESERVE_PINNED
        );
    }
    plan.entry = ClipboardTransferPlanEntry{
        .sourceTrack = sourceTrack,
        .targetTrack = targetTrack,
        .targetMidiChannel = destinationChannel,
        .targetRouteValid = destinationChannel <= 15U,
        .targetMuted = core::state::project::projectTrackMuted(
            projectTracks,
            targetTrack
        ),
        .inheritedLaneCount = inheritedLaneCount,
        .pinnedLaneCount = pinnedLaneCount,
        .targetKind = destinationEnabled
            ? ClipboardTransferTargetKind::OVERWRITE
            : ClipboardTransferTargetKind::FREE,
    };
    plan.hasEntry = true;

    if ((targetBit & pendingTrackMask) != 0) {
        return disabledTrackPlan(plan, ClipboardTransferReason::PASTE_PENDING);
    }
    if (sourceTrack == targetTrack) {
        return disabledTrackPlan(plan, ClipboardTransferReason::SAME_TRACK);
    }
    if (!plan.entry.targetRouteValid) {
        plan.availability = ClipboardTransferAvailability::WARNING;
        plan.reason = ClipboardTransferReason::NO_ROUTE;
        return plan;
    }

    plan.availability = ClipboardTransferAvailability::READY;
    plan.reason = ClipboardTransferReason::NONE;
    return plan;
}
FLASHMEM bool sameSequencerTrackClipboardTransferIdentity(
    const ClipboardTransferPlan& lhs,
    const ClipboardTransferPlan& rhs
) {
    const auto& left = lhs.entry;
    const auto& right = rhs.entry;
    return lhs.payloadKind == rhs.payloadKind &&
           lhs.clipboardRevision == rhs.clipboardRevision &&
           lhs.sourceMask == rhs.sourceMask &&
           lhs.targetMask == rhs.targetMask &&
           lhs.createMask == rhs.createMask &&
           lhs.overwriteMask == rhs.overwriteMask &&
           lhs.bindingPolicy == rhs.bindingPolicy &&
           lhs.hasEntry == rhs.hasEntry &&
           left.sourceTrack == right.sourceTrack &&
           left.targetTrack == right.targetTrack &&
           left.targetMuted == right.targetMuted &&
           left.inheritedLaneCount == right.inheritedLaneCount &&
           left.pinnedLaneCount == right.pinnedLaneCount &&
           left.targetKind == right.targetKind;
}
FLASHMEM bool sameSequencerTrackClipboardTransferPlan(
    const ClipboardTransferPlan& lhs,
    const ClipboardTransferPlan& rhs
) {
    return sameSequencerTrackClipboardTransferIdentity(lhs, rhs) &&
           lhs.availability == rhs.availability &&
           lhs.reason == rhs.reason &&
           lhs.entry.targetMidiChannel == rhs.entry.targetMidiChannel &&
           lhs.entry.targetRouteValid == rhs.entry.targetRouteValid;
}
}  // namespace core::state

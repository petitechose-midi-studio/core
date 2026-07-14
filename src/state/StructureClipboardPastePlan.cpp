#include "state/StructureClipboardPastePlan.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

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

FLASHMEM bool collectTrackTransferSources(
    const StructureClipboardState& clipboard,
    ClipboardTransferPlan& plan,
    std::array<uint8_t, ClipboardTransferPlan::MAX_ENTRIES>& sources
) {
    if (plan.payloadKind == StructureClipboardKind::SEQUENCER_TRACK) {
        if (clipboard.sequencerTrackSource >= TrackBank::TRACK_COUNT) return false;
        sources[0] = clipboard.sequencerTrackSource;
        plan.sourceCount = 1;
        return true;
    }

    if (plan.payloadKind != StructureClipboardKind::SEQUENCER_TRACK_SELECTION) {
        return false;
    }

    const auto* selection = clipboard.sequencerTrackSelection.get();
    if (selection == nullptr || !selection->valid || selection->count == 0 ||
        selection->count > selection->tracks.size()) {
        return false;
    }

    uint8_t previousSource = TrackBank::TRACK_COUNT;
    for (uint8_t i = 0; i < selection->count; ++i) {
        const auto& entry = selection->tracks[i];
        if (!entry.valid || entry.sourceTrack >= TrackBank::TRACK_COUNT ||
            (i > 0 && entry.sourceTrack <= previousSource)) {
            return false;
        }
        sources[i] = entry.sourceTrack;
        previousSource = entry.sourceTrack;
    }
    plan.sourceCount = selection->count;
    return true;
}

FLASHMEM const core::state::sequencer::SequencerCcLaneBank* sourceCcLanes(
    const StructureClipboardState& clipboard,
    uint8_t clipboardIndex
) {
    if (clipboard.kind.get() == StructureClipboardKind::SEQUENCER_TRACK) {
        return clipboardIndex == 0 ? clipboard.sequencerCcLanes.get() : nullptr;
    }
    const auto* selection = clipboard.sequencerTrackSelection.get();
    if (selection == nullptr || clipboardIndex >= selection->count) return nullptr;
    return selection->tracks[clipboardIndex].ccLanes.get();
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
    uint8_t targetTrack,
    uint16_t pendingTrackMask,
    const core::state::sequencer::SequencerState* activeEditor
) {
    ClipboardTransferPlan plan;
    plan.payloadKind = clipboard.kind.get();
    plan.clipboardRevision = clipboard.revision.get();

    if (plan.payloadKind == StructureClipboardKind::NONE) {
        return disabledTrackPlan(plan, ClipboardTransferReason::EMPTY_CLIPBOARD);
    }
    if (plan.payloadKind != StructureClipboardKind::SEQUENCER_TRACK &&
        plan.payloadKind != StructureClipboardKind::SEQUENCER_TRACK_SELECTION) {
        return disabledTrackPlan(plan, ClipboardTransferReason::WRONG_PAYLOAD);
    }

    std::array<uint8_t, ClipboardTransferPlan::MAX_ENTRIES> sources{};
    if (!collectTrackTransferSources(clipboard, plan, sources)) {
        return disabledTrackPlan(plan, ClipboardTransferReason::INVALID_PAYLOAD);
    }

    plan.firstSource = sources[0];
    plan.lastSource = sources[plan.sourceCount - 1U];
    for (uint8_t i = 0; i < plan.sourceCount; ++i) {
        plan.sourceMask = static_cast<uint16_t>(
            plan.sourceMask | core::state::shared::slotBit(sources[i])
        );
    }

    plan.targetEndExclusive = static_cast<uint16_t>(targetTrack) + plan.sourceCount;
    if (targetTrack >= TrackBank::TRACK_COUNT ||
        plan.targetEndExclusive > TrackBank::TRACK_COUNT) {
        return disabledTrackPlan(plan, ClipboardTransferReason::OUT_OF_RANGE);
    }

    plan.firstTarget = targetTrack;
    plan.lastTarget = static_cast<uint8_t>(plan.targetEndExclusive - 1U);
    plan.bindingPolicy = static_cast<uint8_t>(
        CLIPBOARD_TRANSFER_PRESERVE_ROUTE |
        CLIPBOARD_TRANSFER_PRESERVE_MUTE |
        CLIPBOARD_TRANSFER_PRESERVE_SLOT
    );

    bool allIdentityMappings = true;
    bool missingRoute = false;
    for (uint8_t i = 0; i < plan.sourceCount; ++i) {
        const uint8_t destination = static_cast<uint8_t>(targetTrack + i);
        const uint16_t destinationBit = core::state::shared::slotBit(destination);
        const bool destinationEnabled = tracks.isTrackEnabled(destination);
        // The live editor owns the current active Track between bank
        // synchronizations. Planning and committing must therefore observe its
        // route for that one slot; every inactive Track remains bank-owned.
        const uint8_t destinationChannel =
            activeEditor != nullptr && destination == tracks.activeTrackIndex()
                ? activeEditor->pattern.midiChannel.get()
                : tracks.track(destination).midiChannel.get();
        uint8_t inheritedLaneCount = 0;
        uint8_t pinnedLaneCount = 0;
        countLanePolicies(
            sourceCcLanes(clipboard, i),
            inheritedLaneCount,
            pinnedLaneCount
        );

        plan.entries[i] = ClipboardTransferPlanEntry{
            .clipboardIndex = i,
            .sourceTrack = sources[i],
            .targetTrack = destination,
            .targetMidiChannel = destinationChannel,
            .targetRouteValid = destinationChannel <= 15U,
            .targetMuted = tracks.isTrackMuted(destination),
            .inheritedLaneCount = inheritedLaneCount,
            .pinnedLaneCount = pinnedLaneCount,
            .targetKind = destinationEnabled
                ? ClipboardTransferTargetKind::OVERWRITE
                : ClipboardTransferTargetKind::FREE,
        };
        ++plan.count;
        plan.inheritedLaneCount = static_cast<uint8_t>(
            plan.inheritedLaneCount + inheritedLaneCount
        );
        plan.pinnedLaneCount = static_cast<uint8_t>(
            plan.pinnedLaneCount + pinnedLaneCount
        );
        plan.targetMask = static_cast<uint16_t>(plan.targetMask | destinationBit);
        if (destinationEnabled) {
            plan.overwriteMask = static_cast<uint16_t>(plan.overwriteMask | destinationBit);
        } else {
            plan.createMask = static_cast<uint16_t>(plan.createMask | destinationBit);
        }
        allIdentityMappings = allIdentityMappings && sources[i] == destination;
        missingRoute = missingRoute || destinationChannel > 15U;
    }

    if (plan.inheritedLaneCount > 0) {
        plan.bindingPolicy = static_cast<uint8_t>(
            plan.bindingPolicy | CLIPBOARD_TRANSFER_REBIND_INHERITED
        );
    }
    if (plan.pinnedLaneCount > 0) {
        plan.bindingPolicy = static_cast<uint8_t>(
            plan.bindingPolicy | CLIPBOARD_TRANSFER_PRESERVE_PINNED
        );
    }

    if ((plan.targetMask & pendingTrackMask) != 0) {
        return disabledTrackPlan(plan, ClipboardTransferReason::PASTE_PENDING);
    }
    if (allIdentityMappings) {
        return disabledTrackPlan(plan, ClipboardTransferReason::SAME_TRACK);
    }
    if (missingRoute) {
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
    if (lhs.payloadKind != rhs.payloadKind ||
        lhs.clipboardRevision != rhs.clipboardRevision ||
        lhs.sourceMask != rhs.sourceMask || lhs.targetMask != rhs.targetMask ||
        lhs.createMask != rhs.createMask ||
        lhs.overwriteMask != rhs.overwriteMask ||
        lhs.targetEndExclusive != rhs.targetEndExclusive ||
        lhs.sourceCount != rhs.sourceCount || lhs.count != rhs.count ||
        lhs.firstSource != rhs.firstSource || lhs.lastSource != rhs.lastSource ||
        lhs.firstTarget != rhs.firstTarget || lhs.lastTarget != rhs.lastTarget ||
        lhs.bindingPolicy != rhs.bindingPolicy ||
        lhs.inheritedLaneCount != rhs.inheritedLaneCount ||
        lhs.pinnedLaneCount != rhs.pinnedLaneCount) {
        return false;
    }
    for (uint8_t i = 0; i < lhs.count; ++i) {
        const auto& left = lhs.entries[i];
        const auto& right = rhs.entries[i];
        if (left.clipboardIndex != right.clipboardIndex ||
            left.sourceTrack != right.sourceTrack ||
            left.targetTrack != right.targetTrack ||
            left.targetMuted != right.targetMuted ||
            left.inheritedLaneCount != right.inheritedLaneCount ||
            left.pinnedLaneCount != right.pinnedLaneCount ||
            left.targetKind != right.targetKind) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameSequencerTrackClipboardTransferPlan(
    const ClipboardTransferPlan& lhs,
    const ClipboardTransferPlan& rhs
) {
    if (!sameSequencerTrackClipboardTransferIdentity(lhs, rhs) ||
        lhs.availability != rhs.availability || lhs.reason != rhs.reason) {
        return false;
    }
    for (uint8_t i = 0; i < lhs.count; ++i) {
        const auto& left = lhs.entries[i];
        const auto& right = rhs.entries[i];
        if (left.targetMidiChannel != right.targetMidiChannel ||
            left.targetRouteValid != right.targetRouteValid) {
            return false;
        }
    }
    return true;
}

FLASHMEM SequencerPageSelectionPastePlan buildSequencerPageSelectionPastePlan(
    const SequencerPageSelectionClipboard& clipboard,
    uint8_t cursorPage,
    uint8_t activePageCount
) {
    SequencerPageSelectionPastePlan plan;
    if (!clipboard.valid || clipboard.count == 0) return plan;
    if (clipboard.sourceFirstPage >= core::state::sequencer::SequencerPatternState::PAGE_COUNT) {
        return plan;
    }

    const uint8_t pageLimit = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    const uint8_t clampedCursor = std::min<uint8_t>(
        cursorPage,
        static_cast<uint8_t>(pageLimit - 1U)
    );
    const uint8_t clampedActivePageCount = std::min<uint8_t>(activePageCount, pageLimit);

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const auto& page = clipboard.pages[i];
        if (!page.valid || page.sourcePage < clipboard.sourceFirstPage) continue;

        const uint16_t destination = static_cast<uint16_t>(clampedCursor) +
            static_cast<uint16_t>(page.sourcePage - clipboard.sourceFirstPage);
        if (destination >= pageLimit) continue;
        if (plan.count >= plan.entries.size()) break;

        const auto destinationPage = static_cast<uint8_t>(destination);
        plan.entries[plan.count++] = {
            .clipboardIndex = i,
            .destinationPage = destinationPage,
        };
        plan.destinationMask = static_cast<uint16_t>(
            plan.destinationMask | core::state::shared::slotBit(destinationPage)
        );
        if (destinationPage < clampedActivePageCount) {
            plan.overwriteMask = static_cast<uint16_t>(
                plan.overwriteMask | core::state::shared::slotBit(destinationPage)
            );
        }
        plan.firstDestinationPage = std::min(plan.firstDestinationPage, destinationPage);
    }

    return plan;
}

}  // namespace core::state

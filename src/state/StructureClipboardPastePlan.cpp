#include "state/StructureClipboardPastePlan.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

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

        plan.entries[i] = ClipboardTransferPlanEntry{
            .clipboardIndex = i,
            .sourceTrack = sources[i],
            .targetTrack = destination,
            .targetMidiChannel = destinationChannel,
            .targetRouteValid = destinationChannel <= 15U,
            .targetMuted = tracks.isTrackMuted(destination),
            .targetKind = destinationEnabled
                ? ClipboardTransferTargetKind::OVERWRITE
                : ClipboardTransferTargetKind::FREE,
        };
        ++plan.count;
        plan.targetMask = static_cast<uint16_t>(plan.targetMask | destinationBit);
        if (destinationEnabled) {
            plan.overwriteMask = static_cast<uint16_t>(plan.overwriteMask | destinationBit);
        } else {
            plan.createMask = static_cast<uint16_t>(plan.createMask | destinationBit);
        }
        allIdentityMappings = allIdentityMappings && sources[i] == destination;
        missingRoute = missingRoute || destinationChannel > 15U;
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

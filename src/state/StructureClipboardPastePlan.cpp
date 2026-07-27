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

FLASHMEM bool collectTrackTransferSources(
    const StructureClipboardState& clipboard,
    ClipboardTransferPlan& plan,
    std::array<uint8_t, ClipboardTransferPlan::MAX_ENTRIES>& sources
) {
    if (plan.payloadKind == StructureClipboardKind::SEQUENCER_TRACK) {
        if (clipboard.sequencerTrackSource >= TrackBank::TRACK_COUNT) {
            return false;
        }
        sources[0] = clipboard.sequencerTrackSource;
        plan.sourceCount = 1U;
        return true;
    }

    if (plan.payloadKind !=
        StructureClipboardKind::SEQUENCER_TRACK_SELECTION) {
        return false;
    }

    const auto* selection = clipboard.sequencerTrackSelection.get();
    if (selection == nullptr || !selection->valid ||
        !selection->projectControl ||
        selection->count == 0U ||
        selection->count > selection->tracks.size()) {
        return false;
    }

    uint8_t previousSource = TrackBank::TRACK_COUNT;
    for (uint8_t index = 0; index < selection->count; ++index) {
        const auto& source = selection->tracks[index];
        if (!source.valid || source.sourceTrack >= TrackBank::TRACK_COUNT ||
            (index > 0U && source.sourceTrack <= previousSource)) {
            return false;
        }
        sources[index] = source.sourceTrack;
        previousSource = source.sourceTrack;
    }
    plan.sourceCount = selection->count;
    return true;
}

FLASHMEM const core::state::sequencer::SequencerCcLaneBank* sourceCcLanes(
    const StructureClipboardState& clipboard,
    uint8_t clipboardIndex
) {
    if (clipboard.kind.get() == StructureClipboardKind::SEQUENCER_TRACK) {
        return clipboardIndex == 0U
            ? clipboard.sequencerCcLanes.get()
            : nullptr;
    }
    const auto* selection = clipboard.sequencerTrackSelection.get();
    if (selection == nullptr || clipboardIndex >= selection->count) {
        return nullptr;
    }
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
    if (plan.payloadKind != StructureClipboardKind::SEQUENCER_TRACK &&
        plan.payloadKind !=
            StructureClipboardKind::SEQUENCER_TRACK_SELECTION) {
        return disabledTrackPlan(plan, ClipboardTransferReason::WRONG_PAYLOAD);
    }

    std::array<uint8_t, ClipboardTransferPlan::MAX_ENTRIES> sources{};
    if (!collectTrackTransferSources(clipboard, plan, sources)) {
        return disabledTrackPlan(plan, ClipboardTransferReason::INVALID_PAYLOAD);
    }

    if (targetTrack >= TrackBank::TRACK_COUNT) {
        return disabledTrackPlan(plan, ClipboardTransferReason::OUT_OF_RANGE);
    }

    plan.firstSource = sources[0];
    plan.lastSource = sources[plan.sourceCount - 1U];
    plan.firstTarget = targetTrack;
    plan.targetEndExclusive = static_cast<uint16_t>(targetTrack) +
        static_cast<uint16_t>(
            plan.lastSource - plan.firstSource + 1U
        );
    plan.bindingPolicy = static_cast<uint8_t>(
        CLIPBOARD_TRANSFER_PRESERVE_ROUTE |
        CLIPBOARD_TRANSFER_PRESERVE_MUTE |
        CLIPBOARD_TRANSFER_PRESERVE_SLOT
    );

    bool outOfRange = false;
    bool allIdentityMappings = true;
    bool missingRoute = false;
    for (uint8_t index = 0; index < plan.sourceCount; ++index) {
        const uint8_t sourceTrack = sources[index];
        plan.sourceMask = static_cast<uint16_t>(
            plan.sourceMask |
            core::state::shared::slotBit(sourceTrack)
        );
        const uint16_t destinationWide =
            static_cast<uint16_t>(targetTrack) +
            static_cast<uint16_t>(
                sourceTrack - plan.firstSource
            );
        if (destinationWide >= TrackBank::TRACK_COUNT) {
            outOfRange = true;
            allIdentityMappings = false;
            continue;
        }

        const auto destination = static_cast<uint8_t>(destinationWide);
        const uint16_t destinationBit =
            core::state::shared::slotBit(destination);
        const bool destinationEnabled =
            tracks.isTrackEnabled(destination);
        const uint8_t destinationChannel =
            core::state::project::projectTrackMidiChannel(
                projectTracks,
                destination
            );
        uint8_t inheritedLaneCount = 0;
        uint8_t pinnedLaneCount = 0;
        countLanePolicies(
            sourceCcLanes(clipboard, index),
            inheritedLaneCount,
            pinnedLaneCount
        );

        auto& entry = plan.entries[plan.count++];
        entry = ClipboardTransferPlanEntry{
            .clipboardIndex = index,
            .sourceTrack = sourceTrack,
            .targetTrack = destination,
            .targetMidiChannel = destinationChannel,
            .targetRouteValid = destinationChannel <= 15U,
            .targetMuted =
                core::state::project::projectTrackMuted(
                    projectTracks,
                    destination
                ),
            .inheritedLaneCount = inheritedLaneCount,
            .pinnedLaneCount = pinnedLaneCount,
            .targetKind = destinationEnabled
                ? ClipboardTransferTargetKind::OVERWRITE
                : ClipboardTransferTargetKind::FREE,
        };
        plan.targetMask = static_cast<uint16_t>(
            plan.targetMask | destinationBit
        );
        if (destinationEnabled) {
            plan.overwriteMask = static_cast<uint16_t>(
                plan.overwriteMask | destinationBit
            );
        } else {
            plan.createMask = static_cast<uint16_t>(
                plan.createMask | destinationBit
            );
        }
        plan.inheritedLaneCount = static_cast<uint8_t>(
            plan.inheritedLaneCount + inheritedLaneCount
        );
        plan.pinnedLaneCount = static_cast<uint8_t>(
            plan.pinnedLaneCount + pinnedLaneCount
        );
        plan.lastTarget = destination;
        allIdentityMappings =
            allIdentityMappings && sourceTrack == destination;
        missingRoute = missingRoute || destinationChannel > 15U;
    }

    plan.hasEntry = plan.count > 0U;
    if (plan.hasEntry) plan.entry = plan.entries[0];

    if (plan.inheritedLaneCount > 0U) {
        plan.bindingPolicy = static_cast<uint8_t>(
            plan.bindingPolicy | CLIPBOARD_TRANSFER_REBIND_INHERITED
        );
    }
    if (plan.pinnedLaneCount > 0U) {
        plan.bindingPolicy = static_cast<uint8_t>(
            plan.bindingPolicy | CLIPBOARD_TRANSFER_PRESERVE_PINNED
        );
    }

    if (outOfRange || plan.count != plan.sourceCount) {
        return disabledTrackPlan(
            plan,
            ClipboardTransferReason::OUT_OF_RANGE
        );
    }
    if ((plan.targetMask & pendingTrackMask) != 0U) {
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
        lhs.sourceMask != rhs.sourceMask ||
        lhs.targetMask != rhs.targetMask ||
        lhs.createMask != rhs.createMask ||
        lhs.overwriteMask != rhs.overwriteMask ||
        lhs.targetEndExclusive != rhs.targetEndExclusive ||
        lhs.sourceCount != rhs.sourceCount ||
        lhs.count != rhs.count ||
        lhs.firstSource != rhs.firstSource ||
        lhs.lastSource != rhs.lastSource ||
        lhs.firstTarget != rhs.firstTarget ||
        lhs.lastTarget != rhs.lastTarget ||
        lhs.bindingPolicy != rhs.bindingPolicy ||
        lhs.inheritedLaneCount != rhs.inheritedLaneCount ||
        lhs.pinnedLaneCount != rhs.pinnedLaneCount ||
        lhs.hasEntry != rhs.hasEntry) {
        return false;
    }
    for (uint8_t index = 0; index < lhs.count; ++index) {
        const auto& left = lhs.entries[index];
        const auto& right = rhs.entries[index];
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
        lhs.availability != rhs.availability ||
        lhs.reason != rhs.reason) {
        return false;
    }
    for (uint8_t index = 0; index < lhs.count; ++index) {
        const auto& left = lhs.entries[index];
        const auto& right = rhs.entries[index];
        if (left.targetMidiChannel != right.targetMidiChannel ||
            left.targetRouteValid != right.targetRouteValid) {
            return false;
        }
    }
    return true;
}

FLASHMEM SequencerPageSelectionPastePlan
buildSequencerPageSelectionPastePlan(
    const SequencerPageSelectionClipboard& clipboard,
    uint8_t cursorPage,
    uint8_t activePageCount
) {
    using Pattern = core::state::sequencer::SequencerPatternState;

    SequencerPageSelectionPastePlan plan;
    if (!clipboard.valid || clipboard.count == 0U) {
        return plan;
    }
    if (clipboard.sourceFirstPage >= Pattern::PAGE_COUNT ||
        clipboard.count > clipboard.pages.size()) {
        plan.reason = ClipboardTransferReason::INVALID_PAYLOAD;
        return plan;
    }
    if (cursorPage >= Pattern::PAGE_COUNT) {
        plan.reason = ClipboardTransferReason::OUT_OF_RANGE;
        return plan;
    }

    plan.sourceCount = clipboard.count;
    const uint8_t boundedActivePageCount =
        std::min<uint8_t>(activePageCount, Pattern::PAGE_COUNT);
    bool invalidPayload = false;
    bool outOfRange = false;
    uint8_t previousSource = Pattern::PAGE_COUNT;
    for (uint8_t index = 0; index < clipboard.count; ++index) {
        const auto& source = clipboard.pages[index];
        if (!source.valid ||
            source.sourcePage < clipboard.sourceFirstPage ||
            (index > 0U && source.sourcePage <= previousSource)) {
            invalidPayload = true;
            break;
        }
        previousSource = source.sourcePage;

        const uint16_t destinationWide =
            static_cast<uint16_t>(cursorPage) +
            static_cast<uint16_t>(
                source.sourcePage - clipboard.sourceFirstPage
            );
        if (destinationWide >= Pattern::PAGE_COUNT) {
            outOfRange = true;
            continue;
        }

        const auto destination = static_cast<uint8_t>(destinationWide);
        const uint16_t destinationBit =
            core::state::shared::slotBit(destination);
        plan.entries[plan.count++] = {
            .clipboardIndex = index,
            .destinationPage = destination,
        };
        plan.destinationMask = static_cast<uint16_t>(
            plan.destinationMask | destinationBit
        );
        if (destination < boundedActivePageCount) {
            plan.overwriteMask = static_cast<uint16_t>(
                plan.overwriteMask | destinationBit
            );
        } else {
            plan.createMask = static_cast<uint16_t>(
                plan.createMask | destinationBit
            );
        }
        if (plan.firstDestinationPage >= Pattern::PAGE_COUNT) {
            plan.firstDestinationPage = destination;
        }
        plan.lastDestinationPage = destination;
    }

    if (invalidPayload) {
        plan.availability = ClipboardTransferAvailability::DISABLED;
        plan.reason = ClipboardTransferReason::INVALID_PAYLOAD;
        return plan;
    }
    if (outOfRange || plan.count != plan.sourceCount) {
        plan.availability = ClipboardTransferAvailability::DISABLED;
        plan.reason = ClipboardTransferReason::OUT_OF_RANGE;
        return plan;
    }

    plan.requiredPageCount = static_cast<uint8_t>(
        std::max<uint16_t>(
            boundedActivePageCount,
            static_cast<uint16_t>(plan.lastDestinationPage) + 1U
        )
    );
    plan.availability = ClipboardTransferAvailability::READY;
    plan.reason = ClipboardTransferReason::NONE;
    return plan;
}

FLASHMEM MacroPageSelectionPastePlan
buildMacroPageSelectionPastePlan(
    const MacroPageSelectionClipboard& clipboard,
    uint8_t cursorPage,
    uint8_t activePageCount
) {
    using namespace core::state::macro;

    MacroPageSelectionPastePlan plan;
    if (!clipboard.valid || clipboard.count == 0U ||
        !clipboard.projectControl) {
        return plan;
    }
    if (clipboard.sourceTrack >= TRACK_COUNT ||
        clipboard.sourceFirstPage >= PAGE_COUNT ||
        clipboard.count > clipboard.pages.size()) {
        plan.reason = ClipboardTransferReason::INVALID_PAYLOAD;
        return plan;
    }
    if (cursorPage >= PAGE_COUNT) {
        plan.reason = ClipboardTransferReason::OUT_OF_RANGE;
        return plan;
    }

    plan.sourceCount = clipboard.count;
    const uint8_t boundedPageCount =
        std::min<uint8_t>(activePageCount, PAGE_COUNT);
    uint8_t previousSource = PAGE_COUNT;
    for (uint8_t index = 0U;
         index < clipboard.count;
         ++index) {
        const auto& source = clipboard.pages[index];
        if (!source.valid ||
            source.sourcePage < clipboard.sourceFirstPage ||
            source.sourcePage >= PAGE_COUNT ||
            (index > 0U && source.sourcePage <= previousSource)) {
            plan.reason = ClipboardTransferReason::INVALID_PAYLOAD;
            return plan;
        }
        previousSource = source.sourcePage;
        const uint16_t destinationWide =
            static_cast<uint16_t>(cursorPage) +
            static_cast<uint16_t>(
                source.sourcePage - clipboard.sourceFirstPage
            );
        if (destinationWide >= PAGE_COUNT) {
            plan.reason = ClipboardTransferReason::OUT_OF_RANGE;
            return plan;
        }
        const auto destination =
            static_cast<uint8_t>(destinationWide);
        const uint16_t bit = static_cast<uint16_t>(
            1U << destination
        );
        plan.entries[plan.count++] = {
            .clipboardIndex = index,
            .destinationPage = destination,
        };
        plan.destinationMask = static_cast<uint16_t>(
            plan.destinationMask | bit
        );
        if (destination < boundedPageCount) {
            plan.overwriteMask = static_cast<uint16_t>(
                plan.overwriteMask | bit
            );
        } else {
            plan.createMask = static_cast<uint16_t>(
                plan.createMask | bit
            );
        }
        if (plan.firstDestinationPage >= PAGE_COUNT) {
            plan.firstDestinationPage = destination;
        }
        plan.lastDestinationPage = destination;
    }
    plan.requiredPageCount = static_cast<uint8_t>(
        std::max<uint16_t>(
            boundedPageCount,
            static_cast<uint16_t>(plan.lastDestinationPage) + 1U
        )
    );
    plan.availability = ClipboardTransferAvailability::READY;
    plan.reason = ClipboardTransferReason::NONE;
    return plan;
}
}  // namespace core::state

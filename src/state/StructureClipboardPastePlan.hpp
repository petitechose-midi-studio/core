#pragma once

#include <array>
#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {
struct SequencerState;
}

namespace core::state {

enum class ClipboardTransferAvailability : uint8_t {
    READY = 0,
    WARNING,
    DISABLED,
};

enum class ClipboardTransferReason : uint8_t {
    NONE = 0,
    EMPTY_CLIPBOARD,
    WRONG_PAYLOAD,
    INVALID_PAYLOAD,
    SAME_TRACK,
    OUT_OF_RANGE,
    CAPACITY,
    PASTE_PENDING,
    NO_ROUTE,
    HISTORY_UNAVAILABLE,
    ALLOCATION_UNAVAILABLE,
};

enum class ClipboardTransferTargetKind : uint8_t {
    FREE = 0,
    OVERWRITE,
};

enum ClipboardTransferBinding : uint8_t {
    CLIPBOARD_TRANSFER_PRESERVE_ROUTE = 1U << 0,
    CLIPBOARD_TRANSFER_PRESERVE_MUTE = 1U << 1,
    CLIPBOARD_TRANSFER_PRESERVE_SLOT = 1U << 2,
    CLIPBOARD_TRANSFER_REBIND_INHERITED = 1U << 3,
    CLIPBOARD_TRANSFER_PRESERVE_PINNED = 1U << 4,
};

struct ClipboardTransferPlanEntry {
    uint8_t clipboardIndex = 0;
    uint8_t sourceTrack = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t targetTrack = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t targetMidiChannel = 0;
    bool targetRouteValid = false;
    bool targetMuted = false;
    uint8_t inheritedLaneCount = 0;
    uint8_t pinnedLaneCount = 0;
    ClipboardTransferTargetKind targetKind = ClipboardTransferTargetKind::FREE;
};

/**
 * Non-mutating Track transfer preflight shared by handlers, presenters and UX
 * traces. Source selections are projected densely onto contiguous destination
 * Tracks; no entry is ever silently clipped or skipped.
 */
struct ClipboardTransferPlan {
    static constexpr uint8_t MAX_ENTRIES =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    StructureClipboardKind payloadKind = StructureClipboardKind::NONE;
    uint32_t clipboardRevision = 0;
    uint16_t sourceMask = 0;
    uint16_t targetMask = 0;
    uint16_t createMask = 0;
    uint16_t overwriteMask = 0;
    uint16_t targetEndExclusive = 0;
    uint8_t sourceCount = 0;
    uint8_t count = 0;
    uint8_t firstSource = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t lastSource = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t firstTarget = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t lastTarget = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t bindingPolicy = 0;
    uint8_t inheritedLaneCount = 0;
    uint8_t pinnedLaneCount = 0;
    ClipboardTransferAvailability availability = ClipboardTransferAvailability::DISABLED;
    ClipboardTransferReason reason = ClipboardTransferReason::EMPTY_CLIPBOARD;
    std::array<ClipboardTransferPlanEntry, MAX_ENTRIES> entries{};

    bool hasEntries() const { return count > 0; }
    bool canCommit() const {
        return availability != ClipboardTransferAvailability::DISABLED &&
               sourceCount > 0 && count == sourceCount;
    }
};

ClipboardTransferPlan buildSequencerTrackClipboardTransferPlan(
    const StructureClipboardState& clipboard,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    uint8_t targetTrack,
    uint16_t pendingTrackMask = 0,
    const core::state::sequencer::SequencerState* activeEditor = nullptr
);

/**
 * Stable Track-paste identity, excluding destination route fields which are
 * deliberately refreshed at preflight/commit time.
 */
bool sameSequencerTrackClipboardTransferIdentity(
    const ClipboardTransferPlan& lhs,
    const ClipboardTransferPlan& rhs
);

/** Complete bounded value comparison, including live destination routes. */
bool sameSequencerTrackClipboardTransferPlan(
    const ClipboardTransferPlan& lhs,
    const ClipboardTransferPlan& rhs
);

struct SequencerPageSelectionPastePlanEntry {
    uint8_t clipboardIndex = 0;
    uint8_t destinationPage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
};

struct SequencerPageSelectionPastePlan {
    static constexpr uint8_t MAX_ENTRIES =
        core::state::sequencer::SequencerPatternState::PAGE_COUNT;

    uint16_t destinationMask = 0;
    uint16_t overwriteMask = 0;
    uint8_t firstDestinationPage =
        core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    uint8_t count = 0;
    std::array<SequencerPageSelectionPastePlanEntry, MAX_ENTRIES> entries{};

    bool hasEntries() const { return count > 0; }
};

SequencerPageSelectionPastePlan buildSequencerPageSelectionPastePlan(
    const SequencerPageSelectionClipboard& clipboard,
    uint8_t cursorPage,
    uint8_t activePageCount
);

}  // namespace core::state

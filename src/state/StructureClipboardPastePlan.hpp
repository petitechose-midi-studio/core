#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

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
 * traces. The current direct Track workflow transfers one source Track to the
 * focused destination without retaining an inaccessible multi-selection path.
 */
struct ClipboardTransferPlan {
    StructureClipboardKind payloadKind = StructureClipboardKind::NONE;
    uint32_t clipboardRevision = 0;
    uint16_t sourceMask = 0;
    uint16_t targetMask = 0;
    uint16_t createMask = 0;
    uint16_t overwriteMask = 0;
    uint8_t bindingPolicy = 0;
    ClipboardTransferAvailability availability = ClipboardTransferAvailability::DISABLED;
    ClipboardTransferReason reason = ClipboardTransferReason::EMPTY_CLIPBOARD;
    ClipboardTransferPlanEntry entry{};
    bool hasEntry = false;

    bool hasEntries() const { return hasEntry; }
    bool canCommit() const {
        return availability != ClipboardTransferAvailability::DISABLED &&
               hasEntry;
    }
};

ClipboardTransferPlan buildSequencerTrackClipboardTransferPlan(
    const StructureClipboardState& clipboard,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    uint8_t targetTrack,
    uint16_t pendingTrackMask = 0
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

}  // namespace core::state

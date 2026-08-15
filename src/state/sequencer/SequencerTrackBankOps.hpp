#pragma once

#include <cstdint>

#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

/**
 * Non-owning flat Pattern view used by a prepared active-Track rotation.
 *
 * SequencerPatternSnapshot deliberately does not carry the Pattern-owned CC
 * revision, so the caller supplies it beside the immutable snapshot. Both
 * values must remain alive and unchanged through commit.
 */
struct SequencerTrackFlatSnapshotView {
    const SequencerPatternSnapshot* snapshot = nullptr;
    uint32_t ccLaneRevision = 0U;
};

enum class SequencerActiveTrackIncomingOwnerPolicy : uint8_t {
    Preserve = 0,
    Reset,
};

/**
 * Scalar precondition and immutable flat views for one no-publish rotation.
 *
 * The owner identities are captured during prepare and revalidated immediately
 * before commit. No Graph, CC bank or flat snapshot is owned by this plan.
 */
struct SequencerPreparedActiveTrackRotation {
    SequencerTrackFlatSnapshotView expectedOutgoing{};
    SequencerTrackFlatSnapshotView expectedIncoming{};
    SequencerTrackFlatSnapshotView finalOutgoing{};
    SequencerTrackFlatSnapshotView finalIncoming{};

    const oc::note::sequencer::StepSequencerGraph* expectedEditorGraphOwner = nullptr;
    const SequencerCcLaneBank* expectedEditorCcLaneOwner = nullptr;
    const oc::note::sequencer::StepSequencerGraph* expectedOutgoingGraphOwner = nullptr;
    const SequencerCcLaneBank* expectedOutgoingCcLaneOwner = nullptr;
    const oc::note::sequencer::StepSequencerGraph* expectedIncomingGraphOwner = nullptr;
    const SequencerCcLaneBank* expectedIncomingCcLaneOwner = nullptr;

    uint16_t expectedEnabledMask = 0U;
    uint8_t outgoingTrack = SequencerTrackBankState::TRACK_COUNT;
    uint8_t incomingTrack = SequencerTrackBankState::TRACK_COUNT;
    SequencerActiveTrackIncomingOwnerPolicy incomingOwnerPolicy =
        SequencerActiveTrackIncomingOwnerPolicy::Preserve;
};

static_assert(
    sizeof(void*) != 4U || sizeof(SequencerPreparedActiveTrackRotation) <= 80U,
    "active-Track rotation must remain a bounded scalar ARM plan"
);
static_assert(
    sizeof(SequencerPreparedActiveTrackRotation) <= 128U,
    "active-Track rotation must remain a bounded native plan"
);

/**
 * Resolves the canonical Pattern for a Track.
 *
 * The retained editor owns the active Track; its matching bank slot is only
 * rotation scratch. Inactive Tracks remain canonical in the bank.
 */
[[nodiscard]] const SequencerPatternState& canonicalTrackPattern(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex
) noexcept;

/**
 * Mutable canonical Pattern selection for an already-admitted transaction.
 * Revision publication and active-editor synchronization remain caller-owned.
 */
[[nodiscard]] SequencerPatternState& mutableCanonicalTrackPattern(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t trackIndex
) noexcept;

/** Exact allocation-free comparison of persisted flat bytes and revisions. */
[[nodiscard]] bool sequencerPatternMatchesFlatSnapshot(
    const SequencerPatternState& pattern,
    SequencerTrackFlatSnapshotView expected
) noexcept;

/**
 * Captures the three live Graph/CC owner identities and validates both the
 * expected-before and final flat views without mutating live state.
 */
[[nodiscard]] bool prepareActiveTrackOwnerRotation(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t incomingTrack,
    SequencerTrackFlatSnapshotView expectedOutgoing,
    SequencerTrackFlatSnapshotView expectedIncoming,
    SequencerTrackFlatSnapshotView finalOutgoing,
    SequencerTrackFlatSnapshotView finalIncoming,
    SequencerActiveTrackIncomingOwnerPolicy incomingOwnerPolicy,
    SequencerPreparedActiveTrackRotation& out
) noexcept;

/** Side-effect-free final revalidation for a prepared owner rotation. */
[[nodiscard]] bool preparedActiveTrackOwnerRotationMatches(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerPreparedActiveTrackRotation& prepared
) noexcept;

/**
 * Allocation-free, no-fail commit after preparedActiveTrackOwnerRotationMatches().
 *
 * Rotates editor/outgoing/incoming Graph and CC owners, installs the two final
 * flat views and resets Track-scoped transient editor state. Shared topology
 * and all higher-level publication/settlement remain caller-owned.
 */
void rotateActiveTrackOwnersNoPublish(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerPreparedActiveTrackRotation& prepared
) noexcept;

/**
 * Synchronizes the active sequencer editor with the persisted per-track bank.
 *
 * Switching tracks stores the current editor, loads the target track, and clears
 * transient edit overlays while preserving persistent pattern data.
 */
[[nodiscard]] bool initializeTrackBankFromActive(
    SequencerTrackBankState& bank,
    const SequencerState& active
);

[[nodiscard]] bool storeActiveTrack(
    SequencerTrackBankState& bank,
    const SequencerState& active
);

// Avoids a Graph allocation when graph revisions are already synchronized.
// Pattern-owned CC lanes are still copied so a switched Track's spare bank
// payload can never be mistaken for the active editor merely by revision.
[[nodiscard]] bool storeActiveTrackPreservingGraph(
    SequencerTrackBankState& bank,
    const SequencerState& active
);

// Clears editor-only state when a prepared transaction changes the active
// Track without going through switchActiveTrack().
void resetTransientTrackState(SequencerState& state);

bool switchActiveTrack(SequencerTrackBankState& bank, SequencerState& active, uint8_t nextTrack);

void captureTrackBankSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerTrackBankSnapshot& out
);

void applyTrackBankSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerTrackBankSnapshot& snapshot
);

}  // namespace core::state::sequencer

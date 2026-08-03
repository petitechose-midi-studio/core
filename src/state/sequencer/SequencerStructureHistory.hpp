#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::state::sequencer {

struct SequencerHistoryMacroTrackStructurePayload {
    static constexpr uint8_t INVALID_AFFECTED_TRACK = macro::TRACK_COUNT;

    uint16_t capturedTrackMask = 0U;
    bool afterCaptured = false;
    // Reuses the ARM padding byte that previously followed afterCaptured.
    // Direct Macro actions set one exact target; multi-Track transfers retain
    // INVALID_AFFECTED_TRACK and use their activation Track mask.
    uint8_t affectedTrackIndex = INVALID_AFFECTED_TRACK;
    std::array<core::state::macro::MacroTrackData, macro::TRACK_COUNT>
        beforeTracks{};
    std::array<core::state::macro::MacroTrackData, macro::TRACK_COUNT>
        afterTracks{};
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > beforeControl{};
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > afterControl{};
};

struct SequencerHistoryTrackStructureSnapshot {
    uint16_t enabledMask = 0x0001;
    uint8_t activeTrack = 0;
    uint8_t focusedStep = 0;
    uint8_t page = 0;
    uint16_t capturedTrackMask = 0x0001;
    std::array<SequencerHistoryPatternSnapshot, SequencerTrackBankState::TRACK_COUNT> tracks{};

    SequencerHistoryTrackStructureSnapshot();
    ~SequencerHistoryTrackStructureSnapshot();
    SequencerHistoryTrackStructureSnapshot(const SequencerHistoryTrackStructureSnapshot&) = delete;
    SequencerHistoryTrackStructureSnapshot& operator=(
        const SequencerHistoryTrackStructureSnapshot&
    ) = delete;
    SequencerHistoryTrackStructureSnapshot(SequencerHistoryTrackStructureSnapshot&&) noexcept;
    SequencerHistoryTrackStructureSnapshot& operator=(
        SequencerHistoryTrackStructureSnapshot&&
    ) noexcept;
    void reset();
};

struct SequencerHistoryTrackStructureChange {
    SequencerHistoryDescriptor descriptor{};
    // Stable Track paste operation identity. Each audible Undo/Redo transition
    // receives a fresh generation, while stacked operations may safely rebind
    // the single realtime slot for a Track.
    SequencerTrackActivationHistoryRef activation{};
    // Canonical audible targets captured with the operation. Structure
    // snapshots still retain derived runtime views, but activation
    // never derives realtime behaviour from them. Two masks are required when
    // the operation creates Tracks: Undo targets the old enabled topology,
    // Redo targets the new one while preserving Mute/Solo semantics.
    uint16_t activationBeforeAudibleMask = 0;
    uint16_t activationAfterAudibleMask = 0;
    SequencerHistoryTrackStructureSnapshot before;
    SequencerHistoryTrackStructureSnapshot after;
    core::app::ExtmemUniquePtr<SequencerHistoryMacroTrackStructurePayload>
        macroStructure{};

    SequencerHistoryTrackStructureChange();
    ~SequencerHistoryTrackStructureChange();
    SequencerHistoryTrackStructureChange(const SequencerHistoryTrackStructureChange&) = delete;
    SequencerHistoryTrackStructureChange& operator=(
        const SequencerHistoryTrackStructureChange&
    ) = delete;
    SequencerHistoryTrackStructureChange(SequencerHistoryTrackStructureChange&&) noexcept;
    SequencerHistoryTrackStructureChange& operator=(
        SequencerHistoryTrackStructureChange&&
    ) noexcept;
};

enum class SequencerStructureHistoryReplayPrepareOutcome : uint8_t {
    Unavailable = 0,
    Rejected,
    Prepared,
};

// Detached target owners and exact retained-entry proof for one coupled
// Structure Undo/Redo. The History entry stays on its source stack until the
// atomic activation gate has succeeded; commit then consumes these owners in
// an allocation-free tail.
struct SequencerPreparedStructureHistoryReplay {
    SequencerHistoryDirection direction = SequencerHistoryDirection::Undo;
    uintptr_t entryIdentity = 0U;
    const SequencerHistoryTrackStructureChange* entry = nullptr;
    const SequencerHistoryTrackStructureSnapshot* targetSnapshot = nullptr;
    const SequencerHistoryMacroTrackStructurePayload* macroStructure = nullptr;
    SequencerTrackActivationHistoryPlan activation{};
    uint16_t capturedTrackMask = 0U;
    uint8_t targetActiveTrack = SequencerTrackBankState::TRACK_COUNT;
    bool ready = false;
    std::array<SequencerHistoryGraphPtr, SequencerTrackBankState::TRACK_COUNT>
        bankGraphs{};
    std::array<SequencerHistoryCcLanePtr, SequencerTrackBankState::TRACK_COUNT>
        bankCcLanes{};
    SequencerHistoryGraphPtr editorGraph{};
    SequencerHistoryCcLanePtr editorCcLanes{};

    SequencerPreparedStructureHistoryReplay();
    ~SequencerPreparedStructureHistoryReplay();
    SequencerPreparedStructureHistoryReplay(
        const SequencerPreparedStructureHistoryReplay&) = delete;
    SequencerPreparedStructureHistoryReplay& operator=(
        const SequencerPreparedStructureHistoryReplay&) = delete;
    SequencerPreparedStructureHistoryReplay(
        SequencerPreparedStructureHistoryReplay&&) noexcept;
    SequencerPreparedStructureHistoryReplay& operator=(
        SequencerPreparedStructureHistoryReplay&&) noexcept;

    void reset();
    bool valid() const {
        return ready && entryIdentity != 0U && entry != nullptr &&
            targetSnapshot != nullptr && capturedTrackMask != 0U &&
            targetActiveTrack < SequencerTrackBankState::TRACK_COUNT;
    }
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(
    sizeof(SequencerHistoryTrackStructureSnapshot) == 13576U,
    "LOCK-P: ARM Structure snapshot ABI changed"
);
static_assert(
    sizeof(SequencerHistoryMacroTrackStructurePayload) == 30860U,
    "LOCK-P: ARM Macro Structure payload ABI changed"
);
static_assert(
    sizeof(SequencerHistoryTrackStructureChange) == 27192U,
    "LOCK-P: ARM Structure History transaction ABI changed"
);
static_assert(
    sizeof(SequencerPreparedStructureHistoryReplay) <= 256U,
    "prepared Structure replay handle exceeds its ARM frame contract"
);
#endif

uint16_t sequencerHistoryTrackBit(uint8_t trackIndex);
uint16_t sequencerHistorySanitizeTrackMask(uint16_t trackMask);
uint8_t sequencerHistoryEnabledTrackCount(uint16_t enabledMask);

bool captureHistoryStructureSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
);
bool reserveHistoryStructureSnapshotStorage(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
);
bool captureHistoryStructureSnapshotUsingReservedStorage(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
);
bool captureHistoryStructureSnapshotUsingReservedGraphs(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
);

SequencerHistoryTrackStructureChangePtr prepareHistoryStructureChangeBefore(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryDescriptor descriptor = {}
);
// The before.capturedTrackMask is the immutable union of every Track needed by
// both states, including any future active Track. Failed/partial reservations
// are discard-only; capture rejects an active Track outside that frozen union.
bool reservePreparedHistoryStructureAfter(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackStructureChange& change
);
bool capturePreparedHistoryStructureAfterUsingReservedStorage(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackStructureChange& change
);

// Constructs the final immutable After directly from Before. Preserved Tracks
// clone Graph then CC owners in ascending Track order; reset Tracks reproduce
// exactly SequencerPatternState::reset(), including its revision transitions,
// and retain no payload owner. A failed allocation leaves only a discardable
// detached After; Before and live state remain untouched.
bool buildHistoryStructureSnapshotAfterFromBefore(
    SequencerHistoryTrackStructureChange& change,
    uint16_t enabledMask,
    uint8_t activeTrack,
    uint8_t focusedStep,
    uint8_t page,
    uint16_t canonicalResetTrackMask = 0U
);

// Allocation-free byte/revision/payload revalidation. Pointer-owner identity
// is intentionally supplied by the transaction's scalar sidecar.
bool liveHistoryStructureSnapshotMatches(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot
);

bool prepareHistoryStructureReplayOwners(
    const SequencerHistoryTrackStructureSnapshot& snapshot,
    uint8_t liveActiveTrack,
    SequencerPreparedStructureHistoryReplay& out
);
void commitPreparedHistoryStructureReplayState(
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerPreparedStructureHistoryReplay& replay
) noexcept;

bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
);

bool captureMacroTrackStructureHistoryBefore(
    const core::state::macro::MacroPagesState& pages,
    uint16_t trackMask,
    SequencerHistoryTrackStructureChange& change,
    uint8_t affectedTrackIndex =
        SequencerHistoryMacroTrackStructurePayload::INVALID_AFFECTED_TRACK
);
bool captureMacroTrackStructureHistoryAfter(
    const core::state::macro::MacroPagesState& pages,
    SequencerHistoryTrackStructureChange& change
);
bool macroTrackStructureHistoryChanged(
    const SequencerHistoryTrackStructureChange& change
);
bool liveMacroTrackStructureMatches(
    const core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
);
bool validateMacroTrackStructureHistoryReplay(
    const core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
);
// Precondition: validateMacroTrackStructureHistoryReplay() succeeded and the
// payload/live source remained immutable. This durable-only commit allocates
// nothing, cannot report a recoverable failure and deliberately leaves Macro
// cache/runtime publication to the coordinated caller's final boundary.
void commitMacroTrackStructureHistoryReplay(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
);
// Preconditions: the coordinated transaction already proved that live state
// matches `before`, admitted this normalized payload, and crossed its final
// no-fail boundary. A null afterControl means byte-identical control state;
// a non-null afterControl is known distinct and is installed without another
// full-domain comparison.
void commitAdmittedMacroTrackStructureHistoryAfter(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload
) noexcept;
bool applyMacroTrackStructureHistory(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
);

}  // namespace core::state::sequencer

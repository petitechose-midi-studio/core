#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::state::sequencer {

struct SequencerHistoryMacroTrackStructurePayload {
    uint16_t capturedTrackMask = 0U;
    bool afterCaptured = false;
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

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(
    sizeof(SequencerHistoryTrackStructureChange) == 27192U,
    "LOCK-P: ARM Structure History transaction ABI changed"
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

bool applyHistoryStructureSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot
);

bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
);

bool captureMacroTrackStructureHistoryBefore(
    const core::state::macro::MacroPagesState& pages,
    uint16_t trackMask,
    SequencerHistoryTrackStructureChange& change
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
bool applyMacroTrackStructureHistory(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
);

}  // namespace core::state::sequencer

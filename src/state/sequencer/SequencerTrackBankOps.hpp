#pragma once

#include <cstdint>

#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

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

// Installs a fully decoded bank by transferring graph ownership. The staged
// objects are consumed and no PSRAM allocation occurs during the commit.
void installTrackBankState(
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerTrackBankState& stagedBank,
    SequencerState& stagedActive
);

}  // namespace core::state::sequencer

#pragma once

#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

struct SequencerTrackSelectionPasteTargets {
    uint16_t targetMask = 0;
    uint8_t firstTarget =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    bool hasTargets() const { return targetMask != 0; }
};

core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard>
captureTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
);

SequencerTrackSelectionPasteTargets buildTrackSelectionPasteTargets(
    const core::state::SequencerTrackSelectionClipboard& clipboard,
    uint8_t cursorTrack
);

void pasteTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerTrackSelectionClipboard& clipboard,
    uint8_t cursorTrack,
    uint8_t previousActiveTrack
);

}  // namespace core::handler

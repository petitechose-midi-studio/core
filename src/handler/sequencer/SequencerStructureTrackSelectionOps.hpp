#pragma once

#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

struct SequencerTrackSelectionPasteTargets {
    uint16_t targetMask = 0;
    uint8_t firstTarget =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    bool hasTargets() const { return targetMask != 0; }
};

uint16_t activeTrackSelectionMask(
    uint16_t selectedMask,
    uint16_t enabledMask
);

bool toggleSelectedSequencerStructureTrackMute(
    core::state::sequencer::SequencerTrackBankState& tracks,
    uint16_t selectedMask
);

core::state::shared::MaskMutation removeSelectedSequencerStructureTracks(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t activeTrack
);

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

[[nodiscard]] bool pasteTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerTrackSelectionClipboard& clipboard,
    uint8_t cursorTrack,
    uint8_t previousActiveTrack
);

}  // namespace core::handler

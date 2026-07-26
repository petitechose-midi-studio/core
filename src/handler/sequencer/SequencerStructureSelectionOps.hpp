#pragma once

#include <cstdint>

#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

uint16_t activeTrackSelectionMask(
    uint16_t selectedMask,
    uint16_t enabledMask
);

core::state::shared::MaskMutation removeSelectedStructureTracks(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t activeTrack
);

uint16_t activeContentPageSelectionMask(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
);

bool resetSelectedActiveContentPages(
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask,
    StepResetDepth depth
);

bool removeSelectedRootPages(
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
);

}  // namespace core::handler

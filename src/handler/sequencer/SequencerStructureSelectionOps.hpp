#pragma once

#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/StructureClipboardPastePlan.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

uint16_t activeTrackSelectionMask(
    uint16_t selectedMask,
    uint16_t enabledMask
);

core::state::shared::MaskMutation deleteSelectedStructureTracks(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t activeTrack
);

uint16_t activeContentPageSelectionMask(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
);

core::app::ExtmemUniquePtr<
    core::state::SequencerTrackSelectionClipboard
> captureTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::macro::MacroPagesState& pages,
    uint16_t selectedMask
);

bool capturePageSelectionClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask,
    core::state::SequencerPageSelectionClipboard& clipboard
);

core::state::SequencerPageSelectionPastePlan
buildPageSelectionPastePlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    uint8_t cursorPage
);

}  // namespace core::handler

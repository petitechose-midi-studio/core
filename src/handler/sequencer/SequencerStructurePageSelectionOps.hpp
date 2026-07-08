#pragma once

#include <cstdint>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

bool capturePageSelectionClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask,
    core::state::SequencerPageSelectionClipboard& clipboard
);

core::state::SequencerPageSelectionPastePlan buildPageSelectionPastePlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    uint8_t cursorPage
);

void pastePageSelectionClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    const core::state::SequencerPageSelectionPastePlan& plan
);

}  // namespace core::handler

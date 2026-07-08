#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

bool capturePageClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t page,
    core::state::SequencerPageClipboard& clipboard
);

void pastePageClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerPageClipboard& clipboard,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetPage
);

}  // namespace core::handler

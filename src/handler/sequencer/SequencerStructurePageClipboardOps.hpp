#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

bool capturePageClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t page,
    core::state::SequencerPageClipboard& clipboard
);

}  // namespace core::handler

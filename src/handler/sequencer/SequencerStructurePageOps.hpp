#pragma once

#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

uint8_t sequencerStructurePageTarget(
    const core::state::sequencer::SequencerState& sequencer
);

bool createSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
);

void syncSequencerPagePreviewToVisible(
    core::state::sequencer::SequencerState& sequencer,
    bool syncFocusedStep
);

bool clearCurrentSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
);

bool removeCurrentSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
);

}  // namespace core::handler

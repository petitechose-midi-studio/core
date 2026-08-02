#pragma once

#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

void syncSequencerPagePreviewToVisible(
    core::state::sequencer::SequencerState& sequencer,
    bool syncFocusedStep
);

}  // namespace core::handler

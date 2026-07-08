#pragma once

#include <cstdint>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state {
struct StructureClipboardState;
}

namespace core::handler::sequencer::step_context_row_workflow {

bool focusedRowIsContext(const core::state::sequencer::SequencerState& sequencer);
bool focusedContextHasChild(const core::state::sequencer::SequencerState& sequencer,
                            uint8_t step);
bool canPasteFocusedContextChild(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::StructureClipboardState& clipboard
);

core::state::sequencer::StepContentOpenResult openOrCreateFocusedContextChild(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step
);
bool copyFocusedContextChildToClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::StructureClipboardState& clipboard
);
bool clearFocusedContextChild(core::state::sequencer::SequencerState& sequencer,
                              uint8_t step);
bool pasteFocusedContextChildFromClipboard(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::StructureClipboardState& clipboard
);

}  // namespace core::handler::sequencer::step_context_row_workflow

#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

enum class StepResetDepth : uint8_t {
    Shallow,
    Deep,
};

bool captureFocusedStepClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    uint8_t step,
    core::state::SequencerStepsClipboard& clipboard
);

bool captureStepSelectionClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const oc::note::sequencer::StepBitMask128& selectedMask,
    core::state::SequencerStepsClipboard& clipboard
);

}  // namespace core::handler

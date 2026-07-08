#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

enum class StepResetDepth : uint8_t {
    Shallow,
    Deep,
};

bool selectedStepRange(
    const oc::note::sequencer::StepBitMask128& mask,
    uint8_t activeLength,
    uint8_t& outFirst,
    uint8_t& outLast
);

oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks
);

bool resetActiveContentStep(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    StepResetDepth depth
);

bool appendStepClipboardEntry(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    uint8_t firstStep,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    core::state::SequencerStepsClipboard& clipboard
);

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

bool writeRootStepFromClipboardEntry(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepClipboardEntry& entry,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetStep
);

bool writeChildStepFromClipboardEntry(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepClipboardEntry& entry,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetStep
);

}  // namespace core::handler

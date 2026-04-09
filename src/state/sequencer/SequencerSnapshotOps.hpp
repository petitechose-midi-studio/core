#pragma once

#include <cstdint>

#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::state::sequencer {

oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length);

void captureSnapshot(const SequencerState& source, SequencerPatternSnapshot& out);

void applySnapshot(SequencerState& target, const SequencerPatternSnapshot& snapshot);

void mergeSnapshotIntoCurrent(SequencerState& target, const SequencerPatternSnapshot& snapshot);

bool duplicatePatternForward(SequencerState& target);

bool rotatePattern(SequencerState& target, int offsetSteps);

bool clearStepRange(SequencerState& target, uint8_t startStep, uint8_t endStep);

bool copyStepRangeToClipboard(
    const SequencerState& source,
    uint8_t startStep,
    uint8_t endStep,
    SequencerRangeClipboard& clipboard
);

bool pasteClipboardRange(
    SequencerState& target,
    uint8_t targetStart,
    const SequencerRangeClipboard& clipboard
);

}  // namespace core::state::sequencer

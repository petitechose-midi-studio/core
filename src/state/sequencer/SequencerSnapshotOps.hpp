#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

struct SequencerPatternSnapshot {
    uint8_t length = oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED;
    uint64_t enabledMask = 0;
    std::array<uint8_t, SequencerState::MAX_STEPS> note{};
    std::array<uint8_t, SequencerState::MAX_STEPS> velocity{};
    std::array<uint16_t, SequencerState::MAX_STEPS> gate{};
    std::array<int8_t, SequencerState::MAX_STEPS> nudge{};
    std::array<uint8_t, SequencerState::MAX_STEPS> probability{};
};

uint64_t lengthMask(uint8_t length);

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

#include "state/sequencer/SequencerSnapshotOps.hpp"

#include <algorithm>

namespace core::state::sequencer {

namespace {

uint8_t sanitizeSequencerLength(uint8_t length) {
    if (length == 0 || length > SequencerState::MAX_STEPS) {
        return oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    }
    return length;
}

uint8_t sanitizeStepsPerBeat(uint8_t spb) {
    if (spb == 0) {
        return oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    }
    return spb;
}

uint8_t sanitizeMidiChannel(uint8_t channel) {
    return (channel > 15U)
               ? oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED
               : channel;
}

uint8_t sanitizeMidi7(uint8_t value) {
    return (value > 127U) ? 127U : value;
}

}  // namespace

uint64_t lengthMask(uint8_t length) {
    if (length == 0) return 0;
    if (length >= SequencerState::MAX_STEPS) return ~uint64_t{0};
    return (uint64_t{1} << length) - uint64_t{1};
}

void captureSnapshot(const SequencerState& source, SequencerPatternSnapshot& out) {
    out.length = sanitizeSequencerLength(source.length.get());
    out.stepsPerBeat = sanitizeStepsPerBeat(source.stepsPerBeat.get());
    out.midiChannel = sanitizeMidiChannel(source.midiChannel.get());
    out.enabledMask = source.enabledMask.get();

    for (uint8_t i = 0; i < SequencerState::MAX_STEPS; ++i) {
        out.note[i] = sanitizeMidi7(source.note[i]);
        out.velocity[i] = sanitizeMidi7(source.velocity[i]);
        out.gate[i] = SequencerState::clampGatePercent(source.gate[i]);
        out.nudge[i] = source.nudge[i];
        out.probability[i] = SequencerState::clampProbability(source.probability[i]);
    }
}

void applySnapshot(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
    const uint8_t length = sanitizeSequencerLength(snapshot.length);
    const uint8_t focusedBefore = target.focusedStep.get();

    target.length.set(length);
    target.stepsPerBeat.set(sanitizeStepsPerBeat(snapshot.stepsPerBeat));
    target.midiChannel.set(sanitizeMidiChannel(snapshot.midiChannel));
    target.enabledMask.set(snapshot.enabledMask & lengthMask(length));

    for (uint8_t i = 0; i < SequencerState::MAX_STEPS; ++i) {
        target.note[i] = sanitizeMidi7(snapshot.note[i]);
        target.velocity[i] = sanitizeMidi7(snapshot.velocity[i]);
        target.gate[i] = SequencerState::clampGatePercent(snapshot.gate[i]);
        target.nudge[i] = snapshot.nudge[i];
        target.probability[i] = SequencerState::clampProbability(snapshot.probability[i]);
    }

    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.bumpStepDataRevision();
}

void mergeSnapshotIntoCurrent(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
    const uint8_t focusedBefore = target.focusedStep.get();

    const uint8_t currentLength = sanitizeSequencerLength(target.length.get());
    const uint8_t incomingLength = sanitizeSequencerLength(snapshot.length);
    const uint8_t mergedLength = std::max(currentLength, incomingLength);

    target.length.set(mergedLength);

    uint64_t mergedMask = target.enabledMask.get() & lengthMask(mergedLength);
    const uint64_t incomingMask = snapshot.enabledMask & lengthMask(incomingLength);

    for (uint8_t i = 0; i < incomingLength; ++i) {
        const uint64_t bit = uint64_t{1} << i;
        if ((incomingMask & bit) == 0) continue;

        target.note[i] = sanitizeMidi7(snapshot.note[i]);
        target.velocity[i] = sanitizeMidi7(snapshot.velocity[i]);
        target.gate[i] = SequencerState::clampGatePercent(snapshot.gate[i]);
        target.nudge[i] = snapshot.nudge[i];
        target.probability[i] = SequencerState::clampProbability(snapshot.probability[i]);
        mergedMask |= bit;
    }

    target.enabledMask.set(mergedMask);

    const uint8_t focused =
        (focusedBefore >= mergedLength) ? static_cast<uint8_t>(mergedLength - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.bumpStepDataRevision();
}

}  // namespace core::state::sequencer

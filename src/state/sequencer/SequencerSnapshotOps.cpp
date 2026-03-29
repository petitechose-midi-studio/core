#include "state/sequencer/SequencerSnapshotOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
namespace core::state::sequencer {

namespace {

FLASHMEM uint8_t sanitizeSequencerLength(uint8_t length) {
    if (length == 0 || length > SequencerState::MAX_STEPS) {
        return oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    }
    return length;
}

FLASHMEM uint8_t sanitizeStepsPerBeat(uint8_t spb) {
    if (spb == 0) {
        return oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    }
    return spb;
}

FLASHMEM uint8_t sanitizeMidiChannel(uint8_t channel) {
    return (channel > 15U)
               ? oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED
               : channel;
}

FLASHMEM uint8_t sanitizeMidi7(uint8_t value) {
    return (value > 127U) ? 127U : value;
}

}  // namespace

FLASHMEM uint64_t lengthMask(uint8_t length) {
    if (length == 0) return 0;
    if (length >= SequencerState::MAX_STEPS) return ~uint64_t{0};
    return (uint64_t{1} << length) - uint64_t{1};
}

FLASHMEM void captureSnapshot(const SequencerState& source, SequencerPatternSnapshot& out) {
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

FLASHMEM void applySnapshot(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
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

FLASHMEM void mergeSnapshotIntoCurrent(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
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

FLASHMEM bool duplicatePatternForward(SequencerState& target) {
    const uint8_t len = target.length.get();
    if (len == 0 || len >= SequencerState::MAX_STEPS) return false;

    const uint8_t targetStart = len;
    const uint8_t copyCount = static_cast<uint8_t>(
        std::min<uint16_t>(len, static_cast<uint16_t>(SequencerState::MAX_STEPS - targetStart))
    );
    if (copyCount == 0) return false;

    uint64_t mask = target.enabledMask.get();
    bool dataChanged = false;

    for (uint8_t i = 0; i < copyCount; ++i) {
        const uint8_t src = i;
        const uint8_t dst = static_cast<uint8_t>(targetStart + i);

        if (target.note[dst] != target.note[src] ||
            target.velocity[dst] != target.velocity[src] ||
            target.gate[dst] != target.gate[src] ||
            target.nudge[dst] != target.nudge[src] ||
            target.probability[dst] != target.probability[src]) {
            dataChanged = true;
        }

        target.note[dst] = target.note[src];
        target.velocity[dst] = target.velocity[src];
        target.gate[dst] = target.gate[src];
        target.nudge[dst] = target.nudge[src];
        target.probability[dst] = target.probability[src];

        const uint64_t dstBit = (uint64_t{1} << dst);
        const bool srcEnabled = (mask & (uint64_t{1} << src)) != 0;
        const bool dstEnabledBefore = (mask & dstBit) != 0;
        if (srcEnabled != dstEnabledBefore) {
            dataChanged = true;
        }

        if (srcEnabled) mask |= dstBit;
        else mask &= ~dstBit;
    }

    target.enabledMask.set(mask);

    const uint8_t requiredLength = static_cast<uint8_t>(targetStart + copyCount);
    if (requiredLength > len) {
        target.length.set(requiredLength);
    }

    target.page.set(target.pageForStep(targetStart));
    target.focusedStep.set(targetStart);

    if (dataChanged) {
        target.bumpStepDataRevision();
    }

    return true;
}

FLASHMEM bool rotatePattern(SequencerState& target, int offsetSteps) {
    const uint8_t len = target.length.get();
    if (len <= 1) return false;

    int normalizedOffset = offsetSteps % static_cast<int>(len);
    if (normalizedOffset < 0) {
        normalizedOffset += len;
    }
    if (normalizedOffset == 0) return false;

    std::array<uint8_t, SequencerState::MAX_STEPS> nextNote{};
    std::array<uint8_t, SequencerState::MAX_STEPS> nextVelocity{};
    std::array<uint16_t, SequencerState::MAX_STEPS> nextGate{};
    std::array<int8_t, SequencerState::MAX_STEPS> nextNudge{};
    std::array<uint8_t, SequencerState::MAX_STEPS> nextProbability{};
    const uint64_t activeMask =
        (len >= SequencerState::MAX_STEPS) ? ~uint64_t{0} : ((uint64_t{1} << len) - uint64_t{1});
    const uint64_t sourceMask = target.enabledMask.get();
    uint64_t nextMask = sourceMask & ~activeMask;

    for (uint8_t i = 0; i < len; ++i) {
        const uint8_t dst = static_cast<uint8_t>((i + normalizedOffset) % len);
        nextNote[dst] = target.note[i];
        nextVelocity[dst] = target.velocity[i];
        nextGate[dst] = target.gate[i];
        nextNudge[dst] = target.nudge[i];
        nextProbability[dst] = target.probability[i];

        if ((sourceMask & (uint64_t{1} << i)) != 0) {
            nextMask |= (uint64_t{1} << dst);
        }
    }

    for (uint8_t i = 0; i < len; ++i) {
        target.note[i] = nextNote[i];
        target.velocity[i] = nextVelocity[i];
        target.gate[i] = nextGate[i];
        target.nudge[i] = nextNudge[i];
        target.probability[i] = nextProbability[i];
    }

    target.enabledMask.set(nextMask);
    target.bumpStepDataRevision();
    return true;
}

FLASHMEM bool clearStepRange(SequencerState& target, uint8_t startStep, uint8_t endStep) {
    const uint8_t len = target.length.get();
    if (len == 0) return false;

    const uint8_t start = static_cast<uint8_t>(std::min(startStep, endStep));
    const uint8_t end = static_cast<uint8_t>(std::max(startStep, endStep));
    if (start >= len || start >= SequencerState::MAX_STEPS) return false;

    const uint8_t clampedEnd = static_cast<uint8_t>(std::min<uint16_t>(end, len - 1));
    uint64_t mask = target.enabledMask.get();
    bool dataChanged = false;
    bool maskChanged = false;

    for (uint8_t step = start; step <= clampedEnd; ++step) {
        const uint64_t bit = (uint64_t{1} << step);
        if ((mask & bit) != 0) {
            mask &= ~bit;
            maskChanged = true;
        }

        if (target.note[step] != SequencerState::DEFAULT_NOTE ||
            target.velocity[step] != SequencerState::DEFAULT_VELOCITY ||
            target.gate[step] != SequencerState::DEFAULT_GATE_PERCENT ||
            target.nudge[step] != 0 ||
            target.probability[step] != SequencerState::DEFAULT_PROBABILITY) {
            target.note[step] = SequencerState::DEFAULT_NOTE;
            target.velocity[step] = SequencerState::DEFAULT_VELOCITY;
            target.gate[step] = SequencerState::DEFAULT_GATE_PERCENT;
            target.nudge[step] = 0;
            target.probability[step] = SequencerState::DEFAULT_PROBABILITY;
            dataChanged = true;
        }
    }

    if (maskChanged) {
        target.enabledMask.set(mask);
    }

    target.focusedStep.set(start);
    target.page.set(target.pageForStep(start));

    if (dataChanged || maskChanged) {
        target.bumpStepDataRevision();
    }

    return dataChanged || maskChanged;
}

FLASHMEM bool copyStepRangeToClipboard(
    const SequencerState& source,
    uint8_t startStep,
    uint8_t endStep,
    SequencerRangeClipboard& clipboard
) {
    clipboard.reset();

    const uint8_t len = source.length.get();
    if (len == 0) return false;

    const uint8_t start = static_cast<uint8_t>(std::min(startStep, endStep));
    const uint8_t end = static_cast<uint8_t>(std::max(startStep, endStep));
    if (start >= len || start >= SequencerState::MAX_STEPS) return false;

    const uint8_t clampedEnd = static_cast<uint8_t>(std::min<uint16_t>(end, len - 1));
    const uint8_t count = static_cast<uint8_t>((clampedEnd - start) + 1);
    if (count == 0) return false;

    uint64_t relativeEnabledMask = 0;
    const uint64_t mask = source.enabledMask.get();

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t step = static_cast<uint8_t>(start + i);
        clipboard.note[i] = source.note[step];
        clipboard.velocity[i] = source.velocity[step];
        clipboard.gate[i] = source.gate[step];
        clipboard.nudge[i] = source.nudge[step];
        clipboard.probability[i] = source.probability[step];

        if ((mask & (uint64_t{1} << step)) != 0) {
            relativeEnabledMask |= (uint64_t{1} << i);
        }
    }

    clipboard.count = count;
    clipboard.enabledMask = relativeEnabledMask;
    clipboard.valid = true;
    return true;
}

FLASHMEM bool pasteClipboardRange(
    SequencerState& target,
    uint8_t targetStart,
    const SequencerRangeClipboard& clipboard
) {
    if (!clipboard.valid || clipboard.count == 0) return false;
    if (targetStart >= SequencerState::MAX_STEPS) return false;

    const uint8_t maxCount = static_cast<uint8_t>(SequencerState::MAX_STEPS - targetStart);
    const uint8_t copyCount =
        static_cast<uint8_t>(std::min<uint16_t>(clipboard.count, maxCount));
    if (copyCount == 0) return false;

    uint64_t mask = target.enabledMask.get();
    bool dataChanged = false;

    for (uint8_t i = 0; i < copyCount; ++i) {
        const uint8_t step = static_cast<uint8_t>(targetStart + i);

        if (target.note[step] != clipboard.note[i] ||
            target.velocity[step] != clipboard.velocity[i] ||
            target.gate[step] != clipboard.gate[i] ||
            target.nudge[step] != clipboard.nudge[i] ||
            target.probability[step] != clipboard.probability[i]) {
            dataChanged = true;
        }

        target.note[step] = clipboard.note[i];
        target.velocity[step] = clipboard.velocity[i];
        target.gate[step] = clipboard.gate[i];
        target.nudge[step] = clipboard.nudge[i];
        target.probability[step] = clipboard.probability[i];

        const uint64_t bit = (uint64_t{1} << step);
        const bool enabled = clipboard.isEnabled(i);
        const bool wasEnabled = (mask & bit) != 0;
        if (enabled != wasEnabled) {
            dataChanged = true;
        }

        if (enabled) mask |= bit;
        else mask &= ~bit;
    }

    target.enabledMask.set(mask);

    const uint8_t requiredLength = static_cast<uint8_t>(targetStart + copyCount);
    if (requiredLength > target.length.get()) {
        target.length.set(requiredLength);
    }

    target.focusedStep.set(targetStart);
    target.page.set(target.pageForStep(targetStart));

    if (dataChanged) {
        target.bumpStepDataRevision();
    }

    return true;
}

}  // namespace core::state::sequencer

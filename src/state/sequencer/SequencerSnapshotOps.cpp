#include "state/sequencer/SequencerSnapshotOps.hpp"

#include <algorithm>
#include <array>

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

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings sanitizeScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    return settings;
}

struct StepPayload {
    uint8_t note = SequencerState::DEFAULT_NOTE;
    uint8_t velocity = SequencerState::DEFAULT_VELOCITY;
    uint16_t gate = SequencerState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
};

struct PagePayload {
    uint8_t count = 0;
    std::array<StepPayload, SequencerState::STEPS_PER_PAGE> steps{};
    uint8_t enabledMask = 0;
};

FLASHMEM StepPayload defaultStep() {
    return {};
}

FLASHMEM StepPayload readStep(const SequencerState& source, uint8_t step) {
    return {
        source.note[step],
        source.velocity[step],
        source.gate[step],
        source.nudge[step],
        source.probability[step],
    };
}

FLASHMEM PagePayload readPage(const SequencerState& source, uint8_t page) {
    PagePayload payload{};
    const uint8_t len = source.length.get();
    const uint8_t start = static_cast<uint8_t>(page * SequencerState::STEPS_PER_PAGE);
    if (start >= len || start >= SequencerState::MAX_STEPS) {
        return payload;
    }

    payload.count = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::STEPS_PER_PAGE,
        static_cast<uint16_t>(len - start)
    ));

    for (uint8_t i = 0; i < SequencerState::STEPS_PER_PAGE; ++i) {
        const uint8_t step = static_cast<uint8_t>(start + i);
        payload.steps[i] = (i < payload.count) ? readStep(source, step) : defaultStep();
        if (i < payload.count && source.isEnabled(step)) {
            payload.enabledMask = static_cast<uint8_t>(payload.enabledMask | (1U << i));
        }
    }

    return payload;
}

FLASHMEM StepPayload readSanitizedStep(const SequencerState& source, uint8_t step) {
    return {
        sanitizeMidi7(source.note[step]),
        sanitizeMidi7(source.velocity[step]),
        SequencerState::clampGatePercent(source.gate[step]),
        source.nudge[step],
        SequencerState::clampProbability(source.probability[step]),
    };
}

FLASHMEM StepPayload readSanitizedStep(const SequencerPatternSnapshot& source, uint8_t step) {
    return {
        sanitizeMidi7(source.note[step]),
        sanitizeMidi7(source.velocity[step]),
        SequencerState::clampGatePercent(source.gate[step]),
        source.nudge[step],
        SequencerState::clampProbability(source.probability[step]),
    };
}

FLASHMEM bool sameStep(const StepPayload& lhs, const StepPayload& rhs) {
    return lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity &&
           lhs.gate == rhs.gate &&
           lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

FLASHMEM void writeStep(SequencerState& target, uint8_t step, const StepPayload& payload) {
    target.note[step] = payload.note;
    target.velocity[step] = payload.velocity;
    target.gate[step] = payload.gate;
    target.nudge[step] = payload.nudge;
    target.probability[step] = payload.probability;
}

FLASHMEM void writeStep(SequencerPatternSnapshot& target, uint8_t step, const StepPayload& payload) {
    target.note[step] = payload.note;
    target.velocity[step] = payload.velocity;
    target.gate[step] = payload.gate;
    target.nudge[step] = payload.nudge;
    target.probability[step] = payload.probability;
}

}  // namespace

FLASHMEM oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length) {
    return oc::note::sequencer::StepBitMask128::prefixMask(length);
}

FLASHMEM void captureSnapshot(const SequencerState& source, SequencerPatternSnapshot& out) {
    out.length = sanitizeSequencerLength(source.length.get());
    out.stepsPerBeat = sanitizeStepsPerBeat(source.stepsPerBeat.get());
    out.midiChannel = sanitizeMidiChannel(source.midiChannel.get());
    out.enabledMask = source.enabledMask.get();
    out.stepDataRevision = source.stepDataRevision.get();
    out.patternVariationRevision = source.patternVariationRevision.get();
    out.patternScaleRevision = source.patternScaleRevision.get();
    out.variationRanges = source.variationRanges;
    out.variationRanges.clamp();
    out.scalePolicy = source.scalePolicy;
    out.scaleOverride = sanitizeScaleSettings(source.scaleOverride);
    out.pitchEditMode = source.pitchEditMode;
    out.effectiveScaleSettings = resolveEffectiveScaleSettings(
        {},
        out.scalePolicy,
        out.scaleOverride
    );

    for (uint8_t i = 0; i < SequencerState::MAX_STEPS; ++i) {
        writeStep(out, i, readSanitizedStep(source, i));
    }
}

FLASHMEM void applySnapshot(SequencerState& target, const SequencerPatternSnapshot& snapshot) {
    const uint8_t length = sanitizeSequencerLength(snapshot.length);
    const uint8_t focusedBefore = target.focusedStep.get();

    target.length.set(length);
    target.stepsPerBeat.set(sanitizeStepsPerBeat(snapshot.stepsPerBeat));
    target.midiChannel.set(sanitizeMidiChannel(snapshot.midiChannel));
    target.enabledMask.set(snapshot.enabledMask & lengthMask(length));
    target.setPatternVariationRanges(snapshot.variationRanges);
    target.setPatternScalePolicy(snapshot.scalePolicy);
    target.setPatternScaleOverride(snapshot.scaleOverride);
    target.setPitchEditMode(snapshot.pitchEditMode);

    for (uint8_t i = 0; i < SequencerState::MAX_STEPS; ++i) {
        writeStep(target, i, readSanitizedStep(snapshot, i));
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

    auto mergedMask = target.enabledMask.get() & lengthMask(mergedLength);
    const auto incomingMask = snapshot.enabledMask & lengthMask(incomingLength);

    for (uint8_t i = 0; i < incomingLength; ++i) {
        if (!incomingMask.test(i)) continue;

        writeStep(target, i, readSanitizedStep(snapshot, i));
        mergedMask.setBit(i, true);
    }

    target.enabledMask.set(mergedMask);
    target.setPatternVariationRanges(snapshot.variationRanges);
    target.setPatternScalePolicy(snapshot.scalePolicy);
    target.setPatternScaleOverride(snapshot.scaleOverride);
    target.setPitchEditMode(snapshot.pitchEditMode);

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

    auto mask = target.enabledMask.get();
    bool dataChanged = false;

    for (uint8_t i = 0; i < copyCount; ++i) {
        const uint8_t src = i;
        const uint8_t dst = static_cast<uint8_t>(targetStart + i);
        const StepPayload sourceStep = readStep(target, src);

        if (!sameStep(readStep(target, dst), sourceStep)) {
            dataChanged = true;
        }

        writeStep(target, dst, sourceStep);

        const bool srcEnabled = mask.test(src);
        const bool dstEnabledBefore = mask.test(dst);
        if (srcEnabled != dstEnabledBefore) {
            dataChanged = true;
        }

        mask.setBit(dst, srcEnabled);
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

    std::array<StepPayload, SequencerState::MAX_STEPS> nextSteps{};
    const auto activeMask = lengthMask(len);
    const auto sourceMask = target.enabledMask.get();
    auto nextMask = sourceMask & ~activeMask;

    for (uint8_t i = 0; i < len; ++i) {
        const uint8_t dst = static_cast<uint8_t>((i + normalizedOffset) % len);
        nextSteps[dst] = readStep(target, i);

        if (sourceMask.test(i)) {
            nextMask.setBit(dst, true);
        }
    }

    for (uint8_t i = 0; i < len; ++i) {
        writeStep(target, i, nextSteps[i]);
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
    auto mask = target.enabledMask.get();
    bool dataChanged = false;
    bool maskChanged = false;

    for (uint8_t step = start; step <= clampedEnd; ++step) {
        if (mask.test(step)) {
            mask.setBit(step, false);
            maskChanged = true;
        }

        if (!sameStep(readStep(target, step), defaultStep())) {
            writeStep(target, step, defaultStep());
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

FLASHMEM bool appendPage(SequencerState& target) {
    const uint8_t len = target.length.get();
    const uint8_t pageCount = target.activePageCount();
    if (len == 0 || pageCount >= SequencerState::PAGE_COUNT) return false;

    const uint8_t newLength = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS,
        static_cast<uint16_t>(len) + SequencerState::STEPS_PER_PAGE
    ));
    if (newLength <= len) return false;

    auto mask = target.enabledMask.get();
    for (uint8_t step = len; step < newLength; ++step) {
        writeStep(target, step, defaultStep());
        mask.setBit(step, false);
    }

    target.length.set(newLength);
    target.enabledMask.set(mask & lengthMask(newLength));

    const uint8_t newPage = pageCount;
    const uint8_t focused = static_cast<uint8_t>(newPage * SequencerState::STEPS_PER_PAGE);
    target.focusedStep.set(focused);
    target.page.set(newPage);
    target.bumpStepDataRevision();
    return true;
}

FLASHMEM bool insertPage(SequencerState& target, uint8_t pageIndex) {
    const uint8_t len = target.length.get();
    const uint8_t pageCount = target.activePageCount();
    if (len == 0 || pageCount >= SequencerState::PAGE_COUNT || pageIndex > pageCount) {
        return false;
    }

    if (pageIndex == pageCount) {
        return appendPage(target);
    }

    const uint8_t insertStart = static_cast<uint8_t>(pageIndex * SequencerState::STEPS_PER_PAGE);
    if (insertStart > len) return false;

    const uint8_t newLength = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS,
        static_cast<uint16_t>(len) + SequencerState::STEPS_PER_PAGE
    ));
    if (newLength <= len) return false;

    auto mask = target.enabledMask.get();

    for (int dst = static_cast<int>(newLength) - 1;
         dst >= static_cast<int>(insertStart + SequencerState::STEPS_PER_PAGE);
         --dst) {
        const uint8_t dstIndex = static_cast<uint8_t>(dst);
        const uint8_t srcIndex =
            static_cast<uint8_t>(dst - static_cast<int>(SequencerState::STEPS_PER_PAGE));
        writeStep(target, dstIndex, readStep(target, srcIndex));
        mask.setBit(dstIndex, mask.test(srcIndex));
    }

    const uint8_t clearEnd = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(insertStart + SequencerState::STEPS_PER_PAGE - 1)
    ));
    for (uint8_t step = insertStart; step <= clearEnd; ++step) {
        writeStep(target, step, defaultStep());
        mask.setBit(step, false);
    }

    target.length.set(newLength);
    target.enabledMask.set(mask & lengthMask(newLength));
    target.focusedStep.set(insertStart);
    target.page.set(pageIndex);
    target.bumpStepDataRevision();
    return true;
}

FLASHMEM bool ensurePageExists(SequencerState& target, uint8_t pageIndex) {
    if (pageIndex >= SequencerState::PAGE_COUNT) return false;

    const uint8_t requiredLength = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::MAX_STEPS,
        static_cast<uint16_t>((static_cast<uint16_t>(pageIndex) + 1U) * SequencerState::STEPS_PER_PAGE)
    ));
    const uint8_t currentLength = target.length.get();
    if (requiredLength <= currentLength) {
        target.page.set(pageIndex);
        target.focusedStep.set(static_cast<uint8_t>(pageIndex * SequencerState::STEPS_PER_PAGE));
        return true;
    }

    auto mask = target.enabledMask.get();
    for (uint8_t step = currentLength; step < requiredLength; ++step) {
        writeStep(target, step, defaultStep());
        mask.setBit(step, false);
    }

    target.length.set(requiredLength);
    target.enabledMask.set(mask & lengthMask(requiredLength));
    target.page.set(pageIndex);
    target.focusedStep.set(static_cast<uint8_t>(pageIndex * SequencerState::STEPS_PER_PAGE));
    target.bumpStepDataRevision();
    return true;
}

FLASHMEM bool removePage(SequencerState& target, uint8_t pageIndex) {
    const uint8_t len = target.length.get();
    if (len <= SequencerState::STEPS_PER_PAGE) return false;

    const uint8_t pageCount = target.activePageCount();
    if (pageCount <= 1 || pageIndex >= pageCount) return false;

    const uint8_t pageStart = static_cast<uint8_t>(pageIndex * SequencerState::STEPS_PER_PAGE);
    if (pageStart >= len) return false;

    const uint8_t deleteSpan = static_cast<uint8_t>(std::min<uint16_t>(
        SequencerState::STEPS_PER_PAGE,
        static_cast<uint16_t>(len - pageStart)
    ));
    const uint8_t newLength = static_cast<uint8_t>(len - deleteSpan);
    auto mask = target.enabledMask.get();

    for (uint8_t dst = pageStart; static_cast<uint16_t>(dst + deleteSpan) < len; ++dst) {
        const uint8_t src = static_cast<uint8_t>(dst + deleteSpan);
        writeStep(target, dst, readStep(target, src));
        mask.setBit(dst, mask.test(src));
    }

    for (uint8_t step = newLength; step < SequencerState::MAX_STEPS; ++step) {
        writeStep(target, step, defaultStep());
        mask.setBit(step, false);
    }

    target.length.set(newLength);
    target.enabledMask.set(mask & lengthMask(newLength));

    const uint8_t focused = static_cast<uint8_t>(std::min<uint16_t>(
        pageStart,
        static_cast<uint16_t>(newLength - 1U)
    ));
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.bumpStepDataRevision();
    return true;
}

FLASHMEM bool duplicatePagesFromPlan(SequencerState& target, const SequencerPageDuplicatePlan& plan) {
    if (!plan.hasEntries()) return false;

    std::array<PagePayload, SequencerState::PAGE_COUNT> sourcePages{};
    for (uint8_t i = 0; i < plan.entryCount; ++i) {
        const auto& entry = plan.entries[i];
        if (entry.sourcePage >= SequencerState::PAGE_COUNT) continue;
        sourcePages[entry.sourcePage] = readPage(target, entry.sourcePage);
    }

    auto mask = target.enabledMask.get();
    const uint8_t previousLength = target.length.get();
    uint8_t requiredLength = previousLength;
    std::array<bool, SequencerState::MAX_STEPS> writtenSteps{};
    bool executed = false;
    bool changed = false;

    for (uint8_t entryIndex = 0; entryIndex < plan.entryCount; ++entryIndex) {
        const auto& entry = plan.entries[entryIndex];
        if (entry.sourcePage >= SequencerState::PAGE_COUNT ||
            entry.destinationPage >= SequencerState::PAGE_COUNT) {
            continue;
        }
        const auto& sourcePage = sourcePages[entry.sourcePage];
        if (sourcePage.count == 0) continue;
        executed = true;

        const uint8_t destinationStart = static_cast<uint8_t>(
            entry.destinationPage * SequencerState::STEPS_PER_PAGE
        );
        if (destinationStart >= SequencerState::MAX_STEPS) continue;

        for (uint8_t i = 0; i < SequencerState::STEPS_PER_PAGE; ++i) {
            const uint8_t destinationStep = static_cast<uint8_t>(destinationStart + i);
            if (destinationStep >= SequencerState::MAX_STEPS) break;

            const bool sourceHasStep = i < sourcePage.count;
            const StepPayload nextStep = sourceHasStep ? sourcePage.steps[i] : defaultStep();
            const bool nextEnabled =
                sourceHasStep && ((sourcePage.enabledMask & static_cast<uint8_t>(1U << i)) != 0);

            if (!sameStep(readStep(target, destinationStep), nextStep) ||
                mask.test(destinationStep) != nextEnabled) {
                changed = true;
            }

            writeStep(target, destinationStep, nextStep);
            mask.setBit(destinationStep, nextEnabled);
            writtenSteps[destinationStep] = true;
        }

        const uint8_t entryLength = static_cast<uint8_t>(std::min<uint16_t>(
            SequencerState::MAX_STEPS,
            static_cast<uint16_t>(destinationStart + sourcePage.count)
        ));
        if (entryLength > requiredLength) {
            requiredLength = entryLength;
        }
    }

    if (requiredLength > previousLength) {
        for (uint8_t step = previousLength; step < requiredLength; ++step) {
            if (writtenSteps[step]) continue;
            if (!sameStep(readStep(target, step), defaultStep()) || mask.test(step)) {
                changed = true;
            }
            writeStep(target, step, defaultStep());
            mask.setBit(step, false);
        }
        target.length.set(requiredLength);
        changed = true;
    }

    if (!executed) return false;

    target.enabledMask.set(mask & lengthMask(target.length.get()));
    const uint8_t focusedPage = plan.entries[0].destinationPage;
    const uint8_t focusedStep = static_cast<uint8_t>(focusedPage * SequencerState::STEPS_PER_PAGE);
    target.page.set(focusedPage);
    target.focusedStep.set(focusedStep);

    if (changed) {
        target.bumpStepDataRevision();
    }

    return executed;
}

}  // namespace core::state::sequencer

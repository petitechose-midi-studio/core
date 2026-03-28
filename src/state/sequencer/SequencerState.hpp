#pragma once

/**
 * @file SequencerState.hpp
 * @brief Sequencer state for Core UI + v0 playback engine integration
 */

#include <cstdint>
#include <algorithm>

#include <oc/note/sequencer/StepSequencerState.hpp>
#include "SequencerUiState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

struct SequencerState : public oc::note::sequencer::StepSequencerState {
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr uint8_t MAX_STEPS = oc::note::sequencer::StepSequencerState::MAX_STEPS;
    static constexpr uint8_t PAGE_COUNT = (MAX_STEPS + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE;
    static constexpr uint16_t MAX_GATE_PERCENT =
        oc::note::sequencer::StepSequencerState::MAX_GATE_PERCENT;
    static constexpr uint8_t DEFAULT_PROBABILITY =
        oc::note::sequencer::StepSequencerState::DEFAULT_PROBABILITY;

    /// Visible page index [0..PAGE_COUNT-1]
    Signal<uint8_t, 8> page{0};

    /// Absolute focused step index [0..length-1]
    Signal<uint8_t, 6> focusedStep{0};

    /// Bumps when non-signal step arrays change (note/velocity/gate/nudge/probability)
    Signal<uint32_t> stepDataRevision{0};

    /// Active property edited by the 8 macro encoders in Sequencer view
    Signal<StepProperty, 6> activeStepProperty{StepProperty::NOTE};

    // UI state
    SequencerStepEditOverlayState stepEdit;
    SequencerStepPropertyInlineSelectorState stepPropertyInlineSelector;
    SequencerStepInlineFeedbackState stepInlineFeedback;
    SequencerPatternQuickControlsState patternQuickControls;
    SequencerRangeSelectionState rangeSelection;

    static uint8_t clampMidi7(uint8_t value) {
        return (value > 127U) ? 127U : value;
    }

    static uint16_t clampGatePercent(uint16_t value) {
        return (value > MAX_GATE_PERCENT) ? MAX_GATE_PERCENT : value;
    }

    static int8_t clampNudge(int value) {
        if (value < -50) return -50;
        if (value > 50) return 50;
        return static_cast<int8_t>(value);
    }

    static uint8_t clampProbability(uint8_t value) {
        return oc::note::sequencer::StepSequencerState::clampProbability(value);
    }

    void bumpStepDataRevision() {
        stepDataRevision.set(stepDataRevision.get() + 1);
    }

    bool setStepNoteAt(uint8_t step, uint8_t noteValue) {
        if (step >= MAX_STEPS) return false;
        const uint8_t clamped = clampMidi7(noteValue);
        if (note[step] == clamped) return false;
        note[step] = clamped;
        bumpStepDataRevision();
        return true;
    }

    bool setStepVelocityAt(uint8_t step, uint8_t velocityValue) {
        if (step >= MAX_STEPS) return false;
        const uint8_t clamped = clampMidi7(velocityValue);
        if (velocity[step] == clamped) return false;
        velocity[step] = clamped;
        bumpStepDataRevision();
        return true;
    }

    bool setStepGateAt(uint8_t step, uint16_t gatePercent) {
        if (step >= MAX_STEPS) return false;
        const uint16_t clamped = clampGatePercent(gatePercent);
        if (gate[step] == clamped) return false;
        gate[step] = clamped;
        bumpStepDataRevision();
        return true;
    }

    bool setStepNudgeAt(uint8_t step, int8_t nudgeValue) {
        if (step >= MAX_STEPS) return false;
        const int8_t clamped = clampNudge(nudgeValue);
        if (nudge[step] == clamped) return false;
        nudge[step] = clamped;
        bumpStepDataRevision();
        return true;
    }

    bool setStepProbabilityAt(uint8_t step, uint8_t probabilityValue) {
        if (step >= MAX_STEPS) return false;
        const uint8_t clamped = clampProbability(probabilityValue);
        if (probability[step] == clamped) return false;
        probability[step] = clamped;
        bumpStepDataRevision();
        return true;
    }

    bool setStepDataAt(uint8_t step, uint8_t noteValue, uint8_t velocityValue, uint16_t gatePercent) {
        if (step >= MAX_STEPS) return false;
        return setStepDataAt(step, noteValue, velocityValue, gatePercent, nudge[step], probability[step]);
    }

    bool setStepDataAt(
        uint8_t step,
        uint8_t noteValue,
        uint8_t velocityValue,
        uint16_t gatePercent,
        int8_t nudgeValue
    ) {
        if (step >= MAX_STEPS) return false;
        return setStepDataAt(
            step,
            noteValue,
            velocityValue,
            gatePercent,
            nudgeValue,
            probability[step]
        );
    }

    bool setStepDataAt(
        uint8_t step,
        uint8_t noteValue,
        uint8_t velocityValue,
        uint16_t gatePercent,
        int8_t nudgeValue,
        uint8_t probabilityValue
    ) {
        if (step >= MAX_STEPS) return false;
        const uint8_t clampedNote = clampMidi7(noteValue);
        const uint8_t clampedVelocity = clampMidi7(velocityValue);
        const uint16_t clampedGate = clampGatePercent(gatePercent);
        const int8_t clampedNudge = clampNudge(nudgeValue);
        const uint8_t clampedProbability = clampProbability(probabilityValue);

        if (note[step] == clampedNote &&
            velocity[step] == clampedVelocity &&
            gate[step] == clampedGate &&
            nudge[step] == clampedNudge &&
            probability[step] == clampedProbability) {
            return false;
        }

        note[step] = clampedNote;
        velocity[step] = clampedVelocity;
        gate[step] = clampedGate;
        nudge[step] = clampedNudge;
        probability[step] = clampedProbability;
        bumpStepDataRevision();
        return true;
    }

    bool duplicatePageForward(uint8_t sourcePage) {
        const uint8_t len = length.get();
        if (len == 0) return false;

        const uint8_t safePage = normalizePage(sourcePage);
        const uint8_t sourceStart = pageStartStep(safePage);
        if (sourceStart >= len || sourceStart >= MAX_STEPS) return false;

        const uint8_t targetStart = static_cast<uint8_t>(sourceStart + STEPS_PER_PAGE);
        if (targetStart >= MAX_STEPS) return false;

        const uint8_t sourceEndExclusive = static_cast<uint8_t>(
            std::min<uint16_t>(MAX_STEPS, sourceStart + STEPS_PER_PAGE)
        );
        const uint8_t sourceCount = static_cast<uint8_t>(
            std::min<uint16_t>(len, sourceEndExclusive) - sourceStart
        );
        if (sourceCount == 0) return false;

        const uint8_t targetEndExclusive = static_cast<uint8_t>(
            std::min<uint16_t>(MAX_STEPS, targetStart + sourceCount)
        );
        const uint8_t copyCount = static_cast<uint8_t>(targetEndExclusive - targetStart);
        if (copyCount == 0) return false;

        uint64_t mask = enabledMask.get();
        bool dataChanged = false;

        for (uint8_t i = 0; i < copyCount; ++i) {
            const uint8_t src = static_cast<uint8_t>(sourceStart + i);
            const uint8_t dst = static_cast<uint8_t>(targetStart + i);

            if (note[dst] != note[src] ||
                velocity[dst] != velocity[src] ||
                gate[dst] != gate[src] ||
                nudge[dst] != nudge[src] ||
                probability[dst] != probability[src]) {
                dataChanged = true;
            }

            note[dst] = note[src];
            velocity[dst] = velocity[src];
            gate[dst] = gate[src];
            nudge[dst] = nudge[src];
            probability[dst] = probability[src];

            const uint64_t dstBit = (1ULL << dst);
            const bool srcEnabled = (mask & (1ULL << src)) != 0;
            const bool dstEnabledBefore = (mask & dstBit) != 0;
            if (srcEnabled != dstEnabledBefore) {
                dataChanged = true;
            }

            if (srcEnabled) {
                mask |= dstBit;
            } else {
                mask &= ~dstBit;
            }
        }

        enabledMask.set(mask);

        const uint8_t requiredLength = static_cast<uint8_t>(targetStart + copyCount);
        if (requiredLength > len) {
            length.set(requiredLength);
        }

        page.set(pageForStep(targetStart));
        focusedStep.set(targetStart);

        if (dataChanged) {
            bumpStepDataRevision();
        }

        return true;
    }

    bool clearStepRange(uint8_t startStep, uint8_t endStep) {
        const uint8_t len = length.get();
        if (len == 0) return false;

        const uint8_t start = static_cast<uint8_t>(std::min(startStep, endStep));
        const uint8_t end = static_cast<uint8_t>(std::max(startStep, endStep));
        if (start >= len || start >= MAX_STEPS) return false;

        const uint8_t clampedEnd = static_cast<uint8_t>(std::min<uint16_t>(end, len - 1));
        uint64_t mask = enabledMask.get();
        bool dataChanged = false;
        bool maskChanged = false;

        for (uint8_t step = start; step <= clampedEnd; ++step) {
            const uint64_t bit = (1ULL << step);
            if ((mask & bit) != 0) {
                mask &= ~bit;
                maskChanged = true;
            }

            if (note[step] != DEFAULT_NOTE ||
                velocity[step] != DEFAULT_VELOCITY ||
                gate[step] != DEFAULT_GATE_PERCENT ||
                nudge[step] != 0 ||
                probability[step] != DEFAULT_PROBABILITY) {
                note[step] = DEFAULT_NOTE;
                velocity[step] = DEFAULT_VELOCITY;
                gate[step] = DEFAULT_GATE_PERCENT;
                nudge[step] = 0;
                probability[step] = DEFAULT_PROBABILITY;
                dataChanged = true;
            }
        }

        if (maskChanged) {
            enabledMask.set(mask);
        }

        focusedStep.set(start);
        page.set(pageForStep(start));

        if (dataChanged || maskChanged) {
            bumpStepDataRevision();
        }

        return dataChanged || maskChanged;
    }

    bool copyStepRangeToClipboard(
        uint8_t startStep,
        uint8_t endStep,
        SequencerRangeClipboard& clipboard
    ) const {
        clipboard.reset();

        const uint8_t len = length.get();
        if (len == 0) return false;

        const uint8_t start = static_cast<uint8_t>(std::min(startStep, endStep));
        const uint8_t end = static_cast<uint8_t>(std::max(startStep, endStep));
        if (start >= len || start >= MAX_STEPS) return false;

        const uint8_t clampedEnd = static_cast<uint8_t>(std::min<uint16_t>(end, len - 1));
        const uint8_t count = static_cast<uint8_t>((clampedEnd - start) + 1);
        if (count == 0) return false;

        uint64_t relativeEnabledMask = 0;
        const uint64_t mask = enabledMask.get();

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t step = static_cast<uint8_t>(start + i);
            clipboard.note[i] = note[step];
            clipboard.velocity[i] = velocity[step];
            clipboard.gate[i] = gate[step];
            clipboard.nudge[i] = nudge[step];
            clipboard.probability[i] = probability[step];

            if ((mask & (1ULL << step)) != 0) {
                relativeEnabledMask |= (1ULL << i);
            }
        }

        clipboard.count = count;
        clipboard.enabledMask = relativeEnabledMask;
        clipboard.valid = true;
        return true;
    }

    bool pasteClipboardRange(uint8_t targetStart, const SequencerRangeClipboard& clipboard) {
        if (!clipboard.valid || clipboard.count == 0) return false;
        if (targetStart >= MAX_STEPS) return false;

        const uint8_t maxCount = static_cast<uint8_t>(MAX_STEPS - targetStart);
        const uint8_t copyCount = static_cast<uint8_t>(std::min<uint16_t>(clipboard.count, maxCount));
        if (copyCount == 0) return false;

        uint64_t mask = enabledMask.get();
        bool dataChanged = false;

        for (uint8_t i = 0; i < copyCount; ++i) {
            const uint8_t step = static_cast<uint8_t>(targetStart + i);

            if (note[step] != clipboard.note[i] ||
                velocity[step] != clipboard.velocity[i] ||
                gate[step] != clipboard.gate[i] ||
                nudge[step] != clipboard.nudge[i] ||
                probability[step] != clipboard.probability[i]) {
                dataChanged = true;
            }

            note[step] = clipboard.note[i];
            velocity[step] = clipboard.velocity[i];
            gate[step] = clipboard.gate[i];
            nudge[step] = clipboard.nudge[i];
            probability[step] = clipboard.probability[i];

            const uint64_t bit = (1ULL << step);
            const bool enabled = clipboard.isEnabled(i);
            const bool wasEnabled = (mask & bit) != 0;
            if (enabled != wasEnabled) {
                dataChanged = true;
            }

            if (enabled) {
                mask |= bit;
            } else {
                mask &= ~bit;
            }
        }

        enabledMask.set(mask);

        const uint8_t requiredLength = static_cast<uint8_t>(targetStart + copyCount);
        if (requiredLength > length.get()) {
            length.set(requiredLength);
        }

        focusedStep.set(targetStart);
        page.set(pageForStep(targetStart));

        if (dataChanged) {
            bumpStepDataRevision();
        }

        return true;
    }

    void reset() {
        oc::note::sequencer::StepSequencerState::reset();
        page.set(0);
        focusedStep.set(0);
        bumpStepDataRevision();
        activeStepProperty.set(StepProperty::NOTE);

        stepEdit.reset();
        stepPropertyInlineSelector.reset();
        stepInlineFeedback.reset();
        patternQuickControls.reset();
        rangeSelection.reset();
    }

    void updateUi(uint32_t nowMs) {
        stepInlineFeedback.update(nowMs);
    }

    uint8_t activePageCount() const {
        const uint8_t len = length.get();
        if (len == 0) return 0;
        const uint8_t pages = static_cast<uint8_t>((len + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE);
        return (pages > PAGE_COUNT) ? PAGE_COUNT : pages;
    }

    uint8_t normalizePage(uint8_t page) const {
        const uint8_t pageCount = activePageCount();
        if (pageCount == 0) return 0;
        return static_cast<uint8_t>(page % pageCount);
    }

    uint8_t pageStartStep(uint8_t page) const {
        return static_cast<uint8_t>(normalizePage(page) * STEPS_PER_PAGE);
    }

    uint8_t pageForStep(uint8_t step) const {
        return static_cast<uint8_t>(step / STEPS_PER_PAGE);
    }

    bool resolveStepInPage(uint8_t page, uint8_t indexInPage, uint8_t& outStep) const {
        if (indexInPage >= STEPS_PER_PAGE) return false;

        const uint8_t pageCount = activePageCount();
        if (pageCount == 0) return false;

        const uint8_t safePage = normalizePage(page);
        const uint16_t abs = static_cast<uint16_t>(safePage) * STEPS_PER_PAGE + indexInPage;
        if (abs >= length.get() || abs >= MAX_STEPS) return false;

        outStep = static_cast<uint8_t>(abs);
        return true;
    }

    bool isInPattern(uint8_t step) const {
        return step < length.get();
    }
};

}  // namespace core::state::sequencer

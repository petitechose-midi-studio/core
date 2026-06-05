#pragma once

/**
 * @file SequencerState.hpp
 * @brief Sequencer state for Core UI and playback engine integration
 */

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "SequencerPatternState.hpp"
#include "SequencerUiState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

struct SequencerState {
    static constexpr uint8_t STEPS_PER_PAGE = SequencerPatternState::STEPS_PER_PAGE;
    static constexpr uint8_t MAX_STEPS = SequencerPatternState::MAX_STEPS;
    static constexpr uint8_t PAGE_COUNT = SequencerPatternState::PAGE_COUNT;
    static constexpr uint16_t MAX_GATE_PERCENT = SequencerPatternState::MAX_GATE_PERCENT;
    static constexpr uint8_t DEFAULT_NOTE = SequencerPatternState::DEFAULT_NOTE;
    static constexpr uint8_t DEFAULT_VELOCITY = SequencerPatternState::DEFAULT_VELOCITY;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = SequencerPatternState::DEFAULT_GATE_PERCENT;
    static constexpr uint8_t DEFAULT_PROBABILITY = SequencerPatternState::DEFAULT_PROBABILITY;

    SequencerPatternState pattern;

    /// Visible page index [0..PAGE_COUNT-1].
    /// May temporarily point beyond the current pattern length during paste target selection.
    Signal<uint8_t, 8> page{0};

    /// Absolute focused step index.
    /// May temporarily point beyond the current pattern length during paste target selection.
    Signal<uint8_t, 6> focusedStep{0};

    /// Bumps when runtime publishes or editor mutations invalidate resolved variation observations.
    Signal<uint32_t> variationTelemetryRevision{0};
    Signal<int16_t> playheadStep{-1};
    Signal<uint32_t> probabilityCycleRevision{0};
    oc::note::sequencer::StepBitMask128 probabilityCycleMask{};
    uint32_t probabilityCycleIndex = 0;
    oc::note::sequencer::StepSequencerResolvedVariation lastResolvedVariation{};
    oc::note::sequencer::StepSequencerCycleVariationTelemetry cycleVariationTelemetry{};

    /// Active property edited by the 8 macro encoders in Sequencer view
    Signal<StepProperty, 6> activeStepProperty{StepProperty::NOTE};

    // UI state
    SequencerStepEditOverlayState stepEdit;
    SequencerStepPropertyInlineSelectorState stepPropertyInlineSelector;
    SequencerStepInlineFeedbackState stepInlineFeedback;
    SequencerPatternVariationFeedbackState patternVariationFeedback;
    SequencerHistoryFeedbackState historyFeedback;
    SequencerPatternQuickControlsState patternQuickControls;
    SequencerStructureUiState structureUi;

    static uint8_t clampMidi7(uint8_t value) {
        return SequencerPatternState::clampMidi7(value);
    }

    static uint16_t clampGatePercent(uint16_t value) {
        return SequencerPatternState::clampGatePercent(value);
    }

    static int8_t clampNudge(int value) {
        return SequencerPatternState::clampNudge(value);
    }

    static uint8_t clampProbability(uint8_t value) {
        return SequencerPatternState::clampProbability(value);
    }

    void invalidateVariationTelemetry() {
        lastResolvedVariation = {};
        cycleVariationTelemetry.reset();
        variationTelemetryRevision.set(variationTelemetryRevision.get() + 1);
    }

    void invalidateStepVariationTelemetry(uint8_t step) {
        if (step >= MAX_STEPS) return;
        if (!cycleVariationTelemetry.validMask.test(step) &&
            lastResolvedVariation.stepIndex != step) {
            return;
        }

        cycleVariationTelemetry.validMask.setBit(step, false);
        cycleVariationTelemetry.triggeredMask.setBit(step, false);
        cycleVariationTelemetry.scaleInMask.setBit(step, false);
        cycleVariationTelemetry.scaleConstrainedMask.setBit(step, false);
        if (lastResolvedVariation.stepIndex == step) {
            lastResolvedVariation = {};
        }
        variationTelemetryRevision.set(variationTelemetryRevision.get() + 1);
    }

    uint8_t variationRangeForProperty(StepProperty property) const {
        return pattern.variationRangeForProperty(property);
    }

    bool setVariationRangeForProperty(StepProperty property, uint8_t range) {
        if (!pattern.setVariationRangeForProperty(property, range)) return false;
        invalidateVariationTelemetry();
        return true;
    }

    bool setPatternVariationRanges(
        oc::note::sequencer::StepSequencerVariationRanges ranges
    ) {
        if (!pattern.setPatternVariationRanges(ranges)) return false;
        invalidateVariationTelemetry();
        return true;
    }

    bool setPatternScalePolicy(SequencerPatternScalePolicy policy) {
        if (!pattern.setPatternScalePolicy(policy)) return false;
        invalidateVariationTelemetry();
        return true;
    }

    bool setPatternScaleOverride(oc::note::sequencer::StepSequencerScaleSettings settings) {
        if (!pattern.setPatternScaleOverride(settings)) return false;
        invalidateVariationTelemetry();
        return true;
    }

    bool setPitchEditMode(SequencerPitchEditMode mode) {
        if (!pattern.setPitchEditMode(mode)) return false;
        invalidateVariationTelemetry();
        return true;
    }

    bool setStepNoteAt(uint8_t step, uint8_t noteValue) {
        if (!pattern.setStepNoteAt(step, noteValue)) return false;
        invalidateStepVariationTelemetry(step);
        return true;
    }

    bool setStepVelocityAt(uint8_t step, uint8_t velocityValue) {
        if (!pattern.setStepVelocityAt(step, velocityValue)) return false;
        invalidateStepVariationTelemetry(step);
        return true;
    }

    bool setStepGateAt(uint8_t step, uint16_t gatePercent) {
        if (!pattern.setStepGateAt(step, gatePercent)) return false;
        invalidateStepVariationTelemetry(step);
        return true;
    }

    bool setStepNudgeAt(uint8_t step, int8_t nudgeValue) {
        if (!pattern.setStepNudgeAt(step, nudgeValue)) return false;
        invalidateStepVariationTelemetry(step);
        return true;
    }

    bool setStepProbabilityAt(uint8_t step, uint8_t probabilityValue) {
        if (!pattern.setStepProbabilityAt(step, probabilityValue)) return false;
        invalidateStepVariationTelemetry(step);
        return true;
    }

    bool setStepDataAt(uint8_t step, uint8_t noteValue, uint8_t velocityValue, uint16_t gatePercent) {
        if (step >= MAX_STEPS) return false;
        return setStepDataAt(
            step,
            noteValue,
            velocityValue,
            gatePercent,
            pattern.nudge[step],
            pattern.probability[step]
        );
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
            pattern.probability[step]
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
        if (!pattern.setStepDataAt(
                step,
                noteValue,
                velocityValue,
                gatePercent,
                nudgeValue,
                probabilityValue
            )) {
            return false;
        }
        invalidateStepVariationTelemetry(step);
        return true;
    }

    void reset() {
        pattern.reset();
        page.set(0);
        focusedStep.set(0);
        playheadStep.set(-1);
        probabilityCycleRevision.set(0);
        probabilityCycleMask = {};
        probabilityCycleIndex = 0;
        lastResolvedVariation = {};
        cycleVariationTelemetry.reset();
        variationTelemetryRevision.set(0);
        activeStepProperty.set(StepProperty::NOTE);

        stepEdit.reset();
        stepPropertyInlineSelector.reset();
        stepInlineFeedback.reset();
        patternVariationFeedback.reset();
        historyFeedback.reset();
        patternQuickControls.reset();
        structureUi.reset();
    }

    void updateUi(uint32_t nowMs) {
        stepInlineFeedback.update(nowMs);
        patternVariationFeedback.update(nowMs);
        historyFeedback.update(nowMs);
    }

    uint8_t activePageCount() const {
        return pattern.activePageCount();
    }

    uint8_t normalizePage(uint8_t page) const {
        return pattern.normalizePage(page);
    }

    uint8_t clampPage(uint8_t page) const {
        return pattern.clampPage(page);
    }

    uint8_t visiblePage() const {
        if (structureUi.previewAddPageSlot.get()) {
            return clampPage(page.get());
        }
        return normalizePage(page.get());
    }

    uint8_t pageStartStep(uint8_t page) const {
        return pattern.pageStartStep(page);
    }

    uint8_t pageStartStepClamped(uint8_t page) const {
        return pattern.pageStartStepClamped(page);
    }

    uint8_t pageForStep(uint8_t step) const {
        return pattern.pageForStep(step);
    }

    bool resolveStepInPage(uint8_t page, uint8_t indexInPage, uint8_t& outStep) const {
        return pattern.resolveStepInPage(page, indexInPage, outStep);
    }

    bool isInPattern(uint8_t step) const {
        return pattern.isInPattern(step);
    }
};

}  // namespace core::state::sequencer

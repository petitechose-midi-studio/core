#pragma once

/**
 * @file SequencerPatternState.hpp
 * @brief Persisted musical state for one sequencer pattern.
 */

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include "SequencerPatternData.hpp"

#include "app/ExtmemAllocator.hpp"
#include "SequencerCcLaneDomain.hpp"
#include "SequencerScaleState.hpp"
#include "StepProperty.hpp"

namespace core::state::sequencer {

inline constexpr std::array<uint8_t, 6> PATTERN_STEPS_PER_BEAT_CHOICES = {
    1, 2, 3, 4, 6, 8,
};

using oc::state::Signal;

struct SequencerPatternState : public SequencerPatternData {
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr uint8_t MAX_STEPS = oc::note::sequencer::StepSequencerState::MAX_STEPS;
    static constexpr uint8_t PAGE_COUNT = (MAX_STEPS + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE;
    static constexpr uint8_t DEFAULT_LENGTH =
        oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    static constexpr uint8_t DEFAULT_STEPS_PER_BEAT =
        oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    static constexpr uint16_t MAX_GATE_PERCENT =
        oc::note::sequencer::StepSequencerState::MAX_GATE_PERCENT;
    static constexpr uint8_t DEFAULT_PROBABILITY =
        oc::note::sequencer::StepSequencerState::DEFAULT_PROBABILITY;
    static constexpr int8_t MIN_PATTERN_SWING_OFFSET_PERCENT = -75;
    static constexpr int8_t MAX_PATTERN_SWING_OFFSET_PERCENT = 75;
    static constexpr uint8_t MAX_EFFECTIVE_SWING_PERCENT = 75;
    static constexpr int8_t MIN_PATTERN_NUDGE_PERCENT = -50;
    static constexpr int8_t MAX_PATTERN_NUDGE_PERCENT = 50;

    SequencerPatternState() { resetStepData(); }
    SequencerPatternState(const SequencerPatternState&) = delete;
    SequencerPatternState& operator=(const SequencerPatternState&) = delete;
    uint32_t ccLaneRevision = 0U;

    void setLength(uint8_t value) {
        if (length == value) return;
        length = value;
        publishChange(SequencerPatternChange::LENGTH);
    }
    void setStepsPerBeat(uint8_t value) {
        if (stepsPerBeat == value) return;
        stepsPerBeat = value;
        publishChange(SequencerPatternChange::TIMING);
    }
    void setEnabledMask(oc::note::sequencer::StepBitMask128 value) {
        if (enabledMask == value) return;
        enabledMask = value;
        publishChange(SequencerPatternChange::ENABLED);
    }
    void setStepDataRevision(uint32_t value) {
        if (stepDataRevision == value) return;
        stepDataRevision = value;
        publishChange(SequencerPatternChange::STEPS);
    }
    void setPatternVariationRevision(uint32_t value) {
        if (patternVariationRevision == value) return;
        patternVariationRevision = value;
        publishChange(SequencerPatternChange::VARIATION);
    }
    void setPatternScaleRevision(uint32_t value) {
        if (patternScaleRevision == value) return;
        patternScaleRevision = value;
        publishChange(SequencerPatternChange::SCALE);
    }
    void setGraphRevision(uint32_t value) {
        if (graphRevision == value) return;
        graphRevision = value;
        publishChange(SequencerPatternChange::GRAPH);
    }
    void setCcLaneRevision(uint32_t value) {
        if (ccLaneRevision == value) return;
        ccLaneRevision = value;
        publishChange(SequencerPatternChange::CC);
    }
    void setPatternTimingRevision(uint32_t value) {
        if (patternTimingRevision == value) return;
        patternTimingRevision = value;
        publishChange(SequencerPatternChange::TIMING);
    }
    void setSwingOffsetPercent(int8_t value) {
        if (swingOffsetPercent == value) return;
        swingOffsetPercent = value;
        publishChange(SequencerPatternChange::TIMING);
    }

    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph;
    // Four sparse lanes are materialized only when used. The editor and every
    // bank Track live in EXTMEM, while each 840-byte bank is independently
    // allocated so an empty Project pays only one pointer per Pattern.
    core::app::ExtmemUniquePtr<SequencerCcLaneBank> ccLanes;

    ~SequencerPatternState();

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

    static int8_t clampPatternSwingOffsetPercent(int value) {
        if (value < MIN_PATTERN_SWING_OFFSET_PERCENT) return MIN_PATTERN_SWING_OFFSET_PERCENT;
        if (value > MAX_PATTERN_SWING_OFFSET_PERCENT) return MAX_PATTERN_SWING_OFFSET_PERCENT;
        return static_cast<int8_t>(value);
    }

    static int8_t clampPatternNudgePercent(int value) {
        if (value < MIN_PATTERN_NUDGE_PERCENT) return MIN_PATTERN_NUDGE_PERCENT;
        if (value > MAX_PATTERN_NUDGE_PERCENT) return MAX_PATTERN_NUDGE_PERCENT;
        return static_cast<int8_t>(value);
    }

    static uint8_t clampEffectiveSwingPercent(int value) {
        if (value < 0) return 0;
        if (value > MAX_EFFECTIVE_SWING_PERCENT) return MAX_EFFECTIVE_SWING_PERCENT;
        return static_cast<uint8_t>(value);
    }

    static uint8_t clampProbability(uint8_t value) {
        return oc::note::sequencer::StepSequencerState::clampProbability(value);
    }

    void bumpStepDataRevision() {
        setStepDataRevision(stepDataRevision + 1);
    }

    void bumpPatternVariationRevision() {
        setPatternVariationRevision(patternVariationRevision + 1);
    }

    void bumpPatternScaleRevision() {
        setPatternScaleRevision(patternScaleRevision + 1);
    }

    void bumpGraphRevision() {
        setGraphRevision(graphRevision + 1);
    }

    void bumpCcLaneRevision() {
        setCcLaneRevision(ccLaneRevision + 1);
    }

    void bumpPatternTimingRevision() {
        setPatternTimingRevision(patternTimingRevision + 1);
    }

    uint8_t effectiveSwingPercent(uint8_t projectSwingPercent) const {
        return clampEffectiveSwingPercent(
            static_cast<int>(projectSwingPercent) + static_cast<int>(swingOffsetPercent)
        );
    }

    uint8_t variationRangeForProperty(StepProperty property) const;
    bool setVariationRangeForProperty(StepProperty property, uint8_t range);
    bool setPatternVariationRanges(oc::note::sequencer::StepSequencerVariationRanges ranges);
    bool setPatternScalePolicy(SequencerPatternScalePolicy policy);
    bool setPatternScaleOverride(oc::note::sequencer::StepSequencerScaleSettings settings);
    bool setPitchEditMode(SequencerPitchEditMode mode);
    bool setPatternSwingOffsetPercent(int value);
    bool setPatternNudgePercent(int value);
    bool setContentLength(uint8_t newContentLength);

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

    uint8_t patternLength() const { return std::min(length, MAX_STEPS); }
    bool isEnabled(uint8_t step) const { return step < MAX_STEPS && enabledMask.test(step); }
    void setEnabled(uint8_t step, bool enabled) {
        if (step >= MAX_STEPS) return;
        auto next = enabledMask;
        next.setBit(step, enabled);
        setEnabledMask(next);
    }
    void toggle(uint8_t step) {
        if (step >= MAX_STEPS) return;
        auto next = enabledMask;
        next.toggleBit(step);
        setEnabledMask(next);
    }

    void reset();

    uint8_t activePageCount() const {
        const uint8_t len = length;
        if (len == 0) return 0;
        const uint8_t pages = static_cast<uint8_t>((len + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE);
        return (pages > PAGE_COUNT) ? PAGE_COUNT : pages;
    }

    uint8_t normalizePage(uint8_t page) const {
        const uint8_t pageCount = activePageCount();
        if (pageCount == 0) return 0;
        return static_cast<uint8_t>(page % pageCount);
    }

    uint8_t clampPage(uint8_t page) const {
        return (page >= PAGE_COUNT) ? static_cast<uint8_t>(PAGE_COUNT - 1) : page;
    }

    uint8_t pageStartStep(uint8_t page) const {
        return static_cast<uint8_t>(normalizePage(page) * STEPS_PER_PAGE);
    }

    uint8_t pageStartStepClamped(uint8_t page) const {
        return static_cast<uint8_t>(clampPage(page) * STEPS_PER_PAGE);
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
        if (abs >= length || abs >= MAX_STEPS) return false;

        outStep = static_cast<uint8_t>(abs);
        return true;
    }

    bool isInPattern(uint8_t step) const {
        return step < length;
    }
private:
    void publishChange(SequencerPatternChange change);
    SequencerPatternObservation* observation_ = nullptr;
    friend class SequencerPatternObservation;
};

}  // namespace core::state::sequencer

#pragma once

/**
 * @file SequencerState.hpp
 * @brief Sequencer state for Core UI + v0 playback engine integration
 */

#include <cstdint>

#include <oc/note/sequencer/StepSequencerState.hpp>
#include <oc/state/Signal.hpp>

namespace core::state::sequencer {

using oc::state::Signal;

enum class StepProperty : uint8_t {
    NOTE = 0,
    VELOCITY = 1,
    GATE = 2,
};

/**
 * @brief Core sequencer state
 *
 * This extends the reusable engine state (oc-note) with UI-only fields.
 */

struct SequencerPatternConfigOverlayState {
    Signal<bool> visible{false};
    Signal<uint8_t> focusedRow{0};

    // Snapshot for cancel (live editing)
    uint8_t snapshotLength = 0;
    uint8_t snapshotStepsPerBeat = 0;
    uint8_t snapshotMidiChannel = 0;
    bool snapshotValid = false;

    void reset() {
        focusedRow.set(0);
        snapshotValid = false;
    }
};

struct SequencerStepEditOverlayState {
    Signal<bool> visible{false};
    Signal<uint8_t> stepIndex{0};    // absolute step index
    Signal<uint8_t> focusedRow{0};   // 0=NOTE, 1=VEL, 2=GATE

    // Snapshot for cancel (live editing)
    uint8_t snapshotNote = 0;
    uint8_t snapshotVelocity = 0;
    uint16_t snapshotGate = 0;
    bool snapshotValid = false;

    void reset() {
        stepIndex.set(0);
        focusedRow.set(0);
        snapshotValid = false;
    }
};

struct SequencerPropertySelectorOverlayState {
    Signal<bool> visible{false};
    Signal<int> selectedIndex{0};

    int snapshotIndex = 0;
    bool snapshotValid = false;

    void reset() {
        selectedIndex.set(0);
        snapshotValid = false;
    }
};

struct SequencerSettingsOverlayState {
    Signal<bool> visible{false};
    Signal<uint8_t> focusedRow{0};

    void reset() {
        focusedRow.set(0);
    }
};

struct SequencerTrackConfigOverlayState {
    Signal<bool> visible{false};
    Signal<uint8_t> focusedRow{0};

    void reset() {
        focusedRow.set(0);
    }
};

struct SequencerState : public oc::note::sequencer::StepSequencerState {
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr uint8_t MAX_STEPS = oc::note::sequencer::StepSequencerState::MAX_STEPS;
    static constexpr uint8_t PAGE_COUNT = (MAX_STEPS + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE;
    static constexpr uint16_t MAX_GATE_PERCENT =
        oc::note::sequencer::StepSequencerState::MAX_GATE_PERCENT;

    /// Visible page index [0..PAGE_COUNT-1]
    Signal<uint8_t> page{0};

    /// Absolute focused step index [0..length-1]
    Signal<uint8_t> focusedStep{0};

    /// Bumps when non-signal step arrays change (note/velocity/gate)
    Signal<uint32_t> stepDataRevision{0};

    /// Active property edited by the 8 macro encoders in Sequencer view
    Signal<StepProperty> activeStepProperty{StepProperty::NOTE};

    // Overlay state (UI-only)
    SequencerPatternConfigOverlayState patternConfig;
    SequencerStepEditOverlayState stepEdit;
    SequencerPropertySelectorOverlayState propertySelector;
    SequencerSettingsOverlayState settings;
    SequencerTrackConfigOverlayState trackConfig;

    static uint8_t clampMidi7(uint8_t value) {
        return (value > 127U) ? 127U : value;
    }

    static uint16_t clampGatePercent(uint16_t value) {
        return (value > MAX_GATE_PERCENT) ? MAX_GATE_PERCENT : value;
    }

    void bumpStepDataRevision() {
        stepDataRevision.set(stepDataRevision.get() + 1);
    }

    bool setStepNoteAt(uint8_t step, uint8_t noteValue) {
        if (step >= MAX_STEPS) return false;
        note[step] = clampMidi7(noteValue);
        bumpStepDataRevision();
        return true;
    }

    bool setStepVelocityAt(uint8_t step, uint8_t velocityValue) {
        if (step >= MAX_STEPS) return false;
        velocity[step] = clampMidi7(velocityValue);
        bumpStepDataRevision();
        return true;
    }

    bool setStepGateAt(uint8_t step, uint16_t gatePercent) {
        if (step >= MAX_STEPS) return false;
        gate[step] = clampGatePercent(gatePercent);
        bumpStepDataRevision();
        return true;
    }

    bool setStepDataAt(uint8_t step, uint8_t noteValue, uint8_t velocityValue, uint16_t gatePercent) {
        if (step >= MAX_STEPS) return false;
        note[step] = clampMidi7(noteValue);
        velocity[step] = clampMidi7(velocityValue);
        gate[step] = clampGatePercent(gatePercent);
        bumpStepDataRevision();
        return true;
    }

    void reset() {
        oc::note::sequencer::StepSequencerState::reset();
        page.set(0);
        focusedStep.set(0);
        bumpStepDataRevision();
        activeStepProperty.set(StepProperty::NOTE);

        patternConfig.reset();
        stepEdit.reset();
        propertySelector.reset();
        settings.reset();
        trackConfig.reset();
    }

    uint8_t activePageCount() const {
        const uint8_t len = length.get();
        if (len == 0) return 0;
        const uint8_t pages = static_cast<uint8_t>((len + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE);
        return (pages > PAGE_COUNT) ? PAGE_COUNT : pages;
    }

    bool isInPattern(uint8_t step) const {
        return step < length.get();
    }
};

}  // namespace core::state::sequencer

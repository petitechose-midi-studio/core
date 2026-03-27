#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state::sequencer {

using oc::state::Signal;

enum class StepProperty : uint8_t {
    NOTE = 0,
    VELOCITY = 1,
    GATE = 2,
    NUDGE = 3,
    PROBABILITY = 4,
};

enum class PatternQuickControlItem : uint8_t {
    CHANNEL = 0,
    DIVISION = 1,
    LENGTH = 2,
};

struct SequencerStepEditOverlayState {
    Signal<bool> visible{false};
    Signal<uint8_t> stepIndex{0};
    Signal<uint8_t> focusedRow{0};

    uint8_t snapshotNote = 0;
    uint8_t snapshotVelocity = 0;
    uint16_t snapshotGate = 0;
    int8_t snapshotNudge = 0;
    uint8_t snapshotProbability = 100;
    bool snapshotValid = false;

    void reset() {
        stepIndex.set(0);
        focusedRow.set(0);
        snapshotValid = false;
    }
};

struct SequencerStepPropertyInlineSelectorState {
    Signal<bool> selecting{false};
    Signal<int> selectedIndex{0};

    int snapshotIndex = 0;
    bool snapshotValid = false;

    void reset() {
        selecting.set(false);
        selectedIndex.set(0);
        snapshotValid = false;
    }
};

struct SequencerStepInlineFeedbackState {
    static constexpr uint32_t DISPLAY_HOLD_MS = 700;

    Signal<bool> visible{false};
    Signal<uint8_t> stepIndex{0};
    Signal<StepProperty> property{StepProperty::NOTE};

    uint32_t hideAtMs = 0;

    void show(uint8_t step, StepProperty stepProperty, uint32_t nowMs) {
        stepIndex.set(step);
        property.set(stepProperty);
        hideAtMs = nowMs + DISPLAY_HOLD_MS;
        visible.set(true);
    }

    void update(uint32_t nowMs) {
        if (!visible.get()) return;
        if (nowMs < hideAtMs) return;
        visible.set(false);
    }

    void reset() {
        visible.set(false);
        stepIndex.set(0);
        property.set(StepProperty::NOTE);
        hideAtMs = 0;
    }
};

struct SequencerPatternQuickControlsState {
    Signal<bool> selecting{false};
    Signal<PatternQuickControlItem> focusedItem{PatternQuickControlItem::CHANNEL};

    uint8_t snapshotLength = 0;
    uint8_t snapshotStepsPerBeat = 0;
    uint8_t snapshotMidiChannel = 0;
    bool snapshotValid = false;

    void reset() {
        selecting.set(false);
        snapshotValid = false;
    }
};

}  // namespace core::state::sequencer

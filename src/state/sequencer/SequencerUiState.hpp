#pragma once

#include <array>
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

enum class RangeSelectionKind : uint8_t {
    NONE = 0,
    CLEAR = 1,
    COPY = 2,
};

enum class RangeSelectionPhase : uint8_t {
    IDLE = 0,
    SELECT_RANGE = 1,
    CONFIRM_CLEAR = 2,
    PASTE_TARGET = 3,
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
    static constexpr uint8_t MAX_STEPS = 64;

    Signal<bool> visible{false};
    Signal<uint64_t> touchedMask{0};
    Signal<StepProperty> property{StepProperty::NOTE};

    uint32_t hideAtMs[MAX_STEPS]{};

    void show(uint8_t step, StepProperty stepProperty, uint32_t nowMs) {
        if (step >= MAX_STEPS) return;

        uint64_t mask = touchedMask.get();
        mask |= (1ULL << step);
        touchedMask.set(mask);
        property.set(stepProperty);
        hideAtMs[step] = nowMs + DISPLAY_HOLD_MS;
        visible.set(true);
    }

    void update(uint32_t nowMs) {
        if (!visible.get()) return;

        uint64_t nextMask = touchedMask.get();
        if (nextMask == 0) {
            visible.set(false);
            return;
        }

        for (uint8_t step = 0; step < MAX_STEPS; ++step) {
            const uint64_t bit = (1ULL << step);
            if ((nextMask & bit) == 0) continue;
            if (nowMs < hideAtMs[step]) continue;
            nextMask &= ~bit;
            hideAtMs[step] = 0;
        }

        touchedMask.set(nextMask);
        visible.set(nextMask != 0);
    }

    void reset() {
        visible.set(false);
        touchedMask.set(0);
        property.set(StepProperty::NOTE);
        for (auto& value : hideAtMs) {
            value = 0;
        }
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

struct SequencerRangeClipboard {
    static constexpr uint8_t MAX_STEPS = 64;

    bool valid = false;
    uint8_t count = 0;
    uint64_t enabledMask = 0;
    std::array<uint8_t, MAX_STEPS> note{};
    std::array<uint8_t, MAX_STEPS> velocity{};
    std::array<uint16_t, MAX_STEPS> gate{};
    std::array<int8_t, MAX_STEPS> nudge{};
    std::array<uint8_t, MAX_STEPS> probability{};

    void reset() {
        valid = false;
        count = 0;
        enabledMask = 0;
    }

    bool isEnabled(uint8_t index) const {
        if (index >= count) return false;
        return (enabledMask & (1ULL << index)) != 0;
    }
};

struct SequencerRangeSelectionState {
    Signal<RangeSelectionKind> kind{RangeSelectionKind::NONE};
    Signal<RangeSelectionPhase> phase{RangeSelectionPhase::IDLE};
    Signal<uint8_t> cursorStep{0};
    Signal<uint8_t> anchorStep{0};
    Signal<uint8_t> rangeStart{0};
    Signal<uint8_t> rangeEnd{0};
    Signal<bool> rangeValid{false};

    SequencerRangeClipboard clipboard{};

    bool active() const { return kind.get() != RangeSelectionKind::NONE; }
    bool selectingSourceRange() const { return phase.get() == RangeSelectionPhase::SELECT_RANGE; }
    bool confirmingClearPage() const {
        return kind.get() == RangeSelectionKind::CLEAR &&
               phase.get() == RangeSelectionPhase::CONFIRM_CLEAR &&
               rangeValid.get();
    }
    bool selectingPasteTarget() const {
        return kind.get() == RangeSelectionKind::COPY &&
               phase.get() == RangeSelectionPhase::PASTE_TARGET &&
               clipboard.valid;
    }

    void reset() {
        kind.set(RangeSelectionKind::NONE);
        phase.set(RangeSelectionPhase::IDLE);
        cursorStep.set(0);
        anchorStep.set(0);
        rangeStart.set(0);
        rangeEnd.set(0);
        rangeValid.set(false);
        clipboard.reset();
    }
};

}  // namespace core::state::sequencer

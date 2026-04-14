#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/note/sequencer/StepSequencerState.hpp>
#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/StructureSelectionState.hpp"

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
    OFFSET = 0,
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
    PASTE_TARGET = 2,
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
    Signal<bool, 4> selecting{false};
    Signal<int, 4> selectedIndex{0};

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
    static constexpr uint8_t MAX_STEPS = oc::note::sequencer::StepSequencerState::MAX_STEPS;

    Signal<bool> visible{false};
    Signal<oc::note::sequencer::StepBitMask128> touchedMask{};
    Signal<StepProperty> property{StepProperty::NOTE};

    uint32_t hideAtMs[MAX_STEPS]{};

    void show(uint8_t step, StepProperty stepProperty, uint32_t nowMs) {
        if (step >= MAX_STEPS) return;

        auto mask = touchedMask.get();
        mask.setBit(step, true);
        touchedMask.set(mask);
        property.set(stepProperty);
        hideAtMs[step] = nowMs + DISPLAY_HOLD_MS;
        visible.set(true);
    }

    void update(uint32_t nowMs) {
        if (!visible.get()) return;

        auto nextMask = touchedMask.get();
        if (!nextMask.any()) {
            visible.set(false);
            return;
        }

        for (uint8_t step = 0; step < MAX_STEPS; ++step) {
            if (!nextMask.test(step)) continue;
            if (nowMs < hideAtMs[step]) continue;
            nextMask.setBit(step, false);
            hideAtMs[step] = 0;
        }

        touchedMask.set(nextMask);
        visible.set(nextMask.any());
    }

    void reset() {
        visible.set(false);
        touchedMask.set({});
        property.set(StepProperty::NOTE);
        for (auto& value : hideAtMs) {
            value = 0;
        }
    }
};

struct SequencerPatternQuickControlsState {
    Signal<bool, 4> selecting{false};
    Signal<PatternQuickControlItem, 4> focusedItem{
        PatternQuickControlItem::OFFSET
    };
    Signal<int8_t, 4> offsetSteps{0};

    void reset() {
        selecting.set(false);
        offsetSteps.set(0);
    }
};

struct SequencerStructureUiState {
    Signal<bool, 4> previewAddSlot{false};
    Signal<uint8_t, 4> previewTrackIndex{0};
    Signal<uint8_t, 4> previewPageIndex{0};
    core::state::StructureHoldState hold;
    core::state::StructureSelectionState selection;

    void syncPreviewTrack(uint8_t trackIndex) {
        previewTrackIndex.set(trackIndex);
    }

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset() {
        previewAddSlot.set(false);
        previewTrackIndex.set(0);
        previewPageIndex.set(0);
        hold.clear();
        selection.reset(core::state::StructureSelectionScope::PAGE);
    }
};

struct SequencerRangeClipboard {
    static constexpr uint8_t MAX_STEPS = oc::note::sequencer::StepSequencerState::MAX_STEPS;

    bool valid = false;
    uint8_t count = 0;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    std::array<uint8_t, MAX_STEPS> note{};
    std::array<uint8_t, MAX_STEPS> velocity{};
    std::array<uint16_t, MAX_STEPS> gate{};
    std::array<int8_t, MAX_STEPS> nudge{};
    std::array<uint8_t, MAX_STEPS> probability{};

    void reset() {
        valid = false;
        count = 0;
        enabledMask = {};
    }

    bool isEnabled(uint8_t index) const {
        if (index >= count) return false;
        return enabledMask.test(index);
    }
};

struct SequencerRangeSelectionState {
    Signal<RangeSelectionKind, 4> kind{RangeSelectionKind::NONE};
    Signal<RangeSelectionPhase, 4> phase{RangeSelectionPhase::IDLE};
    Signal<uint8_t> cursorStep{0};
    Signal<uint8_t> anchorStep{0};
    Signal<uint8_t> rangeStart{0};
    Signal<uint8_t> rangeEnd{0};
    Signal<bool> rangeValid{false};

    SequencerRangeClipboard clipboard{};
    uint8_t snapshotPage = 0;
    uint8_t snapshotFocusedStep = 0;

    bool active() const { return kind.get() != RangeSelectionKind::NONE; }
    bool selectingSourceRange() const { return phase.get() == RangeSelectionPhase::SELECT_RANGE; }
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
        snapshotPage = 0;
        snapshotFocusedStep = 0;
    }
};

}  // namespace core::state::sequencer

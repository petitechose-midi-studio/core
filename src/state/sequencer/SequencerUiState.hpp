#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/note/sequencer/StepSequencerState.hpp>
#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/StructureSelectionState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

/**
 * Session-only sequencer UI state and quick-edit enums.
 *
 * Pattern data lives in SequencerState; these structs track overlays, inline
 * selector focus, temporary feedback, and page-structure UI.
 */
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
    Signal<bool, 6> selecting{false};
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

struct SequencerPatternVariationFeedbackState {
    static constexpr uint32_t DISPLAY_HOLD_MS = 700;

    Signal<bool> visible{false};
    Signal<StepProperty> property{StepProperty::NOTE};
    uint32_t hideAtMs = 0;

    void show(StepProperty stepProperty, uint32_t nowMs) {
        property.set(stepProperty);
        hideAtMs = nowMs + DISPLAY_HOLD_MS;
        visible.set(true);
    }

    void update(uint32_t nowMs) {
        if (!visible.get()) return;
        if (nowMs < hideAtMs) return;
        visible.set(false);
        hideAtMs = 0;
    }

    void reset() {
        visible.set(false);
        property.set(StepProperty::NOTE);
        hideAtMs = 0;
    }
};

struct SequencerPatternQuickControlsState {
    Signal<bool, 6> selecting{false};
    Signal<PatternQuickControlItem, 6> focusedItem{
        PatternQuickControlItem::OFFSET
    };
    Signal<int8_t, 4> offsetSteps{0};

    void reset() {
        selecting.set(false);
        offsetSteps.set(0);
    }
};

struct SequencerStructureUiState {
    Signal<bool, 4> previewAddPageSlot{false};
    Signal<uint8_t, 4> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset() {
        previewAddPageSlot.set(false);
        previewPageIndex.set(0);
        pageHold.clear();
        pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    }
};

}  // namespace core::state::sequencer

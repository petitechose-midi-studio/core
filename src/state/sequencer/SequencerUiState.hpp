#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/state/Signal.hpp>

#include "state/StructureSelectionState.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/StepProperty.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

/**
 * Session-only sequencer UI state and quick-edit enums.
 *
 * Pattern data lives in SequencerPatternState; these structs track overlays, inline
 * selector focus, temporary feedback, and page-structure UI.
 */
enum class PatternQuickControlItem : uint8_t {
    OFFSET = 0,
    DIVISION = 1,
    LENGTH = 2,
};

enum class SequencerContentViewKind : uint8_t {
    ROOT = 0,
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

struct SequencerContentViewFrame {
    using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;

    SequencerContentViewKind kind = SequencerContentViewKind::ROOT;
    uint8_t ownerRootStep = 0;
    uint8_t ownerLocalStep = 0;
    uint8_t pageSnapshot = 0;
    uint8_t focusSnapshot = 0;
    uint16_t ownerNodeId = GraphLimits::INVALID_ID;
    uint16_t sequenceId = GraphLimits::INVALID_ID;
    uint16_t cycleSetId = GraphLimits::INVALID_ID;
    uint8_t length = 0;
};

struct SequencerContentViewState {
    using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;
    static constexpr uint8_t MAX_CHILD_DEPTH = GraphLimits::MAX_DEPTH;

    Signal<SequencerContentViewKind, 8> kind{SequencerContentViewKind::ROOT};
    Signal<uint8_t, 8> parentStep{0};
    Signal<uint16_t, 8> ownerNodeId{GraphLimits::INVALID_ID};
    Signal<uint16_t, 8> sequenceId{GraphLimits::INVALID_ID};
    Signal<uint16_t, 8> cycleSetId{GraphLimits::INVALID_ID};
    Signal<uint8_t, 8> length{0};
    Signal<uint8_t, 8> depth{0};
    Signal<uint32_t, 8> revision{0};

    uint8_t rootPageSnapshot = 0;
    uint8_t rootFocusSnapshot = 0;
    uint8_t stackDepth = 0;
    std::array<SequencerContentViewFrame, MAX_CHILD_DEPTH> frames{};

    bool isMicroSequence() const {
        return kind.get() == SequencerContentViewKind::MICRO_SEQUENCE &&
               sequenceId.get() != GraphLimits::INVALID_ID;
    }

    bool isCycleStates() const {
        return kind.get() == SequencerContentViewKind::CYCLE_STATES &&
               cycleSetId.get() != GraphLimits::INVALID_ID;
    }

    bool isChildContent() const {
        return stackDepth > 0 && (isMicroSequence() || isCycleStates());
    }

    const SequencerContentViewFrame* currentFrame() const {
        if (stackDepth == 0 || stackDepth > frames.size()) return nullptr;
        return &frames[stackDepth - 1U];
    }

    SequencerContentViewFrame* currentFrame() {
        if (stackDepth == 0 || stackDepth > frames.size()) return nullptr;
        return &frames[stackDepth - 1U];
    }

    void bump() {
        revision.set(revision.get() + 1U);
    }

    void reset();
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
    core::state::StructureHoldState contextHold;

    void reset();
};

struct SequencerStepPropertyInlineSelectorState {
    Signal<bool, 6> selecting{false};
    Signal<int, 4> selectedIndex{0};

    int snapshotIndex = 0;
    bool snapshotValid = false;

    void reset();
};

struct SequencerStepInlineFeedbackState {
    static constexpr uint32_t DISPLAY_HOLD_MS = 700;
    static constexpr uint8_t MAX_STEPS = SequencerPatternState::MAX_STEPS;

    Signal<bool> visible{false};
    Signal<oc::note::sequencer::StepBitMask128> touchedMask{};
    Signal<StepProperty> property{StepProperty::NOTE};

    uint32_t hideAtMs[MAX_STEPS]{};

    void show(uint8_t step, StepProperty stepProperty, uint32_t nowMs);

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

    void reset();
};

struct SequencerPatternVariationFeedbackState {
    static constexpr uint32_t DISPLAY_HOLD_MS = 700;

    Signal<bool> visible{false};
    Signal<StepProperty> property{StepProperty::NOTE};
    uint32_t hideAtMs = 0;

    void show(StepProperty stepProperty, uint32_t nowMs);

    void update(uint32_t nowMs) {
        if (!visible.get()) return;
        if (nowMs < hideAtMs) return;
        visible.set(false);
        hideAtMs = 0;
    }

    void reset();
};

struct SequencerHistoryFeedbackState {
    static constexpr uint32_t DISPLAY_HOLD_MS = 1200;
    static constexpr size_t LINE_SIZE = 32;

    Signal<bool, 6> visible{false};
    Signal<uint32_t, 6> revision{0};
    std::array<char, LINE_SIZE> line1{};
    std::array<char, LINE_SIZE> line2{};
    std::array<char, LINE_SIZE> line3{};
    uint32_t hideAtMs = 0;

    void show(const char* nextLine1, const char* nextLine2, const char* nextLine3, uint32_t nowMs);

    void update(uint32_t nowMs) {
        if (!visible.get()) return;
        if (nowMs < hideAtMs) return;
        reset();
    }

    void reset();

private:
    static void copyLine(std::array<char, LINE_SIZE>& destination, const char* source) {
        const char* text = source ? source : "";
        std::strncpy(destination.data(), text, destination.size() - 1);
        destination[destination.size() - 1] = '\0';
    }
};

struct SequencerPatternQuickControlsState {
    Signal<bool, 6> selecting{false};
    Signal<bool, 6> physicalHoldActive{false};
    Signal<PatternQuickControlItem, 6> focusedItem{
        PatternQuickControlItem::OFFSET
    };
    Signal<int8_t, 4> offsetSteps{0};

    SequencerPatternQuickControlsState();

    void reset();
};

struct SequencerStructureUiState {
    Signal<bool, 4> previewAddPageSlot{false};
    Signal<uint8_t, 4> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;

    SequencerStructureUiState();
    ~SequencerStructureUiState();

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset();
};

}  // namespace core::state::sequencer

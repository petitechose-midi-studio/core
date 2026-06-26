#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/state/Signal.hpp>

#include "state/StructureSelectionState.hpp"
#include "state/project/ProjectState.hpp"
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
    SWING = 3,
    NUDGE = 4,
};

enum class SequencerContentViewKind : uint8_t {
    ROOT = 0,
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

enum class SequencerChordEditField : uint8_t {
    MODE = 0,
    VOICES,
    COLOR,
    VARIANT,
    SPREAD,
    STRUM,
    VELOCITY_CURVE,
    COUNT,
};

struct SequencerChordEditorState {
    Signal<bool> active{false};
    Signal<SequencerChordEditField> focusedField{SequencerChordEditField::MODE};

    void reset();
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
    Signal<bool> localVariationEditActive{false};
    SequencerChordEditorState chordEditor;

    core::state::StructureHoldState contextHold;

    void reset();
};

enum class SequencerStepPresetPickerMode : uint8_t {
    LOAD = 0,
    SAVE,
};

enum class SequencerStepPresetFeedback : uint8_t {
    NONE = 0,
    SAVED,
    EMPTY,
    INCOMPATIBLE,
    FAILED,
};

struct SequencerStepPresetPickerState {
    static constexpr uint8_t ENTRY_CAPACITY = 15;
    static constexpr uint8_t ID_SIZE = core::state::project::ProjectMetadata::ID_SIZE;

    Signal<bool> visible{false};
    Signal<SequencerStepPresetPickerMode> mode{
        SequencerStepPresetPickerMode::LOAD
    };
    Signal<uint8_t> selectedIndex{0};
    Signal<uint8_t> entryCount{0};
    Signal<bool> truncated{false};
    Signal<SequencerStepPresetFeedback> feedback{
        SequencerStepPresetFeedback::NONE
    };
    Signal<uint32_t> revision{0};
    std::array<std::array<char, ID_SIZE>, ENTRY_CAPACITY> entryIds{};

    void open(SequencerStepPresetPickerMode nextMode);
    void reset();
    void setFeedback(SequencerStepPresetFeedback nextFeedback);
    void setEntry(uint8_t index, const char* id);
    const char* entryId(uint8_t index) const;
    uint8_t itemCount() const;
    uint8_t existingEntryIndexForSelectedItem() const;
    void clampSelection();
};

struct SequencerStepPropertyInlineSelectorState {
    Signal<bool, 6> selecting{false};
    Signal<bool, 6> macroLocalVariationEditActive{false};
    Signal<int, 4> selectedIndex{0};

    int snapshotIndex = 0;
    uint8_t localVariationStepIndex = 0;
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
    static constexpr uint32_t DISPLAY_HOLD_MS = 700;

    Signal<bool, 6> selecting{false};
    Signal<bool, 6> physicalHoldActive{false};
    Signal<bool, 6> feedbackVisible{false};
    Signal<PatternQuickControlItem, 6> focusedItem{
        PatternQuickControlItem::LENGTH
    };
    Signal<int8_t, 4> offsetSteps{0};
    uint32_t hideAtMs = 0;

    SequencerPatternQuickControlsState();

    void showFeedback(uint32_t nowMs);

    void update(uint32_t nowMs) {
        if (!feedbackVisible.get()) return;
        if (selecting.get()) return;
        if (nowMs < hideAtMs) return;
        feedbackVisible.set(false);
        hideAtMs = 0;
    }

    void reset();
};

enum class SequencerStepPastePreview : uint8_t {
    NONE = 0,
    EMPTY,
    OVERWRITE,
    GHOST,
    BLOCKED,
};

struct SequencerStepSelectionState {
    Signal<bool, 8> active{false};
    Signal<uint8_t, 8> cursorStep{0};
    Signal<oc::note::sequencer::StepBitMask128, 8> selectedMask{};
    Signal<bool, 8> pastePreviewActive{false};
    Signal<SequencerStepPastePreview, 8> pastePreview{SequencerStepPastePreview::NONE};

    void reset(uint8_t cursor = 0);

    void setSelected(uint8_t step, bool selected);
    bool selected(uint8_t step) const;
    bool anySelected() const {
        return selectedMask.get().any();
    }
};

struct SequencerStructureUiState {
    Signal<bool, 4> previewAddPageSlot{false};
    Signal<uint8_t, 4> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;
    SequencerStepSelectionState stepSelection;

    SequencerStructureUiState();
    ~SequencerStructureUiState();

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset();
};

}  // namespace core::state::sequencer

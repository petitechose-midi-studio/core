#include "state/sequencer/SequencerUiState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM SequencerPatternQuickControlsState::SequencerPatternQuickControlsState() = default;

FLASHMEM void SequencerContentViewState::reset() {
    kind.set(SequencerContentViewKind::ROOT);
    parentStep.set(0);
    sequenceId.set(GraphLimits::INVALID_ID);
    length.set(0);
    rootPageSnapshot = 0;
    rootFocusSnapshot = 0;
    bump();
}

FLASHMEM void SequencerStepEditOverlayState::reset() {
    stepIndex.set(0);
    focusedRow.set(0);
    contentSession.reset();
    snapshotValid = false;
}

FLASHMEM void SequencerStepPropertyInlineSelectorState::reset() {
    selecting.set(false);
    selectedIndex.set(0);
    snapshotValid = false;
}

FLASHMEM void SequencerStepInlineFeedbackState::show(
    uint8_t step,
    StepProperty stepProperty,
    uint32_t nowMs
) {
    if (step >= MAX_STEPS) return;

    auto mask = touchedMask.get();
    mask.setBit(step, true);
    touchedMask.set(mask);
    property.set(stepProperty);
    hideAtMs[step] = nowMs + DISPLAY_HOLD_MS;
    visible.set(true);
}

FLASHMEM void SequencerStepInlineFeedbackState::reset() {
    visible.set(false);
    touchedMask.set({});
    property.set(StepProperty::NOTE);
    for (auto& value : hideAtMs) {
        value = 0;
    }
}

FLASHMEM void SequencerPatternVariationFeedbackState::show(
    StepProperty stepProperty,
    uint32_t nowMs
) {
    property.set(stepProperty);
    hideAtMs = nowMs + DISPLAY_HOLD_MS;
    visible.set(true);
}

FLASHMEM void SequencerPatternVariationFeedbackState::reset() {
    visible.set(false);
    property.set(StepProperty::NOTE);
    hideAtMs = 0;
}

FLASHMEM void SequencerHistoryFeedbackState::show(
    const char* nextLine1,
    const char* nextLine2,
    const char* nextLine3,
    uint32_t nowMs
) {
    copyLine(line1, nextLine1);
    copyLine(line2, nextLine2);
    copyLine(line3, nextLine3);
    hideAtMs = nowMs + DISPLAY_HOLD_MS;
    revision.set(revision.get() + 1);
    visible.set(true);
}

FLASHMEM void SequencerHistoryFeedbackState::reset() {
    visible.set(false);
    hideAtMs = 0;
    copyLine(line1, "");
    copyLine(line2, "");
    copyLine(line3, "");
    revision.set(revision.get() + 1);
}

FLASHMEM void SequencerPatternQuickControlsState::reset() {
    selecting.set(false);
    physicalHoldActive.set(false);
    offsetSteps.set(0);
}

FLASHMEM SequencerStructureUiState::SequencerStructureUiState() = default;
FLASHMEM SequencerStructureUiState::~SequencerStructureUiState() = default;

FLASHMEM void SequencerStructureUiState::reset() {
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
}

}  // namespace core::state::sequencer

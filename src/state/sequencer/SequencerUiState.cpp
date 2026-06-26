#include "state/sequencer/SequencerUiState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM SequencerPatternQuickControlsState::SequencerPatternQuickControlsState() = default;

FLASHMEM void SequencerContentViewState::reset() {
    kind.set(SequencerContentViewKind::ROOT);
    parentStep.set(0);
    ownerNodeId.set(GraphLimits::INVALID_ID);
    sequenceId.set(GraphLimits::INVALID_ID);
    cycleSetId.set(GraphLimits::INVALID_ID);
    length.set(0);
    depth.set(0);
    rootPageSnapshot = 0;
    rootFocusSnapshot = 0;
    stackDepth = 0;
    frames = {};
    bump();
}

FLASHMEM void SequencerChordEditorState::reset() {
    active.set(false);
    focusedField.set(SequencerChordEditField::MODE);
}

FLASHMEM void SequencerStepEditOverlayState::reset() {
    stepIndex.set(0);
    focusedRow.set(0);
    localVariationEditActive.set(false);
    chordEditor.reset();
    contextHold.clear();
}

FLASHMEM void SequencerStepPresetPickerState::open(
    SequencerStepPresetPickerMode nextMode
) {
    mode.set(nextMode);
    selectedIndex.set(0);
    feedback.set(SequencerStepPresetFeedback::NONE);
    revision.set(revision.get() + 1U);
    visible.set(true);
}

FLASHMEM void SequencerStepPresetPickerState::reset() {
    visible.set(false);
    mode.set(SequencerStepPresetPickerMode::LOAD);
    selectedIndex.set(0);
    entryCount.set(0);
    truncated.set(false);
    feedback.set(SequencerStepPresetFeedback::NONE);
    for (auto& id : entryIds) {
        id[0] = '\0';
    }
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerStepPresetPickerState::setFeedback(
    SequencerStepPresetFeedback nextFeedback
) {
    feedback.set(nextFeedback);
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerStepPresetPickerState::setEntry(uint8_t index, const char* id) {
    if (index >= ENTRY_CAPACITY) return;
    const char* source = id ? id : "";
    std::strncpy(entryIds[index].data(), source, ID_SIZE - 1U);
    entryIds[index][ID_SIZE - 1U] = '\0';
}

FLASHMEM const char* SequencerStepPresetPickerState::entryId(uint8_t index) const {
    return index < ENTRY_CAPACITY ? entryIds[index].data() : "";
}

FLASHMEM uint8_t SequencerStepPresetPickerState::itemCount() const {
    const uint8_t existing = entryCount.get();
    if (mode.get() == SequencerStepPresetPickerMode::SAVE) {
        const uint16_t withNew = static_cast<uint16_t>(existing) + 1U;
        return withNew > 255U ? 255U : static_cast<uint8_t>(withNew);
    }
    return existing;
}

FLASHMEM uint8_t SequencerStepPresetPickerState::existingEntryIndexForSelectedItem() const {
    const uint8_t selected = selectedIndex.get();
    if (mode.get() == SequencerStepPresetPickerMode::SAVE) {
        return selected == 0 ? 0 : static_cast<uint8_t>(selected - 1U);
    }
    return selected;
}

FLASHMEM void SequencerStepPresetPickerState::clampSelection() {
    const uint8_t count = itemCount();
    if (count == 0) {
        selectedIndex.set(0);
        return;
    }
    if (selectedIndex.get() >= count) {
        selectedIndex.set(static_cast<uint8_t>(count - 1U));
    }
}

FLASHMEM void SequencerStepPropertyInlineSelectorState::reset() {
    selecting.set(false);
    macroLocalVariationEditActive.set(false);
    selectedIndex.set(0);
    localVariationStepIndex = 0;
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

FLASHMEM void SequencerPatternQuickControlsState::showFeedback(uint32_t nowMs) {
    hideAtMs = nowMs + DISPLAY_HOLD_MS;
    feedbackVisible.set(true);
}

FLASHMEM void SequencerPatternQuickControlsState::reset() {
    selecting.set(false);
    physicalHoldActive.set(false);
    feedbackVisible.set(false);
    focusedItem.set(PatternQuickControlItem::LENGTH);
    offsetSteps.set(0);
    hideAtMs = 0;
}

FLASHMEM void SequencerStepSelectionState::reset(uint8_t cursor) {
    active.set(false);
    cursorStep.set(cursor);
    selectedMask.set({});
    pastePreviewActive.set(false);
    pastePreview.set(SequencerStepPastePreview::NONE);
}

FLASHMEM void SequencerStepSelectionState::setSelected(uint8_t step, bool selected) {
    auto mask = selectedMask.get();
    mask.setBit(step, selected);
    selectedMask.set(mask);
}

FLASHMEM bool SequencerStepSelectionState::selected(uint8_t step) const {
    return selectedMask.get().test(step);
}

FLASHMEM SequencerStructureUiState::SequencerStructureUiState() = default;
FLASHMEM SequencerStructureUiState::~SequencerStructureUiState() = default;

FLASHMEM void SequencerStructureUiState::reset() {
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    stepSelection.reset();
}

}  // namespace core::state::sequencer

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
    detailVisible.set(false);
    detailFocus.set(0);
    inspecting.set(false);
    previewStateIndex.set(0);
    operationActivationGeneration = 0;
    actionGuard.set({});
    operationFeedback.set({});
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
    hasPreviousPage.set(false);
    hasNextPage.set(false);
    totalEntryCount.set(0);
    detailVisible.set(false);
    detailFocus.set(0);
    inspecting.set(false);
    previewStateIndex.set(0);
    previewGeneration.set(0);
    feedback.set(SequencerStepPresetFeedback::NONE);
    actionGuard.set({});
    operationFeedback.set({});
    for (uint8_t i = 0; i < ENTRY_CAPACITY; ++i) {
        entryIds[i][0] = '\0';
        entryNames[i][0] = '\0';
        entryMetadataReadable[i] = false;
    }
    frozenTarget = {};
    descriptor = {};
    operationActivationGeneration = 0;
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerStepPresetPickerState::setFeedback(
    SequencerStepPresetFeedback nextFeedback
) {
    feedback.set(nextFeedback);
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerStepPresetPickerState::setEntry(
    uint8_t index,
    const char* id,
    const char* semanticName,
    bool metadataReadable
) {
    if (index >= ENTRY_CAPACITY) return;
    const char* source = id ? id : "";
    std::strncpy(entryIds[index].data(), source, ID_SIZE - 1U);
    entryIds[index][ID_SIZE - 1U] = '\0';
    source = semanticName ? semanticName : "";
    std::strncpy(entryNames[index].data(), source, NAME_SIZE - 1U);
    entryNames[index][NAME_SIZE - 1U] = '\0';
    entryMetadataReadable[index] = metadataReadable;
}

FLASHMEM const char* SequencerStepPresetPickerState::entryId(uint8_t index) const {
    return index < ENTRY_CAPACITY ? entryIds[index].data() : "";
}

FLASHMEM const char* SequencerStepPresetPickerState::entryName(uint8_t index) const {
    return index < ENTRY_CAPACITY ? entryNames[index].data() : "";
}

FLASHMEM bool SequencerStepPresetPickerState::entryHasReadableMetadata(
    uint8_t index
) const {
    return index < ENTRY_CAPACITY && entryMetadataReadable[index];
}

FLASHMEM uint8_t SequencerStepPresetPickerState::itemCount() const {
    const uint8_t existing = entryCount.get();
    const uint8_t offset = newAssetItemOffset();
    if (offset > 0) {
        const uint16_t withNew = static_cast<uint16_t>(existing) + offset;
        return withNew > 255U ? 255U : static_cast<uint8_t>(withNew);
    }
    return existing;
}

FLASHMEM uint8_t SequencerStepPresetPickerState::newAssetItemOffset() const {
    return mode.get() == SequencerStepPresetPickerMode::SAVE &&
           !hasPreviousPage.get() ? 1U : 0U;
}

FLASHMEM bool SequencerStepPresetPickerState::selectedItemIsNewAsset() const {
    return newAssetItemOffset() > 0 && selectedIndex.get() == 0;
}

FLASHMEM uint8_t SequencerStepPresetPickerState::existingEntryIndexForSelectedItem() const {
    const uint8_t selected = selectedIndex.get();
    const uint8_t offset = newAssetItemOffset();
    return selected < offset ? 0 : static_cast<uint8_t>(selected - offset);
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

FLASHMEM void SequencerStepPresetPickerState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerCcLaneUiState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerCcLaneUiState::reset() {
    overlayVisible.set(false);
    mode = SequencerCcLaneUiMode::CLOSED;
    selectorIndex = 0;
    focusedLane = 0;
    focusedStep = 0;
    focusedField = SequencerCcLaneDraftField::CONTROLLER;
    draft = {};
    draftDirty = false;
    hasAuthoredValue = false;
    authoredValue = 0;
    hasResolvedValue = false;
    resolvedValue = 0;
    winnerClass = core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE;
    routeValid = true;
    laneConflict = false;
    macroConflict = false;
    acceptedMacroConflict = false;
    liveProjection = false;
    actions = {};
    actionGuard.set({});
    operationFeedback.set({});
    bump();
}

FLASHMEM void SequencerStepPropertyInlineSelectorState::reset() {
    selecting.set(false);
    macroLocalVariationEditActive.set(false);
    selectedIndex.set(0);
    localVariationStepIndex = 0;
    snapshotValid = false;
    suppressOpeningRelease = false;
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

FLASHMEM void SequencerTrackPasteUiState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerTrackPasteUiState::reset() {
    guard = {};
    feedback = {};
    plan = {};
    clipboardKind = core::state::StructureClipboardKind::NONE;
    clipboardRevision = 0;
    interactionGeneration = 0;
    operationGeneration = 0;
    activationGeneration = 0;
    focusedIndex = 0;
    detailVisible = false;
    buttonOwned = false;
    commitConsumed = false;
    selectionContext = false;
    bump();
}

FLASHMEM SequencerStructureUiState::SequencerStructureUiState() = default;
FLASHMEM SequencerStructureUiState::~SequencerStructureUiState() = default;

FLASHMEM void SequencerStructureUiState::reset() {
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    stepSelection.reset();
    trackPaste.reset();
}

}  // namespace core::state::sequencer

#include "state/sequencer/SequencerUiState.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

namespace core::state::sequencer {

namespace {

FLASHMEM uint32_t clipWorkspaceFeedbackDeadline(
    ClipWorkspaceFeedback feedback,
    uint32_t nowMs
) {
    if (feedback == ClipWorkspaceFeedback::NONE) return 0U;
    const uint32_t duration = feedback == ClipWorkspaceFeedback::FAILED
        ? Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
        : Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS;
    return nowMs + duration;
}

FLASHMEM void keepClipWorkspaceTrackVisible(ClipWorkspaceUiState& state) {
    if (state.focusedTrack < state.firstVisibleTrack) {
        state.firstVisibleTrack = state.focusedTrack;
    } else if (state.focusedTrack >=
               state.firstVisibleTrack + ClipWorkspaceUiState::VISIBLE_TRACKS) {
        state.firstVisibleTrack = static_cast<uint8_t>(
            state.focusedTrack - ClipWorkspaceUiState::VISIBLE_TRACKS + 1U
        );
    }
    state.firstVisibleTrack = std::min<uint8_t>(
        state.firstVisibleTrack,
        ClipWorkspaceUiState::TRACK_COUNT -
            ClipWorkspaceUiState::VISIBLE_TRACKS
    );
}

FLASHMEM void keepClipWorkspaceSlotVisible(ClipWorkspaceUiState& state) {
    if (state.focusedSlot < state.firstVisibleSlot) {
        state.firstVisibleSlot = state.focusedSlot;
    } else if (state.focusedSlot >=
               state.firstVisibleSlot + ClipWorkspaceUiState::VISIBLE_ROWS) {
        state.firstVisibleSlot = static_cast<uint8_t>(
            state.focusedSlot - ClipWorkspaceUiState::VISIBLE_ROWS + 1U
        );
    }
    state.firstVisibleSlot = std::min<uint8_t>(
        state.firstVisibleSlot,
        ClipWorkspaceUiState::SLOT_COUNT - ClipWorkspaceUiState::VISIBLE_ROWS
    );
}

FLASHMEM void focusClipWithoutPublishing(
    ClipWorkspaceUiState& state,
    uint8_t track,
    uint8_t slot
) {
    state.focusedTrack = std::min<uint8_t>(
        track,
        ClipWorkspaceUiState::TRACK_COUNT - 1U
    );
    state.focusedSlot = std::min<uint8_t>(
        slot,
        ClipWorkspaceUiState::SLOT_COUNT - 1U
    );
    state.focusArea = ClipWorkspaceFocus::CLIP;
    keepClipWorkspaceTrackVisible(state);
    keepClipWorkspaceSlotVisible(state);
    state.feedback = ClipWorkspaceFeedback::NONE;
    state.feedbackHideAtMs = 0U;
}

FLASHMEM void clearQuickControlWithoutPublishing(ClipWorkspaceUiState& state) {
    state.quickAction = ClipWorkspaceQuickAction::EDIT;
    state.quickSelectorVisible = false;
    state.quickPropertyArmed = false;
    state.quickFeedbackVisible = false;
    state.quickFeedbackHideAtMs = 0U;
}

}  // namespace

FLASHMEM SequencerPatternQuickControlsState::SequencerPatternQuickControlsState() = default;
FLASHMEM SequencerPatternQuickControlsState::~SequencerPatternQuickControlsState() = default;

FLASHMEM void SequencerPatternQuickControlsState::bumpPreview() {
    previewRevision.set(previewRevision.get() + 1U);
}

FLASHMEM SequencerContentViewState::SequencerContentViewState() = default;
FLASHMEM SequencerContentViewState::~SequencerContentViewState() = default;

FLASHMEM SequencerStepEditOverlayState::SequencerStepEditOverlayState() = default;
FLASHMEM SequencerStepEditOverlayState::~SequencerStepEditOverlayState() = default;

FLASHMEM SequencerPresetLibrarySessionState::
SequencerPresetLibrarySessionState() = default;
FLASHMEM SequencerPresetLibrarySessionState::
~SequencerPresetLibrarySessionState() = default;

FLASHMEM SequencerStepSelectionState::SequencerStepSelectionState() = default;
FLASHMEM SequencerStepSelectionState::~SequencerStepSelectionState() = default;

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
    drumOwnerActive = false;
    drumOwnerTrack = 0;
    drumOwnerLane = 0;
    drumOwnerStep = 0;
    drumOwnerRootSlot = 0xFFU;
    frames = {};
    bump();
}

FLASHMEM void SequencerChordEditorState::reset() {
    active.set(false);
    focusedField.set(SequencerChordEditField::SHAPE);
    subEditor.set({});
    formulaSnapshot.reset();
}

FLASHMEM void SequencerStepEditOverlayState::reset() {
    stepIndex.set(0);
    focusedRow.set(0);
    localVariationEditActive.set(false);
    drumContext = false;
    drumLane = 0;
    drumStep = 0;
    drumRootSlot = 0xFFU;
    chordEditor.reset();
    contextHold.clear();
}

FLASHMEM void SequencerContextSelectorState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerContextSelectorState::reset() {
    visible = false;
    previewFocus = core::state::StructureNavigationFocus::PAGE;
    bump();
}

FLASHMEM void SequencerPresetLibrarySessionState::open(
    SequencerPresetLibraryMode nextMode,
    SequencerPresetLibraryKind kind
) {
    clearCatalog();
    libraryKind.set(kind);
    mode.set(nextMode);
    selectedIndex.set(0);
    detailVisible.set(false);
    detailFocus.set(0);
    inspecting.set(false);
    previewStateIndex.set(0);
    previewGeneration.set(0);
    if (kind == SequencerPresetLibraryKind::CHORD) {
        payload.emplace<SequencerChordPresetLibraryState>();
    } else if (kind == SequencerPresetLibraryKind::PATTERN) {
        payload.emplace<SequencerPatternPresetLibraryState>();
    } else {
        payload.emplace<SequencerStepPresetLibraryState>();
    }
    actionGuard.set({});
    operationFeedback.set({});
    feedback.set(SequencerPresetLibraryFeedback::NONE);
    revision.set(revision.get() + 1U);
    visible.set(true);
}

FLASHMEM void SequencerPresetLibrarySessionState::reset() {
    visible.set(false);
    libraryKind.set(SequencerPresetLibraryKind::STEP);
    mode.set(SequencerPresetLibraryMode::LOAD);
    selectedIndex.set(0);
    clearCatalog();
    detailVisible.set(false);
    detailFocus.set(0);
    inspecting.set(false);
    previewStateIndex.set(0);
    previewGeneration.set(0);
    feedback.set(SequencerPresetLibraryFeedback::NONE);
    actionGuard.set({});
    operationFeedback.set({});
    payload.emplace<SequencerStepPresetLibraryState>();
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerPresetLibrarySessionState::clearCatalog() {
    entryCount.set(0);
    truncated.set(false);
    hasPreviousPage.set(false);
    hasNextPage.set(false);
    totalEntryCount.set(0);
    for (uint8_t i = 0; i < ENTRY_CAPACITY; ++i) {
        entryIds[i][0] = '\0';
        entryNames[i][0] = '\0';
        entryValues[i][0] = '\0';
        entryMetadataReadable[i] = false;
        entryKinds[i] = SequencerPresetLibraryEntryKind::ASSET;
    }
}

FLASHMEM void SequencerPresetLibrarySessionState::setFeedback(
    SequencerPresetLibraryFeedback nextFeedback
) {
    feedback.set(nextFeedback);
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerPresetLibrarySessionState::setEntry(
    uint8_t index,
    const char* id,
    const char* semanticName,
    bool metadataReadable,
    SequencerPresetLibraryEntryKind kind,
    const char* displayValue
) {
    if (index >= ENTRY_CAPACITY) return;
    const char* source = id ? id : "";
    std::strncpy(entryIds[index].data(), source, ID_SIZE - 1U);
    entryIds[index][ID_SIZE - 1U] = '\0';
    source = semanticName ? semanticName : "";
    std::strncpy(entryNames[index].data(), source, NAME_SIZE - 1U);
    entryNames[index][NAME_SIZE - 1U] = '\0';
    source = displayValue ? displayValue : "";
    std::strncpy(
        entryValues[index].data(),
        source,
        entryValues[index].size() - 1U
    );
    entryValues[index][entryValues[index].size() - 1U] = '\0';
    entryMetadataReadable[index] = metadataReadable;
    entryKinds[index] = kind;
}

FLASHMEM const char* SequencerPresetLibrarySessionState::entryValue(
    uint8_t index
) const {
    return index < ENTRY_CAPACITY ? entryValues[index].data() : "";
}

FLASHMEM const char* SequencerPresetLibrarySessionState::entryId(
    uint8_t index
) const {
    return index < ENTRY_CAPACITY ? entryIds[index].data() : "";
}

FLASHMEM SequencerPresetLibraryEntryKind
SequencerPresetLibrarySessionState::entryKind(uint8_t index) const {
    return index < ENTRY_CAPACITY
        ? entryKinds[index]
        : SequencerPresetLibraryEntryKind::ASSET;
}

FLASHMEM const char* SequencerPresetLibrarySessionState::entryName(
    uint8_t index
) const {
    return index < ENTRY_CAPACITY ? entryNames[index].data() : "";
}

FLASHMEM bool SequencerPresetLibrarySessionState::entryHasReadableMetadata(
    uint8_t index
) const {
    return index < ENTRY_CAPACITY && entryMetadataReadable[index];
}

FLASHMEM SequencerStepPresetLibraryState&
SequencerPresetLibrarySessionState::step() {
    return *std::get_if<SequencerStepPresetLibraryState>(&payload);
}

FLASHMEM const SequencerStepPresetLibraryState&
SequencerPresetLibrarySessionState::step() const {
    return *std::get_if<SequencerStepPresetLibraryState>(&payload);
}

FLASHMEM SequencerChordPresetLibraryState&
SequencerPresetLibrarySessionState::chord() {
    return *std::get_if<SequencerChordPresetLibraryState>(&payload);
}

FLASHMEM const SequencerChordPresetLibraryState&
SequencerPresetLibrarySessionState::chord() const {
    return *std::get_if<SequencerChordPresetLibraryState>(&payload);
}

FLASHMEM SequencerPatternPresetLibraryState&
SequencerPresetLibrarySessionState::pattern() {
    return *std::get_if<SequencerPatternPresetLibraryState>(&payload);
}

FLASHMEM const SequencerPatternPresetLibraryState&
SequencerPresetLibrarySessionState::pattern() const {
    return *std::get_if<SequencerPatternPresetLibraryState>(&payload);
}

FLASHMEM uint8_t SequencerPresetLibrarySessionState::itemCount() const {
    const uint8_t existing = entryCount.get();
    const uint8_t offset = newAssetItemOffset();
    if (offset > 0) {
        const uint16_t withNew = static_cast<uint16_t>(existing) + offset;
        return withNew > 255U ? 255U : static_cast<uint8_t>(withNew);
    }
    return existing;
}

FLASHMEM uint8_t
SequencerPresetLibrarySessionState::newAssetItemOffset() const {
    if (libraryKind.get() == SequencerPresetLibraryKind::PATTERN &&
        pattern().panel ==
            SequencerPatternPresetLibraryPanel::MOVE_DESTINATION) {
        return 1U;
    }
    // Save-new is a first-class command, not an asset belonging to a
    // particular catalog page. Keep it reachable from every Save page.
    if (mode.get() != SequencerPresetLibraryMode::SAVE) return 0U;
    return libraryKind.get() == SequencerPresetLibraryKind::PATTERN ? 2U : 1U;
}

FLASHMEM bool
SequencerPresetLibrarySessionState::selectedItemIsNewAsset() const {
    return newAssetItemOffset() > 0 && selectedIndex.get() == 0 &&
        !(libraryKind.get() == SequencerPresetLibraryKind::PATTERN &&
          pattern().panel ==
              SequencerPatternPresetLibraryPanel::MOVE_DESTINATION);
}

FLASHMEM bool
SequencerPresetLibrarySessionState::selectedItemIsNewFolder() const {
    return mode.get() == SequencerPresetLibraryMode::SAVE &&
           libraryKind.get() == SequencerPresetLibraryKind::PATTERN &&
           pattern().panel == SequencerPatternPresetLibraryPanel::BROWSE &&
           selectedIndex.get() == 1U;
}

FLASHMEM bool
SequencerPresetLibrarySessionState::selectedItemIsExistingAsset() const {
    return entryCount.get() > 0U &&
           !selectedItemIsNewAsset() &&
           existingEntryIndexForSelectedItem() < entryCount.get();
}

FLASHMEM uint8_t
SequencerPresetLibrarySessionState::
existingEntryIndexForSelectedItem() const {
    const uint8_t selected = selectedIndex.get();
    const uint8_t offset = newAssetItemOffset();
    return selected < offset ? 0 : static_cast<uint8_t>(selected - offset);
}

FLASHMEM void
SequencerPresetLibrarySessionState::clampSelection() {
    const uint8_t count = itemCount();
    if (count == 0) {
        selectedIndex.set(0);
        return;
    }
    if (selectedIndex.get() >= count) {
        selectedIndex.set(static_cast<uint8_t>(count - 1U));
    }
}

FLASHMEM void SequencerPresetLibrarySessionState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM void SequencerPatternPresetPreviewUiState::begin(
    const SequencerPatternPresetTarget& nextTarget,
    const char* semanticName,
    bool waitsForLoop
) {
    target = nextTarget;
    name.fill('\0');
    std::strncpy(
        name.data(),
        semanticName != nullptr ? semanticName : "Pattern",
        name.size() - 1U
    );
    phase = waitsForLoop
        ? SequencerPatternPresetPreviewPhase::NEXT_LOOP
        : SequencerPatternPresetPreviewPhase::PREVIEW;
    bump();
}

FLASHMEM void SequencerPatternPresetPreviewUiState::setQueued(
    bool waitsForLoop
) {
    if (!active()) return;
    const auto next = waitsForLoop
        ? SequencerPatternPresetPreviewPhase::NEXT_LOOP
        : SequencerPatternPresetPreviewPhase::PREVIEW;
    if (phase == next) return;
    phase = next;
    bump();
}

FLASHMEM void SequencerPatternPresetPreviewUiState::reset() {
    phase = SequencerPatternPresetPreviewPhase::INACTIVE;
    target = {};
    name.fill('\0');
    bump();
}

FLASHMEM void SequencerPatternPresetPreviewUiState::bump() {
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
    transitionStep = 0;
    selectedTransition = SequencerCcLaneTransition::HOLD;
    compactTransitionPicker = false;
    transitionAppliedFeedback = false;
    focusedField = SequencerCcLaneDraftField::CONTROLLER;
    draft = {};
    draftDirty = false;
    advancedSettings = false;
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

FLASHMEM void SequencerStepContentSelectorState::reset() {
    selecting.set(false);
    focusedAction.set(SequencerStepContentAction::CHORD);
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

FLASHMEM void SequencerHistoryFeedbackState::showRejection(SequencerHistoryRejectionReason reason,
                                                           uint32_t nowMs) {
    const char* detail = "Edit unavailable";
    switch (reason) {
        case SequencerHistoryRejectionReason::ResourceUnavailable:
            detail = "Memory unavailable";
            break;
        case SequencerHistoryRejectionReason::HistoryUnavailable:
            detail = "History unavailable";
            break;
        case SequencerHistoryRejectionReason::Blocked: break;
    }
    show("No change", detail, "", nowMs);
}

FLASHMEM void SequencerHistoryFeedbackState::showRejection(SequencerHistoryOpenOutcome outcome,
                                                           uint32_t nowMs) {
    showRejection(sequencerHistoryRejectionFor(outcome), nowMs);
}

FLASHMEM void SequencerHistoryFeedbackState::showRejection(SequencerHistoryGestureOutcome outcome,
                                                           uint32_t nowMs) {
    showRejection(sequencerHistoryRejectionFor(outcome), nowMs);
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
    feedbackVisible.set(false);
    focusedItem.set(PatternQuickControlItem::LENGTH);
    offsetSteps.set(0);
    previewRevision.set(0);
    hideAtMs = 0;
}

FLASHMEM void SequencerStepSelectionState::reset(uint8_t cursor) {
    active.set(false);
    placing.set(false);
    cursorStep.set(cursor);
    selectedMask.set({});
    pastePreviewActive.set(false);
    pastePreview.set(SequencerStepPastePreview::NONE);
    clipboardRevision.set(0U);
}

FLASHMEM void SequencerStepSelectionState::clearCurrent() {
    placing.set(false);
    selectedMask.set({});
    pastePreviewActive.set(false);
    pastePreview.set(SequencerStepPastePreview::NONE);
    clipboardRevision.set(0U);
}

FLASHMEM void SequencerStepSelectionState::setSelected(uint8_t step, bool selected) {
    auto mask = selectedMask.get();
    mask.setBit(step, selected);
    selectedMask.set(mask);
}

FLASHMEM bool SequencerStepSelectionState::selected(uint8_t step) const {
    return selectedMask.get().test(step);
}

FLASHMEM void ClipWorkspaceUiState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM uint8_t ClipWorkspaceUiState::addTrackIndex(
    uint16_t enabledTrackMask
) {
    for (int track = TRACK_COUNT - 1; track >= 0; --track) {
        if ((enabledTrackMask & static_cast<uint16_t>(1U << track)) == 0U) {
            continue;
        }
        return track + 1 < TRACK_COUNT
            ? static_cast<uint8_t>(track + 1)
            : INVALID_TRACK;
    }
    return 0U;
}

FLASHMEM bool ClipWorkspaceUiState::trackNavigable(
    uint8_t track,
    uint16_t enabledTrackMask
) {
    if (track >= TRACK_COUNT) return false;
    return (enabledTrackMask & static_cast<uint16_t>(1U << track)) != 0U ||
        track == addTrackIndex(enabledTrackMask);
}

FLASHMEM uint8_t ClipWorkspaceUiState::macroBankFirstSlot() const {
    const uint8_t local = focusedSlot >= firstVisibleSlot
        ? static_cast<uint8_t>(focusedSlot - firstVisibleSlot)
        : 0U;
    const uint8_t bankOffset = local >= MACRO_ROWS ? MACRO_ROWS : 0U;
    return static_cast<uint8_t>(firstVisibleSlot + bankOffset);
}

FLASHMEM void ClipWorkspaceUiState::reset(uint8_t activeTrack) {
    route = ClipWorkspaceRoute::MATRIX;
    feedback = ClipWorkspaceFeedback::NONE;
    operation = ClipWorkspaceOperation::BROWSE;
    focusArea = ClipWorkspaceFocus::SCENE;
    quickAction = ClipWorkspaceQuickAction::EDIT;
    quickSelectorVisible = false;
    quickPropertyArmed = false;
    quickFeedbackVisible = false;
    quickTargetFocus = ClipWorkspaceFocus::CLIP;
    quickTargetTrack = std::min<uint8_t>(activeTrack, TRACK_COUNT - 1U);
    quickTargetSlot = 0U;
    quickFeedbackHideAtMs = 0U;
    feedbackHideAtMs = 0U;
    editor = ClipWorkspaceEditor::NONE;
    editorField = ClipWorkspaceBehaviorField::LENGTH;
    slotAction = ClipWorkspaceSlotAction::CREATE_CLIP;
    editorLength = 0U;
    editorFollowChoice = 0xFFU;
    editorQuantization = 0U;
    focusedTrack = std::min<uint8_t>(activeTrack, TRACK_COUNT - 1U);
    focusedSlot = 0U;
    firstVisibleTrack = focusedTrack >= VISIBLE_TRACKS
        ? static_cast<uint8_t>(focusedTrack - VISIBLE_TRACKS + 1U)
        : 0U;
    firstVisibleSlot = 0U;
    returnTrack = focusedTrack;
    returnSlot = focusedSlot;
    sourceTrack = focusedTrack;
    sourceSlot = focusedSlot;
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::focus(
    uint8_t track,
    uint8_t slot
) {
    const uint8_t nextTrack = std::min<uint8_t>(track, TRACK_COUNT - 1U);
    const uint8_t nextSlot = std::min<uint8_t>(slot, SLOT_COUNT - 1U);
    const bool changed = focusedTrack != nextTrack || focusedSlot != nextSlot ||
        focusArea != ClipWorkspaceFocus::CLIP ||
        feedback != ClipWorkspaceFeedback::NONE;
    focusClipWithoutPublishing(*this, nextTrack, nextSlot);
    if (changed) bump();
}

FLASHMEM void ClipWorkspaceUiState::focusScene(uint8_t slot) {
    slot = std::min<uint8_t>(slot, SLOT_COUNT - 1U);
    const bool changed = focusedSlot != slot ||
        focusArea != ClipWorkspaceFocus::SCENE ||
        feedback != ClipWorkspaceFeedback::NONE;
    focusedSlot = slot;
    focusArea = ClipWorkspaceFocus::SCENE;
    keepClipWorkspaceSlotVisible(*this);
    feedback = ClipWorkspaceFeedback::NONE;
    feedbackHideAtMs = 0U;
    if (changed) bump();
}

FLASHMEM void ClipWorkspaceUiState::focusTrackHeader(uint8_t track) {
    track = std::min<uint8_t>(track, TRACK_COUNT - 1U);
    const bool changed = focusedTrack != track ||
        focusArea != ClipWorkspaceFocus::TRACK_HEADER ||
        feedback != ClipWorkspaceFeedback::NONE;
    focusedTrack = track;
    focusArea = ClipWorkspaceFocus::TRACK_HEADER;
    keepClipWorkspaceTrackVisible(*this);
    feedback = ClipWorkspaceFeedback::NONE;
    feedbackHideAtMs = 0U;
    if (changed) bump();
}

FLASHMEM void ClipWorkspaceUiState::showQuickSelector() {
    quickAction = ClipWorkspaceQuickAction::EDIT;
    quickTargetFocus = focusArea;
    quickTargetTrack = focusedTrack;
    quickTargetSlot = focusedSlot;
    quickPropertyArmed = false;
    quickFeedbackVisible = false;
    quickFeedbackHideAtMs = 0U;
    if (quickSelectorVisible) return;
    quickSelectorVisible = true;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::moveQuickAction(int direction) {
    if (!quickSelectorVisible || direction == 0) return;
    constexpr int count = static_cast<int>(ClipWorkspaceQuickAction::COUNT);
    const int current = static_cast<int>(quickAction);
    const int next = ((current + direction) % count + count) % count;
    if (next == current) return;
    quickAction = static_cast<ClipWorkspaceQuickAction>(next);
    bump();
}

FLASHMEM void ClipWorkspaceUiState::armQuickProperty(uint32_t nowMs) {
    quickSelectorVisible = false;
    quickPropertyArmed = quickAction != ClipWorkspaceQuickAction::EDIT;
    quickFeedbackVisible = quickPropertyArmed;
    quickFeedbackHideAtMs = quickPropertyArmed ? nowMs + 700U : 0U;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::showQuickFeedback(uint32_t nowMs) {
    if (!quickPropertyArmed) return;
    quickFeedbackVisible = true;
    quickFeedbackHideAtMs = nowMs + 700U;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::clearQuickControl() {
    if (!quickSelectorVisible && !quickPropertyArmed &&
        !quickFeedbackVisible) {
        return;
    }
    clearQuickControlWithoutPublishing(*this);
    bump();
}

FLASHMEM void ClipWorkspaceUiState::updateQuickFeedback(uint32_t nowMs) {
    if (!quickFeedbackVisible || quickSelectorVisible ||
        static_cast<int32_t>(nowMs - quickFeedbackHideAtMs) < 0) {
        return;
    }
    quickFeedbackVisible = false;
    quickFeedbackHideAtMs = 0U;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::moveVertical(
    int direction,
    uint8_t lastSlot
) {
    if (direction == 0 || editorActive()) return;
    if (placementActive()) {
        const int next = std::clamp(
            static_cast<int>(focusedSlot) + direction,
            0,
            static_cast<int>(SLOT_COUNT - 1U)
        );
        focus(focusedTrack, static_cast<uint8_t>(next));
        return;
    }
    lastSlot = std::min<uint8_t>(lastSlot, SLOT_COUNT - 1U);
    if (sceneFocused()) {
        const int next = std::clamp(
            static_cast<int>(focusedSlot) + direction,
            0,
            static_cast<int>(lastSlot)
        );
        focusScene(static_cast<uint8_t>(next));
        return;
    }
    if (trackHeaderFocused()) {
        if (direction > 0) focus(focusedTrack, firstVisibleSlot);
        return;
    }
    if (direction < 0 && focusedSlot == 0U) {
        focusTrackHeader(focusedTrack);
        return;
    }
    const int next = std::clamp(
        static_cast<int>(focusedSlot) + direction,
        0,
        static_cast<int>(lastSlot)
    );
    focus(focusedTrack, static_cast<uint8_t>(next));
}

FLASHMEM void ClipWorkspaceUiState::moveHorizontal(
    int direction,
    uint16_t enabledTrackMask
) {
    if (direction == 0 || editorActive() ||
        (selectionActive() && !placementActive())) {
        return;
    }
    if (sceneFocused()) {
        if (direction < 0) return;
        for (uint8_t track = firstVisibleTrack; track < TRACK_COUNT; ++track) {
            if (trackNavigable(track, enabledTrackMask)) {
                focus(track, focusedSlot);
                return;
            }
        }
        return;
    }

    const int step = direction < 0 ? -1 : 1;
    for (int track = static_cast<int>(focusedTrack) + step;
         track >= 0 && track < TRACK_COUNT;
         track += step) {
        const bool navigable = placementActive()
            ? (enabledTrackMask & static_cast<uint16_t>(1U << track)) != 0U
            : trackNavigable(static_cast<uint8_t>(track), enabledTrackMask);
        if (!navigable) {
            continue;
        }
        if (trackHeaderFocused()) {
            focusTrackHeader(static_cast<uint8_t>(track));
        } else {
            focus(static_cast<uint8_t>(track), focusedSlot);
        }
        return;
    }
    if (direction < 0 && !placementActive()) focusScene(focusedSlot);
}

FLASHMEM uint8_t ClipWorkspaceUiState::viewportIndex() const {
    return static_cast<uint8_t>(
        (firstVisibleTrack / VISIBLE_TRACKS) * SLOT_VIEWPORT_COUNT +
        firstVisibleSlot / VISIBLE_ROWS
    );
}

FLASHMEM void ClipWorkspaceUiState::moveViewport(int direction) {
    if (direction == 0 || placementActive() || editorActive()) return;
    const int next = std::clamp(
        static_cast<int>(focusedSlot) +
            (direction < 0 ? -static_cast<int>(VISIBLE_ROWS)
                           : static_cast<int>(VISIBLE_ROWS)),
        0,
        static_cast<int>(SLOT_COUNT - 1U)
    );
    if (sceneFocused()) focusScene(static_cast<uint8_t>(next));
    else if (!trackHeaderFocused()) focus(focusedTrack, static_cast<uint8_t>(next));
}

FLASHMEM void ClipWorkspaceUiState::openEditor(
    ClipWorkspaceEditor next,
    uint8_t length,
    uint8_t followChoice,
    uint8_t quantization
) {
    if (next == ClipWorkspaceEditor::NONE) return;
    editor = next;
    editorField = ClipWorkspaceBehaviorField::LENGTH;
    slotAction = ClipWorkspaceSlotAction::CREATE_CLIP;
    editorLength = length;
    editorFollowChoice = followChoice;
    editorQuantization = quantization;
    bump();
}

FLASHMEM bool ClipWorkspaceUiState::closeEditor() {
    if (!editorActive()) return false;
    editor = ClipWorkspaceEditor::NONE;
    bump();
    return true;
}

FLASHMEM void ClipWorkspaceUiState::moveEditorField(int direction) {
    if (!editorActive() || editor == ClipWorkspaceEditor::SLOT_ACTION ||
        direction == 0) {
        return;
    }
    constexpr int count = static_cast<int>(ClipWorkspaceBehaviorField::COUNT);
    int next = static_cast<int>(editorField) + direction;
    next = std::clamp(next, 0, count - 1);
    if (next == static_cast<int>(editorField)) return;
    editorField = static_cast<ClipWorkspaceBehaviorField>(next);
    bump();
}

FLASHMEM void ClipWorkspaceUiState::moveSlotAction(int direction) {
    if (editor != ClipWorkspaceEditor::SLOT_ACTION || direction == 0) return;
    constexpr int count = static_cast<int>(ClipWorkspaceSlotAction::COUNT);
    int next = static_cast<int>(slotAction) + direction;
    next = std::clamp(next, 0, count - 1);
    if (next == static_cast<int>(slotAction)) return;
    slotAction = static_cast<ClipWorkspaceSlotAction>(next);
    bump();
}

FLASHMEM void ClipWorkspaceUiState::setEditorValues(
    uint8_t length,
    uint8_t followChoice,
    uint8_t quantization
) {
    if (editorLength == length && editorFollowChoice == followChoice &&
        editorQuantization == quantization) {
        return;
    }
    editorLength = length;
    editorFollowChoice = followChoice;
    editorQuantization = quantization;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::beginSelection(
    uint8_t track,
    uint8_t slot
) {
    focusClipWithoutPublishing(*this, track, slot);
    sourceTrack = focusedTrack;
    sourceSlot = focusedSlot;
    operation = ClipWorkspaceOperation::SELECT;
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::beginPlacement(
    ClipWorkspaceOperation next,
    uint8_t destinationTrack,
    uint8_t destinationSlot
) {
    if (operation != ClipWorkspaceOperation::SELECT ||
        (next != ClipWorkspaceOperation::MOVE_DESTINATION &&
         next != ClipWorkspaceOperation::DUPLICATE_DESTINATION)) {
        return;
    }
    operation = next;
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    focusClipWithoutPublishing(*this, destinationTrack, destinationSlot);
    bump();
}

FLASHMEM bool ClipWorkspaceUiState::backOperation() {
    if (!selectionActive()) return false;
    if (placementActive()) {
        operation = ClipWorkspaceOperation::SELECT;
        focusClipWithoutPublishing(*this, sourceTrack, sourceSlot);
    } else {
        operation = ClipWorkspaceOperation::BROWSE;
        feedback = ClipWorkspaceFeedback::NONE;
        feedbackHideAtMs = 0U;
    }
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    bump();
    return true;
}

FLASHMEM void ClipWorkspaceUiState::completeOperation(
    uint8_t track,
    uint8_t slot,
    ClipWorkspaceFeedback result,
    uint32_t nowMs
) {
    operation = ClipWorkspaceOperation::BROWSE;
    sourceTrack = std::min<uint8_t>(track, TRACK_COUNT - 1U);
    sourceSlot = std::min<uint8_t>(slot, SLOT_COUNT - 1U);
    focusClipWithoutPublishing(*this, sourceTrack, sourceSlot);
    feedback = result;
    feedbackHideAtMs = clipWorkspaceFeedbackDeadline(result, nowMs);
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::beginRemoveHold(uint32_t nowMs) {
    if (operation != ClipWorkspaceOperation::SELECT ||
        removeHoldActive) {
        return;
    }
    removeHoldStartedAtMs = nowMs;
    removeHoldActive = true;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::beginPendingRemoval() {
    if (operation != ClipWorkspaceOperation::SELECT) return;
    operation = ClipWorkspaceOperation::REMOVE_PENDING;
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::clearRemoveHold() {
    if (!removeHoldActive && removeHoldStartedAtMs == 0U) return;
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    bump();
}

FLASHMEM void ClipWorkspaceUiState::enterPattern(
    uint8_t track,
    uint8_t slot
) {
    returnTrack = std::min<uint8_t>(track, TRACK_COUNT - 1U);
    returnSlot = std::min<uint8_t>(slot, SLOT_COUNT - 1U);
    focusClipWithoutPublishing(*this, returnTrack, returnSlot);
    clearQuickControlWithoutPublishing(*this);
    route = ClipWorkspaceRoute::PATTERN;
    operation = ClipWorkspaceOperation::BROWSE;
    removeHoldStartedAtMs = 0U;
    removeHoldActive = false;
    feedback = ClipWorkspaceFeedback::NONE;
    feedbackHideAtMs = 0U;
    bump();
}

FLASHMEM bool ClipWorkspaceUiState::returnToMatrix() {
    if (matrixVisible()) return false;
    route = ClipWorkspaceRoute::MATRIX;
    operation = ClipWorkspaceOperation::BROWSE;
    focusClipWithoutPublishing(*this, returnTrack, returnSlot);
    clearQuickControlWithoutPublishing(*this);
    feedback = ClipWorkspaceFeedback::NONE;
    feedbackHideAtMs = 0U;
    bump();
    return true;
}

FLASHMEM void ClipWorkspaceUiState::setFeedback(
    ClipWorkspaceFeedback next,
    uint32_t nowMs
) {
    const uint32_t deadline = clipWorkspaceFeedbackDeadline(next, nowMs);
    if (feedback == next && feedbackHideAtMs == deadline) return;
    const bool changed = feedback != next;
    feedback = next;
    feedbackHideAtMs = deadline;
    if (changed) bump();
}

FLASHMEM void ClipWorkspaceUiState::updateFeedback(uint32_t nowMs) {
    if (feedback == ClipWorkspaceFeedback::NONE ||
        static_cast<int32_t>(nowMs - feedbackHideAtMs) < 0) {
        return;
    }
    feedback = ClipWorkspaceFeedback::NONE;
    feedbackHideAtMs = 0U;
    bump();
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
    detailVisible = false;
    buttonOwned = false;
    commitConsumed = false;
    bump();
}

FLASHMEM SequencerStructureUiState::SequencerStructureUiState() = default;
FLASHMEM SequencerStructureUiState::~SequencerStructureUiState() = default;

FLASHMEM void SequencerStructureUiState::reset() {
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    stepSelection.reset();
    trackPaste.reset();
}

}  // namespace core::state::sequencer

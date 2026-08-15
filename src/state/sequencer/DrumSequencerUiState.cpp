#include "state/sequencer/SequencerUiState.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/DrumPatternState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

static_assert(
    DrumSequencerState::RUNTIME_LANE_CAPACITY == DRUM_MAX_LANES,
    "Drum UI/runtime lane projections must share one fixed capacity"
);

FLASHMEM DrumSequencerState::DrumSequencerState() {
    reset();
}
FLASHMEM DrumSequencerState::~DrumSequencerState() = default;

FLASHMEM void DrumLaneContentSelectionState::reset(uint8_t cursor) {
    active = false;
    placing = false;
    pasteBlocked = false;
    moving = false;
    cursorLane = cursor;
    clipboardCount = 0U;
    selectedMask = 0U;
    destinationMask = 0U;
    overwriteMask = 0U;
    clipboardRevision = 0U;
}

FLASHMEM void DrumLaneContentSelectionState::clearCurrent() {
    placing = false;
    pasteBlocked = false;
    moving = false;
    clipboardCount = 0U;
    selectedMask = 0U;
    destinationMask = 0U;
    overwriteMask = 0U;
    clipboardRevision = 0U;
}

FLASHMEM void DrumSequencerState::bump() {
    revision.set(revision.get() + 1U);
}

FLASHMEM void DrumSequencerState::reset() {
    phase = DrumSequencerPhase::INACTIVE;
    selectedKind = DrumSequencerKind::INSTRUMENT;
    selectedKitPreset = DrumKitPreset::EMPTY;
    property = DrumSequencerProperty::VELOCITY;
    dimension = DrumSequencerDimension::LENGTH;
    selector = DrumSequencerSelector::NONE;
    targetTrack = INVALID_TRACK;
    selectedLane = 0;
    laneAddSlotSelected = false;
    laneWindowStart = 0;
    focusedStep = 0;
    page = 0;
    selectorSnapshotProperty = property;
    selectorSnapshotDimension = dimension;
    selectorSnapshotLane = 0;
    selectorSnapshotTimingMode = 0;
    selectorSnapshotLength = 0;
    selectorSnapshotStepsPerBeat = 0;
    laneEditor = {};
    laneSelection.reset();
    patternDefaultField = DrumPatternDefaultField::LENGTH;
    patternDefaultSnapshotLength = DRUM_DEFAULT_LENGTH;
    patternDefaultSnapshotStepsPerBeat = DRUM_DEFAULT_STEPS_PER_BEAT;
    playheadSteps.fill(0U);
    playheadPhasesQ8.fill(0U);
    chanceDecisionSteps.fill(0U);
    playheadValidMask = 0U;
    chanceDecisionValidMask = 0U;
    chanceDecisionPlayedMask = 0U;
    resolvedPage.reset();
    playbackActive = false;
    playbackRevision.set(playbackRevision.get() + 1U);
    drumTrack = nullptr;
    drumTrackBank = nullptr;
    bump();
}

FLASHMEM void DrumSequencerState::bindTrack(
    uint8_t track,
    DrumTrackState& state,
    SequencerTrackBankState& bank
) {
    const bool changed = drumTrack != &state || drumTrackBank != &bank ||
        targetTrack != track;
    if (changed) laneSelection.reset();
    drumTrack = &state;
    drumTrackBank = &bank;
    targetTrack = track;
    if (changed) bump();
}

FLASHMEM void DrumSequencerState::unbindTrack() {
    const bool changed = drumTrack != nullptr || drumTrackBank != nullptr ||
        phase != DrumSequencerPhase::INACTIVE ||
        targetTrack != INVALID_TRACK || selector != DrumSequencerSelector::NONE;
    drumTrack = nullptr;
    drumTrackBank = nullptr;
    phase = DrumSequencerPhase::INACTIVE;
    selector = DrumSequencerSelector::NONE;
    laneSelection.reset();
    targetTrack = INVALID_TRACK;
    if (changed) bump();
}

FLASHMEM void DrumSequencerState::publishAuthoredMutation() {
    if (drumTrackBank != nullptr) {
        drumTrackBank->publishDrumMutation(targetTrack);
    }
    bump();
}

FLASHMEM void DrumSequencerState::openTypePicker(uint8_t track) {
    targetTrack = track;
    selectedKind = DrumSequencerKind::INSTRUMENT;
    selectedKitPreset = DrumKitPreset::EMPTY;
    phase = DrumSequencerPhase::TYPE_PICKER;
    bump();
}

FLASHMEM void DrumSequencerState::moveKind(float delta) {
    if (!typePickerVisible() || delta == 0.0f) return;
    const auto next = delta > 0.0f
        ? DrumSequencerKind::DRUM
        : DrumSequencerKind::INSTRUMENT;
    if (selectedKind == next) return;
    selectedKind = next;
    bump();
}

FLASHMEM void DrumSequencerState::openKitPicker() {
    if (!typePickerVisible() || selectedKind != DrumSequencerKind::DRUM) {
        return;
    }
    selectedKitPreset = DrumKitPreset::EMPTY;
    phase = DrumSequencerPhase::KIT_PICKER;
    bump();
}

FLASHMEM void DrumSequencerState::moveKitPreset(float delta) {
    if (!kitPickerVisible() || delta == 0.0f) return;
    const int count = static_cast<int>(DrumKitPreset::COUNT);
    const int current = static_cast<int>(selectedKitPreset);
    const int direction = delta > 0.0f ? 1 : -1;
    selectedKitPreset = static_cast<DrumKitPreset>(
        (current + direction + count) % count
    );
    bump();
}

FLASHMEM void DrumSequencerState::returnToTypePicker() {
    if (!kitPickerVisible()) return;
    phase = DrumSequencerPhase::TYPE_PICKER;
    selectedKind = DrumSequencerKind::DRUM;
    bump();
}

FLASHMEM void DrumSequencerState::enterGrid() {
    if (!drumTrack) return;
    selectedLane = 0;
    laneAddSlotSelected = drumTrack->kit.laneCount == 0U;
    laneWindowStart = 0;
    focusedStep = 0;
    page = 0;
    property = DrumSequencerProperty::VELOCITY;
    dimension = DrumSequencerDimension::LENGTH;
    selector = DrumSequencerSelector::NONE;
    laneEditor = {};
    laneSelection.reset();
    patternDefaultField = DrumPatternDefaultField::LENGTH;
    phase = DrumSequencerPhase::GRID;
    bump();
}

FLASHMEM void DrumSequencerState::close() {
    unbindTrack();
}

FLASHMEM void DrumSequencerState::moveLane(float delta) {
    if (!gridVisible() || !drumTrack || delta == 0.0f) return;
    const uint8_t laneCount = std::min<uint8_t>(
        drumTrack->kit.laneCount,
        LANE_COUNT
    );
    const bool addVisible = laneCount < LANE_COUNT;
    const uint8_t itemCount = static_cast<uint8_t>(
        laneCount + (addVisible ? 1U : 0U)
    );
    if (itemCount == 0U) return;

    const uint8_t current = laneAddSlotFocused()
        ? laneCount
        : std::min<uint8_t>(selectedLane, laneCount == 0U
              ? 0U
              : static_cast<uint8_t>(laneCount - 1U));
    const int direction = delta > 0.0f ? 1 : -1;
    const uint8_t next = static_cast<uint8_t>(
        (static_cast<int>(current) + direction + static_cast<int>(itemCount)) %
        static_cast<int>(itemCount)
    );
    laneAddSlotSelected = addVisible && next == laneCount;
    if (!laneAddSlotSelected) selectedLane = next;
    ensureSelectedLaneVisible();
    if (laneAddSlotSelected || laneCount == 0U) {
        bump();
        return;
    }
    const uint8_t length = drumTrack->pattern.effectiveLength(selectedLane);
    const uint8_t pageStart = static_cast<uint8_t>(page * STEPS_PER_PAGE);
    if (pageStart < length) {
        const uint8_t pageEnd = std::min<uint8_t>(
            static_cast<uint8_t>(pageStart + STEPS_PER_PAGE - 1U),
            static_cast<uint8_t>(length - 1U)
        );
        focusedStep = std::clamp<uint8_t>(focusedStep, pageStart, pageEnd);
    } else {
        focusedStep = static_cast<uint8_t>(length - 1U);
    }
    bump();
}

FLASHMEM void DrumSequencerState::ensureSelectedLaneVisible() {
    if (!drumTrack) {
        laneWindowStart = 0U;
        return;
    }
    const uint8_t laneCount = std::min<uint8_t>(
        drumTrack->kit.laneCount,
        LANE_COUNT
    );
    const bool addVisible = laneCount < LANE_COUNT;
    if (!addVisible) laneAddSlotSelected = false;
    if (laneCount == 0U) {
        selectedLane = 0U;
        laneAddSlotSelected = addVisible;
        laneWindowStart = 0U;
        return;
    }
    selectedLane = std::min<uint8_t>(
        selectedLane,
        static_cast<uint8_t>(laneCount - 1U)
    );
    const uint8_t itemCount = static_cast<uint8_t>(
        laneCount + (addVisible ? 1U : 0U)
    );
    const uint8_t cursor = laneAddSlotFocused() ? laneCount : selectedLane;
    const uint8_t maxStart = itemCount > VISIBLE_LANE_COUNT
        ? static_cast<uint8_t>(itemCount - VISIBLE_LANE_COUNT)
        : 0U;
    if (cursor < laneWindowStart) {
        laneWindowStart = cursor;
    } else if (cursor >=
               static_cast<uint8_t>(laneWindowStart + VISIBLE_LANE_COUNT)) {
        laneWindowStart = static_cast<uint8_t>(
            cursor - VISIBLE_LANE_COUNT + 1U
        );
    }
    laneWindowStart = std::min<uint8_t>(laneWindowStart, maxStart);
}

FLASHMEM uint8_t DrumSequencerState::overviewLength() const {
    if (drumTrack == nullptr) return 1U;
    const uint8_t laneCount = std::min<uint8_t>(
        drumTrack->kit.laneCount,
        LANE_COUNT
    );
    uint8_t length = laneCount == 0U
        ? drumTrack->pattern.defaultLength
        : 1U;
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        length = std::max<uint8_t>(
            length,
            drumTrack->pattern.effectiveLength(lane)
        );
    }
    return std::max<uint8_t>(1U, length);
}

FLASHMEM uint8_t DrumSequencerState::overviewPageCount() const {
    return std::max<uint8_t>(
        1U,
        static_cast<uint8_t>(
            (overviewLength() + STEPS_PER_PAGE - 1U) / STEPS_PER_PAGE
        )
    );
}

FLASHMEM void DrumSequencerState::clampOverviewPage() {
    page = std::min<uint8_t>(
        page,
        static_cast<uint8_t>(overviewPageCount() - 1U)
    );
}

FLASHMEM bool DrumSequencerState::focusAuthoredLane() {
    if (!gridVisible() || !drumTrack || drumTrack->kit.laneCount == 0U) {
        return false;
    }
    bool changed = false;
    if (laneAddSlotSelected) {
        laneAddSlotSelected = false;
        selectedLane = std::min<uint8_t>(
            selectedLane,
            static_cast<uint8_t>(drumTrack->kit.laneCount - 1U)
        );
        ensureSelectedLaneVisible();
        changed = true;
    }
    const uint8_t length = drumTrack->pattern.effectiveLength(selectedLane);
    const uint8_t pageStart = static_cast<uint8_t>(page * STEPS_PER_PAGE);
    // Pattern paging belongs to the complete polymetric overview. A shorter
    // Lane can therefore be empty on the current page; do not silently jump
    // the musician to another page when entering Step focus.
    if (pageStart >= length) {
        if (changed) bump();
        return false;
    }
    const uint8_t pageEnd = std::min<uint8_t>(
        static_cast<uint8_t>(pageStart + STEPS_PER_PAGE - 1U),
        static_cast<uint8_t>(length - 1U)
    );
    const uint8_t nextStep = std::clamp<uint8_t>(
        focusedStep,
        pageStart,
        pageEnd
    );
    changed = changed || nextStep != focusedStep;
    focusedStep = nextStep;
    if (changed) bump();
    return true;
}

FLASHMEM void DrumSequencerState::moveFocusedStep(float delta) {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused() ||
        delta == 0.0f) return;
    const uint8_t length = drumTrack->pattern.effectiveLength(selectedLane);
    if (length == 0U) return;
    const int direction = delta > 0.0f ? 1 : -1;
    const int next = static_cast<int>(focusedStep) + direction;
    focusedStep = static_cast<uint8_t>(
        (next + static_cast<int>(length)) % static_cast<int>(length)
    );
    page = static_cast<uint8_t>(focusedStep / STEPS_PER_PAGE);
    bump();
}

FLASHMEM void DrumSequencerState::movePage(int direction) {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused() ||
        direction == 0) return;
    const uint8_t pageCount = overviewPageCount();
    const int next = static_cast<int>(page) + (direction > 0 ? 1 : -1);
    page = static_cast<uint8_t>(
        (next + static_cast<int>(pageCount)) % static_cast<int>(pageCount)
    );
    bump();
}

FLASHMEM void DrumSequencerState::openDimensionSelector() {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused() ||
        selectorVisible()) return;
    selectorSnapshotProperty = property;
    selectorSnapshotDimension = dimension;
    selectorSnapshotLane = selectedLane;
    const auto& timing = drumTrack->pattern.lanes[selectedLane].timing;
    selectorSnapshotTimingMode = static_cast<uint8_t>(timing.mode);
    selectorSnapshotLength = timing.length;
    selectorSnapshotStepsPerBeat = timing.stepsPerBeat;
    selector = DrumSequencerSelector::DIMENSION;
    bump();
}

FLASHMEM void DrumSequencerState::openPropertySelector() {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused() ||
        selectorVisible()) return;
    selectorSnapshotProperty = property;
    selectorSnapshotDimension = dimension;
    selectorSnapshotLane = selectedLane;
    const auto& timing = drumTrack->pattern.lanes[selectedLane].timing;
    selectorSnapshotTimingMode = static_cast<uint8_t>(timing.mode);
    selectorSnapshotLength = timing.length;
    selectorSnapshotStepsPerBeat = timing.stepsPerBeat;
    selector = DrumSequencerSelector::PROPERTY;
    bump();
}

FLASHMEM bool DrumSequencerState::openLaneEditor(bool create) {
    if (!gridVisible() || !drumTrack || selectorVisible()) return false;
    const uint8_t laneCount = std::min<uint8_t>(
        drumTrack->kit.laneCount,
        DRUM_MAX_LANES
    );
    if (create && laneCount >= DRUM_MAX_LANES) return false;
    if (!create && (laneCount == 0U || selectedLane >= laneCount)) return false;

    laneEditor = {};
    laneEditor.active = true;
    laneEditor.mode = create
        ? DrumLaneEditorMode::CREATE
        : DrumLaneEditorMode::EDIT;
    laneEditor.field = DrumLaneEditorField::NAME;
    laneEditor.sourceLane = create ? laneCount : selectedLane;
    laneEditor.targetLane = laneEditor.sourceLane;
    if (create) {
        laneEditor.draft = {
            .midiNote = static_cast<uint8_t>(
                std::min<unsigned>(127U, 36U + laneCount)
            ),
            .role = DrumLaneRole::CUSTOM,
        };
        (void)setDrumLaneColorIndex(
            laneEditor.draft,
            static_cast<uint8_t>(laneCount % DRUM_LANE_COLOR_COUNT)
        );
    } else {
        laneEditor.draft = drumTrack->kit.lanes[selectedLane];
    }
    selector = DrumSequencerSelector::LANE_EDITOR;
    bump();
    return true;
}

FLASHMEM bool DrumSequencerState::retargetLaneEditor(float delta) {
    if (!laneEditor.active || laneEditor.dirty || laneEditor.textEditing ||
        laneEditor.mode != DrumLaneEditorMode::EDIT || !drumTrack ||
        delta == 0.0f) {
        return false;
    }
    const uint8_t laneCount = std::min<uint8_t>(
        drumTrack->kit.laneCount,
        DRUM_MAX_LANES
    );
    if (laneCount <= 1U) return false;

    const int direction = delta > 0.0f ? 1 : -1;
    const uint8_t next = static_cast<uint8_t>(
        (static_cast<int>(laneEditor.sourceLane) + direction + laneCount) %
        laneCount
    );
    laneEditor.sourceLane = next;
    laneEditor.targetLane = next;
    laneEditor.draft = drumTrack->kit.lanes[next];
    selectedLane = next;
    ensureSelectedLaneVisible();
    bump();
    return true;
}

FLASHMEM void DrumSequencerState::moveLaneEditorField(float delta) {
    if (!laneEditor.active || delta == 0.0f) return;
    if (laneEditor.textEditing) {
        moveLaneNameKey(delta);
        return;
    }
    constexpr int count = static_cast<int>(DrumLaneEditorField::COUNT);
    const int current = static_cast<int>(laneEditor.field);
    laneEditor.field = static_cast<DrumLaneEditorField>(
        (current + (delta > 0.0f ? 1 : -1) + count) % count
    );
    bump();
}

FLASHMEM void DrumSequencerState::moveLaneNameKey(float delta) {
    if (!laneEditor.active || !laneEditor.textEditing || delta == 0.0f) return;
    laneEditor.textKeyIndex =
        core::state::interaction::textKeyboardMoveColumn(
            laneEditor.textKeyIndex,
            delta > 0.0f ? 1 : -1
        );
    bump();
}

FLASHMEM void DrumSequencerState::toggleLaneNameEditing() {
    if (!laneEditor.active || laneEditor.field != DrumLaneEditorField::NAME) {
        return;
    }
    if (laneEditor.textEditing) {
        acceptLaneNameEditing();
        return;
    }
    laneEditor.nameBeforeTextEditing = laneEditor.draft.name;
    laneEditor.overrideMaskBeforeTextEditing =
        laneEditor.draft.overrideMask;
    if ((laneEditor.draft.overrideMask & DRUM_LANE_OVERRIDE_NAME) == 0U) {
        const char* displayName = drumLaneDisplayName(laneEditor.draft);
        laneEditor.draft.name.fill('\0');
        for (uint8_t index = 0U;
             index < DRUM_LANE_NAME_MAX_LENGTH && displayName[index] != '\0';
             ++index) {
            laneEditor.draft.name[index] = displayName[index];
        }
        laneEditor.draft.overrideMask = static_cast<uint8_t>(
            laneEditor.draft.overrideMask | DRUM_LANE_OVERRIDE_NAME
        );
    }
    laneEditor.dirtyBeforeTextEditing = laneEditor.dirty;
    laneEditor.textEditing = true;
    laneEditor.textShiftActive = false;
    laneEditor.textKeyIndex =
        core::state::interaction::TEXT_KEYBOARD_DEFAULT_INDEX;
    laneEditor.textOptRawPosition = 0.0f;
    laneEditor.textOptRowAccumulator = 0.0f;
    bump();
}

FLASHMEM void DrumSequencerState::moveLaneNameRow(float rawPosition) {
    if (!laneEditor.active || !laneEditor.textEditing) return;
    const float delta = rawPosition - laneEditor.textOptRawPosition;
    laneEditor.textOptRawPosition = rawPosition;
    if (delta == 0.0f) return;
    constexpr float ticksPerRow = (600.0f * 4.0f) /
        static_cast<float>(
            core::state::interaction::TEXT_KEYBOARD_ROW_COUNT
        );
    laneEditor.textOptRowAccumulator += delta / ticksPerRow;
    const float absolute = std::fabs(laneEditor.textOptRowAccumulator);
    if (absolute < 1.0f) return;
    const int steps = static_cast<int>(absolute);
    const bool increasing = laneEditor.textOptRowAccumulator > 0.0f;
    laneEditor.textOptRowAccumulator += increasing
        ? -static_cast<float>(steps)
        : static_cast<float>(steps);
    const uint8_t next = core::state::interaction::textKeyboardMoveRow(
        laneEditor.textKeyIndex,
        increasing ? -steps : steps
    );
    if (next == laneEditor.textKeyIndex) return;
    laneEditor.textKeyIndex = next;
    bump();
}

FLASHMEM void DrumSequencerState::insertLaneNameKey() {
    if (!laneEditor.active || !laneEditor.textEditing) return;
    const char character = core::state::interaction::textKeyboardCharacterAt(
        laneEditor.textKeyIndex,
        laneEditor.textShiftActive
    );
    if (!core::state::interaction::textKeyboardAppend(
            laneEditor.draft.name.data(),
            laneEditor.draft.name.size(),
            character
        )) {
        return;
    }
    laneEditor.dirty = true;
    bump();
}

FLASHMEM void DrumSequencerState::backspaceLaneName() {
    if (!laneEditor.active || !laneEditor.textEditing) return;
    if (!core::state::interaction::textKeyboardBackspace(
            laneEditor.draft.name.data()
        )) {
        return;
    }
    laneEditor.dirty = true;
    bump();
}

FLASHMEM void DrumSequencerState::setLaneNameShift(bool active) {
    if (!laneEditor.active || !laneEditor.textEditing ||
        laneEditor.textShiftActive == active) {
        return;
    }
    laneEditor.textShiftActive = active;
    bump();
}

FLASHMEM void DrumSequencerState::acceptLaneNameEditing() {
    if (!laneEditor.active || !laneEditor.textEditing) return;
    (void)setDrumLaneName(
        laneEditor.draft,
        laneEditor.draft.name.data()
    );
    laneEditor.textEditing = false;
    laneEditor.textShiftActive = false;
    bump();
}

FLASHMEM void DrumSequencerState::cancelLaneNameEditing() {
    if (!laneEditor.active || !laneEditor.textEditing) return;
    laneEditor.draft.name = laneEditor.nameBeforeTextEditing;
    laneEditor.draft.overrideMask =
        laneEditor.overrideMaskBeforeTextEditing;
    laneEditor.dirty = laneEditor.dirtyBeforeTextEditing;
    laneEditor.textEditing = false;
    laneEditor.textShiftActive = false;
    bump();
}

FLASHMEM void DrumSequencerState::editLaneEditorValue(float normalized) {
    if (!laneEditor.active || !drumTrack) return;
    const float value = std::clamp(normalized, 0.0f, 1.0f);
    bool changed = false;
    const auto index = [value](uint16_t count) -> uint16_t {
        if (count <= 1U || value <= 0.0f) return 0U;
        if (value >= 1.0f) return static_cast<uint16_t>(count - 1U);
        return std::min<uint16_t>(
            static_cast<uint16_t>(value * static_cast<float>(count)),
            static_cast<uint16_t>(count - 1U)
        );
    };

    switch (laneEditor.field) {
        case DrumLaneEditorField::ROLE: {
            changed = setDrumLaneRole(
                laneEditor.draft,
                static_cast<DrumLaneRole>(
                    index(
                        static_cast<uint16_t>(
                            DrumLaneRole::PERCUSSION
                        ) + 1U
                    )
                )
            );
            break;
        }
        case DrumLaneEditorField::NAME: {
            return;
        }
        case DrumLaneEditorField::ICON: {
            const auto next = static_cast<DrumLaneIcon>(
                index(static_cast<uint16_t>(DrumLaneIcon::COUNT))
            );
            changed = setDrumLaneIcon(laneEditor.draft, next);
            break;
        }
        case DrumLaneEditorField::COLOR: {
            const auto next = static_cast<uint8_t>(
                index(DRUM_LANE_COLOR_COUNT)
            );
            changed = setDrumLaneColorIndex(laneEditor.draft, next);
            break;
        }
        case DrumLaneEditorField::NOTE: {
            const auto next = static_cast<uint8_t>(index(128U));
            changed = laneEditor.draft.midiNote != next;
            laneEditor.draft.midiNote = next;
            break;
        }
        case DrumLaneEditorField::POSITION: {
            const uint8_t laneCount = std::min<uint8_t>(
                drumTrack->kit.laneCount,
                DRUM_MAX_LANES
            );
            const uint16_t positions = laneEditor.mode == DrumLaneEditorMode::CREATE
                ? static_cast<uint16_t>(laneCount + 1U)
                : laneCount;
            const auto next = static_cast<uint8_t>(index(positions));
            changed = laneEditor.targetLane != next;
            laneEditor.targetLane = next;
            break;
        }
        case DrumLaneEditorField::COUNT:
        default:
            return;
    }
    if (!changed) return;
    laneEditor.dirty = true;
    bump();
}

FLASHMEM bool DrumSequencerState::applyLaneEditor() {
    if (!laneEditor.active || !drumTrack) return false;
    bool changed = false;
    uint8_t selected = laneEditor.targetLane;
    if (laneEditor.mode == DrumLaneEditorMode::CREATE) {
        changed = drumTrack->insertLane(
            laneEditor.targetLane,
            laneEditor.draft
        );
    } else {
        changed = drumTrack->kit.setLane(
            laneEditor.sourceLane,
            laneEditor.draft
        );
        if (laneEditor.targetLane != laneEditor.sourceLane) {
            changed = drumTrack->moveLane(
                laneEditor.sourceLane,
                laneEditor.targetLane
            ) || changed;
        }
    }

    laneEditor = {};
    selector = DrumSequencerSelector::NONE;
    if (changed) {
        laneAddSlotSelected = false;
        selectedLane = selected;
        ensureSelectedLaneVisible();
        clampOverviewPage();
        const uint8_t pageStart = static_cast<uint8_t>(
            page * STEPS_PER_PAGE
        );
        const uint8_t length =
            drumTrack->pattern.effectiveLength(selectedLane);
        focusedStep = std::min<uint8_t>(
            focusedStep,
            static_cast<uint8_t>(length - 1U)
        );
        if (pageStart < length && focusedStep < pageStart) {
            focusedStep = pageStart;
        }
        publishAuthoredMutation();
    } else {
        bump();
    }
    return changed;
}

FLASHMEM bool DrumSequencerState::removeLaneFromEditor() {
    if (!laneEditor.active || !drumTrack ||
        laneEditor.mode != DrumLaneEditorMode::EDIT) {
        return false;
    }
    const uint8_t removed = laneEditor.sourceLane;
    if (!drumTrack->removeLane(removed)) return false;
    laneEditor = {};
    selector = DrumSequencerSelector::NONE;
    const uint8_t laneCount = drumTrack->kit.laneCount;
    laneAddSlotSelected = laneCount == 0U;
    selectedLane = laneCount == 0U
        ? 0U
        : std::min<uint8_t>(removed, static_cast<uint8_t>(laneCount - 1U));
    ensureSelectedLaneVisible();
    clampOverviewPage();
    if (laneCount == 0U) {
        focusedStep = 0U;
    } else {
        const uint8_t length =
            drumTrack->pattern.effectiveLength(selectedLane);
        const uint8_t pageStart = static_cast<uint8_t>(
            page * STEPS_PER_PAGE
        );
        focusedStep = pageStart < length
            ? std::clamp<uint8_t>(
                  focusedStep,
                  pageStart,
                  std::min<uint8_t>(
                      static_cast<uint8_t>(
                          pageStart + STEPS_PER_PAGE - 1U
                      ),
                      static_cast<uint8_t>(length - 1U)
                  )
              )
            : static_cast<uint8_t>(length - 1U);
    }
    publishAuthoredMutation();
    return true;
}

FLASHMEM void DrumSequencerState::cancelLaneEditor() {
    if (!laneEditor.active &&
        selector != DrumSequencerSelector::LANE_EDITOR) {
        return;
    }
    laneEditor = {};
    selector = DrumSequencerSelector::NONE;
    bump();
}

FLASHMEM void DrumSequencerState::openPatternDefaults() {
    if (!gridVisible() || !drumTrack || selectorVisible()) return;
    patternDefaultField = DrumPatternDefaultField::LENGTH;
    patternDefaultSnapshotLength = drumTrack->pattern.defaultLength;
    patternDefaultSnapshotStepsPerBeat = drumTrack->pattern.defaultStepsPerBeat;
    selector = DrumSequencerSelector::PATTERN_DEFAULTS;
    bump();
}

FLASHMEM void DrumSequencerState::movePatternDefaultField(float delta) {
    if (selector != DrumSequencerSelector::PATTERN_DEFAULTS ||
        delta == 0.0f) {
        return;
    }
    const int count = static_cast<int>(DrumPatternDefaultField::COUNT);
    const int direction = delta > 0.0f ? 1 : -1;
    patternDefaultField = static_cast<DrumPatternDefaultField>(
        (static_cast<int>(patternDefaultField) + direction + count) % count
    );
    bump();
}

FLASHMEM void DrumSequencerState::editPatternDefaultValue(
    float normalized
) {
    if (selector != DrumSequencerSelector::PATTERN_DEFAULTS ||
        !drumTrack) {
        return;
    }
    const float value = std::clamp(normalized, 0.0f, 1.0f);
    uint8_t length = drumTrack->pattern.defaultLength;
    uint8_t stepsPerBeat = drumTrack->pattern.defaultStepsPerBeat;
    if (patternDefaultField == DrumPatternDefaultField::LENGTH) {
        length = static_cast<uint8_t>(
            std::min<int>(
                MAX_STEPS,
                static_cast<int>(value * MAX_STEPS) + 1
            )
        );
    } else {
        constexpr std::array<uint8_t, 6> choices{{1U, 2U, 3U, 4U, 6U, 8U}};
        const size_t index = std::min<size_t>(
            choices.size() - 1U,
            static_cast<size_t>(value * choices.size())
        );
        stepsPerBeat = choices[index];
    }
    if (drumTrack->pattern.setDefaults(length, stepsPerBeat)) {
        clampOverviewPage();
        if (drumTrack->kit.laneCount > 0U) {
            const uint8_t laneLength =
                drumTrack->pattern.effectiveLength(selectedLane);
            focusedStep = std::min<uint8_t>(
                focusedStep,
                static_cast<uint8_t>(laneLength - 1U)
            );
        }
        publishAuthoredMutation();
    }
}

FLASHMEM void DrumSequencerState::moveSelector(float delta) {
    if (!selectorVisible() || delta == 0.0f) return;
    if (selector == DrumSequencerSelector::LANE_EDITOR) {
        return;
    }
    if (selector == DrumSequencerSelector::PATTERN_DEFAULTS) {
        movePatternDefaultField(delta);
        return;
    }
    const int direction = delta > 0.0f ? 1 : -1;
    if (selector == DrumSequencerSelector::DIMENSION) {
        constexpr int count = static_cast<int>(
            DrumSequencerDimension::COUNT
        );
        const int current = static_cast<int>(dimension);
        dimension = static_cast<DrumSequencerDimension>(
            (current + direction + count) % count
        );
    } else {
        constexpr int count = static_cast<int>(
            DrumSequencerProperty::COUNT
        );
        const int current = static_cast<int>(property);
        property = static_cast<DrumSequencerProperty>(
            (current + direction + count) % count
        );
    }
    bump();
}

FLASHMEM void DrumSequencerState::applySelector() {
    if (!selectorVisible()) return;
    if (selector == DrumSequencerSelector::LANE_EDITOR) {
        return;
    }
    if (selector == DrumSequencerSelector::PATTERN_DEFAULTS) {
        selector = DrumSequencerSelector::NONE;
        bump();
        return;
    }
    selector = DrumSequencerSelector::NONE;
    bump();
}

FLASHMEM void DrumSequencerState::cancelSelector() {
    if (!selectorVisible()) return;
    if (selector == DrumSequencerSelector::LANE_EDITOR) {
        return;
    }
    if (selector == DrumSequencerSelector::PATTERN_DEFAULTS) {
        const bool restored = drumTrack != nullptr &&
            drumTrack->pattern.setDefaults(
                patternDefaultSnapshotLength,
                patternDefaultSnapshotStepsPerBeat
            );
        selector = DrumSequencerSelector::NONE;
        if (restored) publishAuthoredMutation();
        else bump();
        return;
    }
    const auto closingSelector = selector;
    selector = DrumSequencerSelector::NONE;
    property = selectorSnapshotProperty;
    dimension = selectorSnapshotDimension;
    bool authoredChanged = false;
    if (closingSelector == DrumSequencerSelector::DIMENSION &&
        drumTrack && selectorSnapshotLane < DRUM_MAX_LANES) {
        if (selectorSnapshotTimingMode == static_cast<uint8_t>(
                DrumLaneTimingMode::CUSTOM
            )) {
            authoredChanged = drumTrack->pattern.setLaneTimingCustom(
                selectorSnapshotLane,
                selectorSnapshotLength,
                selectorSnapshotStepsPerBeat
            );
        } else {
            authoredChanged = drumTrack->pattern.setLaneTimingInherited(
                selectorSnapshotLane
            );
        }
    }
    if (authoredChanged) publishAuthoredMutation();
    else bump();
}

FLASHMEM void DrumSequencerState::setSelectedLaneTimingCustom(
    bool custom
) {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused()) return;
    const bool changed = custom
        ? drumTrack->pattern.setLaneTimingCustom(
              selectedLane,
              drumTrack->pattern.effectiveLength(selectedLane),
              drumTrack->pattern.effectiveStepsPerBeat(selectedLane)
          )
        : drumTrack->pattern.setLaneTimingInherited(selectedLane);
    if (!changed) return;
    clampOverviewPage();
    const uint8_t length = drumTrack->pattern.effectiveLength(selectedLane);
    focusedStep = std::min<uint8_t>(
        focusedStep,
        static_cast<uint8_t>(length - 1U)
    );
    publishAuthoredMutation();
}

FLASHMEM void DrumSequencerState::setSelectedLaneLength(
    uint8_t length
) {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused()) return;
    if (!drumTrack->pattern.setLaneTimingCustom(
            selectedLane,
            length,
            drumTrack->pattern.effectiveStepsPerBeat(selectedLane)
        )) {
        return;
    }
    const uint8_t effectiveLength =
        drumTrack->pattern.effectiveLength(selectedLane);
    focusedStep = std::min<uint8_t>(
        focusedStep,
        static_cast<uint8_t>(effectiveLength - 1U)
    );
    clampOverviewPage();
    publishAuthoredMutation();
}

FLASHMEM void DrumSequencerState::setSelectedLaneStepsPerBeat(
    uint8_t stepsPerBeat
) {
    if (!gridVisible() || !drumTrack || laneAddSlotFocused()) return;
    if (!drumTrack->pattern.setLaneTimingCustom(
            selectedLane,
            drumTrack->pattern.effectiveLength(selectedLane),
            stepsPerBeat
        )) {
        return;
    }
    publishAuthoredMutation();
}

FLASHMEM uint8_t DrumSequencerState::visibleStep(
    uint8_t indexInPage
) const {
    const uint16_t step = static_cast<uint16_t>(page) * STEPS_PER_PAGE +
        std::min<uint8_t>(indexInPage, STEPS_PER_PAGE - 1U);
    return static_cast<uint8_t>(std::min<uint16_t>(step, MAX_STEPS - 1U));
}

FLASHMEM uint8_t DrumSequencerState::visibleLane(uint8_t row) const {
    return std::min<uint8_t>(
        static_cast<uint8_t>(laneWindowStart + row),
        static_cast<uint8_t>(LANE_COUNT - 1U)
    );
}

FLASHMEM void DrumSequencerState::toggleVisibleStep(
    uint8_t indexInPage
) {
    if (!gridVisible() || laneAddSlotFocused() ||
        indexInPage >= STEPS_PER_PAGE) return;
    const uint8_t step = visibleStep(indexInPage);
    if (!drumTrack ||
        step >= drumTrack->pattern.effectiveLength(selectedLane)) return;
    if (!drumTrack->pattern.toggleStep(selectedLane, step)) return;
    publishAuthoredMutation();
}

FLASHMEM void DrumSequencerState::setVisibleStepVelocity(
    uint8_t indexInPage,
    uint8_t velocity
) {
    if (!gridVisible() || laneAddSlotFocused() ||
        indexInPage >= STEPS_PER_PAGE) return;
    const uint8_t step = visibleStep(indexInPage);
    (void)setStepVelocity(selectedLane, step, velocity);
}

FLASHMEM void DrumSequencerState::setVisibleStepEnabled(
    uint8_t indexInPage,
    bool enabled
) {
    if (!gridVisible() || laneAddSlotFocused() ||
        indexInPage >= STEPS_PER_PAGE) return;
    const uint8_t step = visibleStep(indexInPage);
    (void)setStepEnabled(selectedLane, step, enabled);
}

FLASHMEM void DrumSequencerState::setVisibleStepGate(
    uint8_t indexInPage,
    uint16_t gatePercent
) {
    if (!gridVisible() || laneAddSlotFocused() ||
        indexInPage >= STEPS_PER_PAGE) return;
    const uint8_t step = visibleStep(indexInPage);
    (void)setStepGate(selectedLane, step, gatePercent);
}

FLASHMEM void DrumSequencerState::setVisibleStepNudge(
    uint8_t indexInPage,
    int8_t nudgePercent
) {
    if (!gridVisible() || laneAddSlotFocused() ||
        indexInPage >= STEPS_PER_PAGE) return;
    const uint8_t step = visibleStep(indexInPage);
    (void)setStepNudge(selectedLane, step, nudgePercent);
}

FLASHMEM void DrumSequencerState::setVisibleStepProbability(
    uint8_t indexInPage,
    uint8_t probability
) {
    if (!gridVisible() || laneAddSlotFocused() ||
        indexInPage >= STEPS_PER_PAGE) return;
    const uint8_t step = visibleStep(indexInPage);
    (void)setStepProbability(selectedLane, step, probability);
}

FLASHMEM bool DrumSequencerState::stepInRange(
    uint8_t lane,
    uint8_t step
) const {
    return gridVisible() && lane < LANE_COUNT && step < MAX_STEPS &&
           lane < drumTrack->kit.laneCount &&
           step < drumTrack->pattern.effectiveLength(lane);
}

FLASHMEM bool DrumSequencerState::adjacentLaneForStep(
    uint8_t lane,
    uint8_t step,
    int direction,
    uint8_t& adjacentLane
) const {
    if (!gridVisible() || direction == 0 || step >= MAX_STEPS) return false;
    const uint8_t laneCount = std::min<uint8_t>(
        drumTrack->kit.laneCount,
        LANE_COUNT
    );
    if (laneCount <= 1U || lane >= laneCount) return false;

    for (uint8_t distance = 1U; distance < laneCount; ++distance) {
        const int offset = direction > 0
            ? static_cast<int>(distance)
            : -static_cast<int>(distance);
        const uint8_t candidate = static_cast<uint8_t>(
            (static_cast<int>(lane) + offset + laneCount) % laneCount
        );
        if (step < drumTrack->pattern.effectiveLength(candidate)) {
            adjacentLane = candidate;
            return true;
        }
    }
    return false;
}

FLASHMEM bool DrumSequencerState::focusStep(
    uint8_t lane,
    uint8_t step
) {
    if (!stepInRange(lane, step)) return false;
    const bool changed = laneAddSlotSelected || selectedLane != lane ||
                         focusedStep != step ||
                         page != static_cast<uint8_t>(step / STEPS_PER_PAGE);
    laneAddSlotSelected = false;
    selectedLane = lane;
    ensureSelectedLaneVisible();
    focusedStep = step;
    page = static_cast<uint8_t>(step / STEPS_PER_PAGE);
    if (changed) bump();
    return true;
}

FLASHMEM bool DrumSequencerState::setStepEnabled(
    uint8_t lane,
    uint8_t step,
    bool enabled
) {
    if (!stepInRange(lane, step) ||
        !drumTrack->pattern.setStepEnabled(lane, step, enabled)) {
        return false;
    }
    publishAuthoredMutation();
    return true;
}

FLASHMEM bool DrumSequencerState::setStepVelocity(
    uint8_t lane,
    uint8_t step,
    uint8_t velocity
) {
    if (!stepInRange(lane, step) ||
        !drumTrack->pattern.setStepVelocity(lane, step, velocity)) {
        return false;
    }
    publishAuthoredMutation();
    return true;
}

FLASHMEM bool DrumSequencerState::setStepGate(
    uint8_t lane,
    uint8_t step,
    uint16_t gatePercent
) {
    if (!stepInRange(lane, step) ||
        !drumTrack->pattern.setStepGate(lane, step, gatePercent)) {
        return false;
    }
    publishAuthoredMutation();
    return true;
}

FLASHMEM bool DrumSequencerState::setStepNudge(
    uint8_t lane,
    uint8_t step,
    int8_t nudgePercent
) {
    if (!stepInRange(lane, step) ||
        !drumTrack->pattern.setStepNudge(lane, step, nudgePercent)) {
        return false;
    }
    publishAuthoredMutation();
    return true;
}

FLASHMEM bool DrumSequencerState::setStepProbability(
    uint8_t lane,
    uint8_t step,
    uint8_t probability
) {
    if (!stepInRange(lane, step) ||
        !drumTrack->pattern.setStepProbability(lane, step, probability)) {
        return false;
    }
    publishAuthoredMutation();
    return true;
}

FLASHMEM void DrumSequencerState::publishPlayback(
    const std::array<uint8_t, RUNTIME_LANE_CAPACITY>& laneSteps,
    const std::array<uint8_t, RUNTIME_LANE_CAPACITY>& lanePhasesQ8,
    uint16_t validMask,
    const std::array<uint8_t, RUNTIME_LANE_CAPACITY>& decisionSteps,
    uint16_t decisionValidMask,
    uint16_t decisionPlayedMask,
    const DrumResolvedPageProjection& resolved,
    bool playing
) {
    if (playheadSteps == laneSteps && playheadPhasesQ8 == lanePhasesQ8 &&
        chanceDecisionSteps == decisionSteps &&
        playheadValidMask == validMask &&
        chanceDecisionValidMask == decisionValidMask &&
        chanceDecisionPlayedMask == decisionPlayedMask &&
        resolvedPage.matches(resolved) &&
        playbackActive == playing) {
        return;
    }
    playheadSteps = laneSteps;
    playheadPhasesQ8 = lanePhasesQ8;
    chanceDecisionSteps = decisionSteps;
    playheadValidMask = validMask;
    chanceDecisionValidMask = decisionValidMask;
    chanceDecisionPlayedMask = decisionPlayedMask;
    resolvedPage = resolved;
    playbackActive = playing;
    playbackRevision.set(playbackRevision.get() + 1U);
}

}  // namespace core::state::sequencer

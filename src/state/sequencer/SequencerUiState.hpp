#pragma once

#include <cstdint>
#include <cstring>

#include <array>
#include <variant>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/state/Signal.hpp>
#include <oc/time/Time.hpp>

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
#include "app/ExtmemAllocator.hpp"
#endif
#include "state/StructureClipboardPastePlan.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerChordPresetModel.hpp"
#include "state/sequencer/SequencerHistoryOutcomes.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerStepPresetModel.hpp"
#include "state/sequencer/SequencerUiStateFwd.hpp"
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

enum class SequencerChordEditField : uint8_t {
    SHAPE = 0,
    FORMULA,
    INVERSION,
    VOICING,
    STRUM,
    VELOCITY_CONTOUR,
    PITCH_CONTEXT,
    COUNT,
};

enum class SequencerChordSourceChoice : uint8_t {
    PARENT_CHORD = 0,
    SINGLE_NOTE,
    LOCAL_CHORD,
};

struct SequencerChordAuthoringSnapshot {
    static constexpr uint16_t INVALID_NODE = 0xFFFFU;

    bool valid = false;
    uint16_t nodeId = INVALID_NODE;
    bool modePresent = false;
    bool localPresent = false;
    oc::note::sequencer::StepSequencerChordMode mode =
        oc::note::sequencer::StepSequencerChordMode::Single;
    oc::note::sequencer::StepSequencerChordSpec spec{};

    void reset() {
        valid = false;
        nodeId = INVALID_NODE;
        modePresent = false;
        localPresent = false;
        mode = oc::note::sequencer::StepSequencerChordMode::Single;
        spec = {};
    }
};

struct SequencerChordSubEditorState {
    bool formulaEditorActive = false;
    // Formula items use their zero-based voice index. Root (0) is never
    // focusable. When the formula has fewer than eight voices, the current
    // voice count identifies the trailing Add item.
    uint8_t focusedFormulaItem = 1;
    bool sourceSelectorActive = false;
    SequencerChordSourceChoice focusedSourceChoice =
        SequencerChordSourceChoice::SINGLE_NOTE;

    bool operator==(const SequencerChordSubEditorState& other) const {
        return formulaEditorActive == other.formulaEditorActive &&
               focusedFormulaItem == other.focusedFormulaItem &&
               sourceSelectorActive == other.sourceSelectorActive &&
               focusedSourceChoice == other.focusedSourceChoice;
    }
};

struct SequencerChordEditorState {
    static constexpr uint8_t FIRST_FORMULA_ITEM = 1;

    // One retained overlay presenter owns this editor projection. Keep the
    // tightly coupled sub-editor fields in one single-subscriber signal: they
    // always invalidate the same surface and must be observed atomically.
    Signal<bool, 1> active{false};
    Signal<SequencerChordEditField, 1> focusedField{
        SequencerChordEditField::SHAPE
    };
    Signal<SequencerChordSubEditorState, 1> subEditor{};
    SequencerChordAuthoringSnapshot formulaSnapshot{};

    void reset();
};

struct SequencerContentViewFrame {
    using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;

    SequencerContentViewKind kind = SequencerContentViewKind::ROOT;
    uint8_t ownerRootStep = 0;
    uint8_t ownerLocalStep = 0;
    uint8_t pageSnapshot = 0;
    uint8_t focusSnapshot = 0;
    uint8_t length = 0;
    uint16_t ownerNodeId = GraphLimits::INVALID_ID;
    uint16_t sequenceId = GraphLimits::INVALID_ID;
    uint16_t cycleSetId = GraphLimits::INVALID_ID;
};

static_assert(
    sizeof(SequencerContentViewFrame) == 12U,
    "content-view frame layout is part of the bounded Page planner contract"
);

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

    SequencerContentViewState();
    ~SequencerContentViewState();

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
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    // The retained Step Editor is shared by melodic and Drum tracks. These
    // two cold session fields select the authored domain without duplicating
    // the overlay or projecting Drum edits through the melodic Pattern.
    bool drumContext = false;
    uint8_t drumLane = 0;
#endif
    SequencerChordEditorState chordEditor;

    core::state::StructureHoldState contextHold;

    SequencerStepEditOverlayState();
    ~SequencerStepEditOverlayState();

    void reset();
};

/**
 * Temporary presentation state for the Sequencer NAV context selector.
 *
 * Gesture ownership (press/hold/turn/release) deliberately remains in the
 * bounded workflow. The retained UI observes one compact revision surface.
 */
struct SequencerContextSelectorState {
    bool visible = false;
    core::state::StructureNavigationFocus previewFocus =
        core::state::StructureNavigationFocus::PAGE;
    Signal<uint32_t, 2> revision{0};

    void bump();
    void reset();
};

enum class SequencerPresetLibraryMode : uint8_t {
    LOAD = 0,
    SAVE,
};

enum class SequencerPresetLibraryKind : uint8_t {
    STEP = 0,
    CHORD,
};

enum class SequencerPresetLibraryFeedback : uint8_t {
    NONE = 0,
    SAVED,
    LOADED,
    QUEUED,
    CANCELLED,
    EMPTY,
    INCOMPATIBLE,
    FAILED,
};

struct SequencerStepPresetLibraryState {
    SequencerStepPresetTarget target{};
    SequencerStepPresetDescriptor descriptor{};
    // Correlates QUEUED feedback with the exact LOADED/CANCELLED terminal
    // activation owned by this Step-library session.
    uint32_t activationGeneration = 0;
};

struct SequencerChordPresetLibraryState {
    SequencerChordPresetTarget target{};
    SequencerChordPresetDescriptor descriptor{};
};

using SequencerPresetLibraryPayload = std::variant<
    SequencerStepPresetLibraryState,
    SequencerChordPresetLibraryState>;

struct SequencerPresetLibrarySessionState {
    static constexpr uint8_t ENTRY_CAPACITY = 15;
    static constexpr uint8_t ID_SIZE = core::state::project::ProjectMetadata::ID_SIZE;
    static constexpr uint8_t NAME_SIZE =
        SequencerStepPresetDescriptor::NAME_SIZE;

    Signal<bool> visible{false};
    Signal<SequencerPresetLibraryKind> libraryKind{
        SequencerPresetLibraryKind::STEP
    };
    Signal<SequencerPresetLibraryMode> mode{
        SequencerPresetLibraryMode::LOAD
    };
    Signal<uint8_t> selectedIndex{0};
    Signal<uint8_t> entryCount{0};
    Signal<bool> truncated{false};
    Signal<bool> hasPreviousPage{false};
    Signal<bool> hasNextPage{false};
    Signal<uint16_t> totalEntryCount{0};
    Signal<bool> detailVisible{false};
    Signal<uint8_t> detailFocus{0};
    Signal<bool> inspecting{false};
    Signal<uint8_t> previewStateIndex{0};
    Signal<uint32_t> previewGeneration{0};
    Signal<SequencerPresetLibraryFeedback> feedback{
        SequencerPresetLibraryFeedback::NONE
    };
    Signal<core::state::contextual::GuardedActionState, 4> actionGuard{};
    Signal<core::state::contextual::OperationFeedbackState, 4>
        operationFeedback{};
    Signal<uint32_t> revision{0};
    std::array<std::array<char, ID_SIZE>, ENTRY_CAPACITY> entryIds{};
    std::array<std::array<char, NAME_SIZE>, ENTRY_CAPACITY> entryNames{};
    std::array<bool, ENTRY_CAPACITY> entryMetadataReadable{};
    // Exactly one domain payload is alive. This avoids retaining parallel
    // Step and Chord descriptors and makes the active library authoritative.
    SequencerPresetLibraryPayload payload{};

    SequencerPresetLibrarySessionState();
    ~SequencerPresetLibrarySessionState();

    void open(
        SequencerPresetLibraryMode nextMode,
        SequencerPresetLibraryKind kind = SequencerPresetLibraryKind::STEP
    );
    void reset();
    void setFeedback(SequencerPresetLibraryFeedback nextFeedback);
    void setEntry(
        uint8_t index,
        const char* id,
        const char* semanticName = nullptr,
        bool metadataReadable = false
    );
    const char* entryId(uint8_t index) const;
    const char* entryName(uint8_t index) const;
    bool entryHasReadableMetadata(uint8_t index) const;
    SequencerStepPresetLibraryState& step();
    const SequencerStepPresetLibraryState& step() const;
    SequencerChordPresetLibraryState& chord();
    const SequencerChordPresetLibraryState& chord() const;
    uint8_t itemCount() const;
    uint8_t newAssetItemOffset() const;
    bool selectedItemIsNewAsset() const;
    bool selectedItemIsExistingAsset() const;
    uint8_t existingEntryIndexForSelectedItem() const;
    void clampSelection();
    void bump();

private:
    void clearCatalog();
};

enum class SequencerCcLaneUiMode : uint8_t {
    CLOSED = 0,
    LANE_SELECTOR,
    LANE_GRID,
    TRANSITION_PICKER,
    LANE_SETTINGS,
};

enum class SequencerCcLaneDraftField : uint8_t {
    CONTROLLER = 0,
    ROUTE_POLICY,
    PINNED_CHANNEL,
    MINIMUM,
    MAXIMUM,
    INITIAL,
    ADVANCED,
    COUNT,
};

enum class SequencerCcLaneActionSlot : uint8_t {
    BOTTOM_LEFT = 0,
    BOTTOM_CENTER,
    BOTTOM_RIGHT,
    COUNT,
};

/**
 * Session-only projection for the complete route-aware CC-lane workflow.
 *
 * Settings fields never write Pattern data before Apply. Preview facts are explicit so LVGL
 * and the semantic UX recorder render the same Authored/Resolved/Source/
 * Route/Conflict truth without reconstructing it independently.
 */
struct SequencerCcLaneUiState {
    static constexpr uint16_t ACTION_GUARD_MS = 650;

    // ExclusiveVisibilityStack owns presentation and input authority through
    // this signal. `mode` remains the semantic workflow state.
    Signal<bool> overlayVisible{false};
    Signal<uint32_t, 8> revision{0};
    SequencerCcLaneUiMode mode = SequencerCcLaneUiMode::CLOSED;
    uint8_t selectorIndex = 0;
    uint8_t focusedLane = 0;
    uint8_t focusedStep = 0;
    uint8_t transitionStep = 0;
    SequencerCcLaneTransition selectedTransition =
        SequencerCcLaneTransition::HOLD;
    // NAV-hold uses one discreet current-choice card instead of the complete
    // transition selector used by the explicit Macro-button workflow.
    bool compactTransitionPicker = false;
    // Short-lived confirmation only; cleared with OperationFeedback expiry or
    // the next CC-grid interaction. It never enters project persistence.
    bool transitionAppliedFeedback = false;
    SequencerCcLaneDraftField focusedField =
        SequencerCcLaneDraftField::CONTROLLER;
    SequencerCcLaneDraft draft{};
    bool draftDirty = false;
    // The common path exposes destination + route only. Range/proposal fields
    // remain one NAV tap away and never burden the default creation surface.
    bool advancedSettings = false;

    bool hasAuthoredValue = false;
    uint8_t authoredValue = 0;
    bool hasResolvedValue = false;
    uint8_t resolvedValue = 0;
    core::state::shared::MidiCcCandidateClass winnerClass =
        core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE;
    bool routeValid = true;
    bool laneConflict = false;
    bool macroConflict = false;
    bool acceptedMacroConflict = false;
    bool liveProjection = false;

    std::array<
        core::state::contextual::ContextActionSpec,
        static_cast<size_t>(SequencerCcLaneActionSlot::COUNT)> actions{};
    Signal<core::state::contextual::GuardedActionState, 4> actionGuard{};
    Signal<core::state::contextual::OperationFeedbackState, 4>
        operationFeedback{};

    [[nodiscard]] bool visible() const {
        return mode != SequencerCcLaneUiMode::CLOSED;
    }
    [[nodiscard]] const core::state::contextual::ContextActionSpec& action(
        SequencerCcLaneActionSlot slot
    ) const {
        return actions[static_cast<size_t>(slot)];
    }
    void bump();
    void reset();
};

struct SequencerStepPropertyInlineSelectorState {
    Signal<bool, 6> selecting{false};
    Signal<bool, 6> macroLocalVariationEditActive{false};
    Signal<int, 4> selectedIndex{0};

    int snapshotIndex = 0;
    uint8_t localVariationStepIndex = 0;
    bool snapshotValid = false;
    // A modal shortcut is opened by a long-press while another scope owns the
    // button. Its physical release is delivered after the scope switches back
    // to the sequencer view and must not immediately close this selector.
    bool suppressOpeningRelease = false;

    void reset();
};

enum class SequencerStepContentAction : uint8_t {
    CHORD = 0,
    MICRO_SEQUENCE,
    CYCLE_STATES,
    COUNT,
};

/**
 * Temporary LEFT_BOTTOM semantic action selector for Step focus.
 *
 * It does not own musical data. Applying Chord opens its editor directly;
 * applying Micro-sequence or Cycle states creates/opens the matching child
 * context through SequencerStepEditHandler, the single editing authority.
 */
struct SequencerStepContentSelectorState {
    Signal<bool, 6> selecting{false};
    Signal<SequencerStepContentAction, 6> focusedAction{
        SequencerStepContentAction::CHORD
    };

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

        for (uint16_t step = 0; step < MAX_STEPS; ++step) {
            const auto stepIndex = static_cast<uint8_t>(step);
            if (!nextMask.test(stepIndex)) continue;
            if (!oc::time::deadlineReachedMs(nowMs, hideAtMs[step])) continue;
            nextMask.setBit(stepIndex, false);
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
        if (!oc::time::deadlineReachedMs(nowMs, hideAtMs)) return;
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

    void showRejection(SequencerHistoryRejectionReason reason, uint32_t nowMs);
    void showRejection(SequencerHistoryOpenOutcome outcome, uint32_t nowMs);
    void showRejection(SequencerHistoryGestureOutcome outcome, uint32_t nowMs);

    void update(uint32_t nowMs) {
        if (!visible.get()) return;
        if (!oc::time::deadlineReachedMs(nowMs, hideAtMs)) return;
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
    Signal<bool, 6> feedbackVisible{false};
    Signal<PatternQuickControlItem, 6> focusedItem{
        PatternQuickControlItem::LENGTH
    };
    Signal<int8_t, 4> offsetSteps{0};
    // One bounded invalidation source for detached authoring projections.
    Signal<uint32_t, 8> previewRevision{0};
    uint32_t hideAtMs = 0;

    SequencerPatternQuickControlsState();
    ~SequencerPatternQuickControlsState();

    void showFeedback(uint32_t nowMs);
    void bumpPreview();

    void update(uint32_t nowMs) {
        if (!feedbackVisible.get()) return;
        if (selecting.get()) return;
        if (!oc::time::deadlineReachedMs(nowMs, hideAtMs)) return;
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
    Signal<bool, 8> placing{false};
    Signal<uint8_t, 8> cursorStep{0};
    Signal<oc::note::sequencer::StepBitMask128, 8> selectedMask{};
    Signal<bool, 8> pastePreviewActive{false};
    Signal<SequencerStepPastePreview, 8> pastePreview{SequencerStepPastePreview::NONE};
    Signal<uint32_t, 8> clipboardRevision{0};

    SequencerStepSelectionState();
    ~SequencerStepSelectionState();

    void reset(uint8_t cursor = 0);
    void clearCurrent();

    bool placementActive() const {
        return active.get() && placing.get();
    }

    void setSelected(uint8_t step, bool selected);
    bool selected(uint8_t step) const;
    bool anySelected() const {
        return selectedMask.get().any();
    }
};

/**
 * One bounded Track-paste interaction snapshot.
 *
 * Only revision is observable: the guard, feedback and exact single-Track plan
 * change together, preventing presenters from observing a mixed generation.
 * The clipboard payload itself is synchronously materialized by the transfer
 * transaction; kind/revision and the complete mapping snapshot retain its
 * immutable UI/semantic provenance while activation is queued.
 */
struct SequencerTrackPasteUiState {
    Signal<uint32_t, 8> revision{0};
    core::state::contextual::GuardedActionState guard{};
    core::state::contextual::OperationFeedbackState feedback{};
    core::state::ClipboardTransferPlan plan{};
    core::state::StructureClipboardKind clipboardKind =
        core::state::StructureClipboardKind::NONE;
    uint32_t clipboardRevision = 0;
    uint32_t interactionGeneration = 0;
    uint32_t operationGeneration = 0;
    uint32_t activationGeneration = 0;
    bool detailVisible = false;
    bool buttonOwned = false;
    bool commitConsumed = false;

    [[nodiscard]] bool gestureActive() const {
        return guard.phase ==
                   core::state::contextual::GuardedActionPhase::PRESSED ||
               guard.phase ==
                   core::state::contextual::GuardedActionPhase::ARMED;
    }
    [[nodiscard]] bool inspectable() const { return plan.hasEntries(); }
    void bump();
    void reset();
};

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
struct DrumTrackState;

/**
 * Native-only interaction shell used to validate Drum Track authoring before
 * the persistent TrackKind is introduced.
 *
 * Authored rhythm delegates to the production-neutral Drum domain. The shell
 * remains session-only and cannot leak into project serialization.
 */
enum class DrumTrackUxPrototypePhase : uint8_t {
    INACTIVE = 0,
    TYPE_PICKER,
    GRID,
};

enum class DrumTrackUxPrototypeKind : uint8_t {
    INSTRUMENT = 0,
    DRUM,
};

enum class DrumTrackUxPrototypeProperty : uint8_t {
    STATE = 0,
    PROBABILITY,
    VELOCITY,
    GATE,
    NUDGE,
    COUNT,
};

enum class DrumTrackUxPrototypeDimension : uint8_t {
    MODE = 0,
    LENGTH,
    DIVISION,
    COUNT,
};

enum class DrumTrackUxPrototypeSelector : uint8_t {
    NONE = 0,
    DIMENSION,
    PROPERTY,
};

struct DrumTrackUxPrototypeState {
    // Keep the thin UI shell independent from the cold Drum domain header.
    static constexpr uint8_t LANE_COUNT = 8U;
    static constexpr uint8_t VISIBLE_LANE_COUNT = 8;
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr uint8_t PAGE_COUNT = 16;
    static constexpr uint8_t MAX_STEPS = STEPS_PER_PAGE * PAGE_COUNT;
    static constexpr uint8_t RUNTIME_LANE_CAPACITY = 16U;
    static constexpr uint8_t INVALID_TRACK = 0xFFU;

    bool armed = false;
    DrumTrackUxPrototypePhase phase = DrumTrackUxPrototypePhase::INACTIVE;
    DrumTrackUxPrototypeKind selectedKind =
        DrumTrackUxPrototypeKind::INSTRUMENT;
    DrumTrackUxPrototypeProperty property =
        DrumTrackUxPrototypeProperty::VELOCITY;
    DrumTrackUxPrototypeDimension dimension =
        DrumTrackUxPrototypeDimension::LENGTH;
    DrumTrackUxPrototypeSelector selector =
        DrumTrackUxPrototypeSelector::NONE;
    uint8_t targetTrack = INVALID_TRACK;
    uint8_t selectedLane = 0;
    uint8_t focusedStep = 0;
    uint8_t page = 0;
    DrumTrackUxPrototypeProperty selectorSnapshotProperty =
        DrumTrackUxPrototypeProperty::VELOCITY;
    DrumTrackUxPrototypeDimension selectorSnapshotDimension =
        DrumTrackUxPrototypeDimension::LENGTH;
    uint8_t selectorSnapshotLane = 0;
    uint8_t selectorSnapshotTimingMode = 0;
    uint8_t selectorSnapshotLength = 0;
    uint8_t selectorSnapshotStepsPerBeat = 0;
    // The fixed-capacity authored payload is cold and prototype-only. Keep it
    // out of the hot SequencerState object and in PSRAM.
    core::app::ExtmemUniquePtr<DrumTrackState> drumTrack;
    Signal<uint32_t, 8> revision{0};
    // Realtime playback publishes only a bounded lane-position projection.
    // It is kept separate from authored revision so the Step Editor is not
    // reformatted every time a playhead advances.
    std::array<uint8_t, RUNTIME_LANE_CAPACITY> playheadSteps{};
    uint16_t playheadValidMask = 0U;
    bool playbackActive = false;
    Signal<uint32_t, 4> playbackRevision{0};

    DrumTrackUxPrototypeState();
    ~DrumTrackUxPrototypeState();

    [[nodiscard]] bool active() const {
        return phase != DrumTrackUxPrototypePhase::INACTIVE;
    }
    [[nodiscard]] bool pickerVisible() const {
        return phase == DrumTrackUxPrototypePhase::TYPE_PICKER;
    }
    [[nodiscard]] bool gridVisible() const {
        return phase == DrumTrackUxPrototypePhase::GRID && drumTrack != nullptr;
    }
    [[nodiscard]] bool selectorVisible() const {
        return selector != DrumTrackUxPrototypeSelector::NONE;
    }

    void reset();
    void arm();
    void openTypePicker(uint8_t track);
    void moveKind(float delta);
    void enterGrid();
    void close();
    void moveLane(float delta);
    void moveFocusedStep(float delta);
    void movePage(int direction);
    void openDimensionSelector();
    void openPropertySelector();
    void moveSelector(float delta);
    void applySelector();
    void cancelSelector();
    void setSelectedLaneTimingCustom(bool custom);
    void setSelectedLaneLength(uint8_t length);
    void setSelectedLaneStepsPerBeat(uint8_t stepsPerBeat);
    void toggleVisibleStep(uint8_t indexInPage);
    void setVisibleStepEnabled(uint8_t indexInPage, bool enabled);
    void setVisibleStepVelocity(uint8_t indexInPage, uint8_t velocity);
    void setVisibleStepGate(uint8_t indexInPage, uint16_t gatePercent);
    void setVisibleStepNudge(uint8_t indexInPage, int8_t nudgePercent);
    void setVisibleStepProbability(uint8_t indexInPage, uint8_t probability);
    [[nodiscard]] bool stepInRange(uint8_t lane, uint8_t step) const;
    bool focusStep(uint8_t lane, uint8_t step);
    bool setStepEnabled(uint8_t lane, uint8_t step, bool enabled);
    bool setStepVelocity(uint8_t lane, uint8_t step, uint8_t velocity);
    bool setStepGate(uint8_t lane, uint8_t step, uint16_t gatePercent);
    bool setStepNudge(uint8_t lane, uint8_t step, int8_t nudgePercent);
    bool setStepProbability(uint8_t lane, uint8_t step, uint8_t probability);
    void publishPlayback(
        const std::array<uint8_t, RUNTIME_LANE_CAPACITY>& laneSteps,
        uint16_t validMask,
        bool playing
    );
    [[nodiscard]] uint8_t visibleStep(uint8_t indexInPage) const;
    [[nodiscard]] uint8_t visibleLane(uint8_t row) const;
    void bump();
};
#endif

struct SequencerStructureUiState {
    Signal<uint8_t, 4> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;
    SequencerStepSelectionState stepSelection;
    SequencerTrackPasteUiState trackPaste;

    SequencerStructureUiState();
    ~SequencerStructureUiState();

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset();
};

}  // namespace core::state::sequencer

#include "SequencerStepEditHandler.hpp"

#include <algorithm>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include <utility>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerStepChordEditorWorkflow.hpp"
#include "SequencerStepContextRowWorkflow.hpp"
#include "SequencerStepContentDraftWorkflow.hpp"
#include "SequencerStepEditSessionWorkflow.hpp"
#include "SequencerStepValueRowWorkflow.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler {
namespace step_chord_editor_workflow =
    core::handler::sequencer::step_chord_editor_workflow;
namespace step_context_row_workflow =
    core::handler::sequencer::step_context_row_workflow;
namespace step_edit_session_workflow =
    core::handler::sequencer::step_edit_session_workflow;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace step_value_row_workflow =
    core::handler::sequencer::step_value_row_workflow;

namespace {

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks
) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        tracks.projectScaleSettings(),
        core::state::sequencer::authoringPattern(sequencer).scalePolicy,
        core::state::sequencer::authoringPattern(sequencer).scaleOverride
    );
}

FLASHMEM bool editedStepHasChordState(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(sequencer)
    );
    if (graph == nullptr) return false;
    const auto* node = graph->stepNode(
        core::state::sequencer::activeContentStepNodeId(sequencer, step)
    );
    return node != nullptr &&
           (node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE) ||
            node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
}

}  // namespace

FLASHMEM SequencerStepEditHandler::SequencerStepEditHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID overlayScope,
    oc::type::ScopeID stepPresetOverlayScope,
    TimeProviderFn timeProvider
)
    : overlay_state_(state.overlays)
    , sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , structure_clipboard_(state.structureClipboard)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , history_(state.history)
    , step_presets_(state.stepPresets)
    , step_preset_picker_(sequencer_, step_presets_, overlays)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , overlay_scope_(overlayScope)
    , step_preset_overlay_scope_(stepPresetOverlayScope)
    , time_provider_(timeProvider ? timeProvider : core::time_compat::millis)
{
    setupBindings();
}

void SequencerStepEditHandler::update(uint32_t nowMs) {
    const auto pickerOutcome = step_preset_picker_.update(nowMs);
    if (pickerOutcome == SequencerStepPresetPickerOutcome::APPLIED ||
        pickerOutcome == SequencerStepPresetPickerOutcome::CANCELLED) {
        handleStepPresetOutcome(pickerOutcome);
    }
    if (step_preset_auto_close_pending_ &&
        !step_preset_action_press_active_ &&
        oc::time::deadlineReachedMs(nowMs, step_preset_auto_close_at_ms_)) {
        closeStepPresetPicker();
    }
}

FLASHMEM void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    if (!step_edit_session_workflow::openForMacroInPage(
            sequencer_,
            history_,
            overlays_,
            history_snapshot_,
            history_snapshot_valid_,
            indexInPage
        )) {
        return;
    }
    step_retarget_active_ = false;
    configureOptForFocusedRow();
}

FLASHMEM bool SequencerStepEditHandler::openFocusedStepAtRow(uint8_t row) {
    const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
    if (length == 0) return false;
    const uint8_t focused = std::min<uint8_t>(
        sequencer_.focusedStep.get(),
        static_cast<uint8_t>(length - 1U)
    );
    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(focused));
    const uint8_t indexInPage = static_cast<uint8_t>(
        focused % core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    if (!step_edit_session_workflow::openForMacroInPage(
            sequencer_,
            history_,
            overlays_,
            history_snapshot_,
            history_snapshot_valid_,
            indexInPage
        )) {
        return false;
    }
    step_retarget_active_ = false;
    sequencer_.stepEdit.focusedRow.set(row);
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool SequencerStepEditHandler::openFocusedStepContentAtRow(
    uint8_t row
) {
    if (!step_edit_rows::isChord(row) &&
        !step_edit_rows::isContext(row)) {
        return false;
    }
    if (!openFocusedStepAtRow(row)) return false;

    if (step_edit_rows::isChord(row)) {
        openChordEditor();
        if (chordEditorActive()) return true;
        closeStepEdit();
        return false;
    }

    if (activateFocusedContextRow()) return true;
    closeStepEdit();
    return false;
}

FLASHMEM void SequencerStepEditHandler::commitStepEditHistory() {
    step_edit_session_workflow::commitHistory(
        sequencer_,
        history_,
        history_snapshot_,
        history_snapshot_valid_
    );
}

FLASHMEM void SequencerStepEditHandler::backFromStepEdit() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        // Back from the explicit decision surface means Continue editing.
        sequencer_.stepContentDraft.hideExitPrompt();
        return;
    }

    if (chordEditorActive() && sequencer_.stepContentDraft.active.get()) {
        // A Chord opened inside a new Micro/Cycle belongs to that outer draft;
        // leaving the detail editor must not discard the owning content graph.
        if (sequencer_.stepContentDraft.kind.get() !=
            core::state::sequencer::SequencerStepContentDraftKind::CHORD) {
            closeChordEditor();
            return;
        }
        const auto result = sequencer::step_content_draft_workflow::requestBack(
            sequencer_
        );
        if (result == sequencer::step_content_draft_workflow::BackResult::DISCARDED) {
            closeChordEditor();
        }
        return;
    }

    if (chordEditorActive()) {
        closeChordEditor();
        return;
    }

    if (core::state::sequencer::isChildContentView(sequencer_) &&
        sequencer_.stepContentDraft.active.get()) {
        const auto result = sequencer::step_content_draft_workflow::requestBack(
            sequencer_
        );
        if (result !=
            sequencer::step_content_draft_workflow::BackResult::DISCARDED) {
            return;
        }
        if (step_edit_session_workflow::backToParentContent(
                sequencer_,
                history_,
                history_snapshot_,
                history_snapshot_valid_
            )) {
            configureOptForFocusedRow();
        }
        return;
    }

    if (step_edit_session_workflow::backToParentContent(
            sequencer_,
            history_,
            history_snapshot_,
            history_snapshot_valid_
        )) {
        configureOptForFocusedRow();
        return;
    }

    closeStepEdit();
}

FLASHMEM void SequencerStepEditHandler::closeStepEdit() {
    if (chordEditorActive() && sequencer_.stepContentDraft.active.get()) {
        backFromStepEdit();
        if (sequencer_.stepContentDraft.active.get()) return;
    }
    step_retarget_active_ = false;
    step_edit_session_workflow::close(
        sequencer_,
        history_,
        context_release_latch_,
        overlays_,
        history_snapshot_,
        history_snapshot_valid_
    );
}

FLASHMEM void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        sequencer::step_content_draft_workflow::moveExitChoice(sequencer_, delta);
        return;
    }

    if (chordEditorActive()) {
        moveChordEditorFocus(delta);
        return;
    }

    const int current = step_edit_rows::navigationIndexForRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        static_cast<int>(step_edit_rows::NAVIGATION_ORDER.size())
    );
    sequencer_.stepEdit.contextHold.clear();
    sequencer_.stepEdit.localVariationEditActive.set(false);
    sequencer_.stepEdit.focusedRow.set(step_edit_rows::rowForNavigationIndex(next));

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::retargetEditedStep(float delta) {
    if (!nav::hasTurnDelta(delta) || chordEditorActive()) return;
    if (step_edit_session_workflow::retargetRootStep(
            sequencer_,
            history_,
            history_snapshot_,
            history_snapshot_valid_,
            nav::turnStep(delta)
        )) {
        configureOptForFocusedRow();
    }
}

FLASHMEM void SequencerStepEditHandler::activateFocusedRowOrClose() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        confirmStepContentDraftExitChoice();
        return;
    }

    auto& edit = sequencer_.stepEdit;
    const uint8_t focusedRow = edit.focusedRow.get();

    if (chordEditorActive()) {
        backFromStepEdit();
        return;
    }

    if (step_edit_rows::isProperty(focusedRow)) {
        // Scalar rows are already edited live with OPT. NAV is deliberately a
        // harmless confirmation so the same gesture never means "close" only
        // for Note/Velocity/Gate/Nudge.
        return;
    }

    if (step_edit_rows::isChord(focusedRow)) {
        openChordEditor();
        return;
    }

    if (step_edit_rows::isActivated(focusedRow)) {
        uint8_t abs = 0;
        if (!editedStepInRange(abs)) return;
        if (core::state::sequencer::toggleActiveContentStep(sequencer_, abs)) {
            configureOptForFocusedRow();
        }
        return;
    }

    if (step_edit_rows::isContext(focusedRow)) {
        (void)activateFocusedContextRow();
        return;
    }
}

FLASHMEM bool SequencerStepEditHandler::activateFocusedContextRow() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    const auto result = step_context_row_workflow::openOrCreateFocusedContextChild(
        sequencer_,
        step
    );
    if (!result.opened) return false;

    if (!result.draft && history_snapshot_valid_) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        if (core::state::sequencer::captureHistorySnapshot(sequencer_, after) &&
            !core::state::sequencer::sameMusicalHistorySnapshot(history_snapshot_, after)) {
            history_.recordPattern(
                std::move(history_snapshot_),
                std::move(after),
                core::state::sequencer::SequencerHistoryDescriptor{
                    .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                    .stepIndex = step,
                    .property = core::state::sequencer::StepProperty::NOTE,
                }
            );
        }
        history_snapshot_valid_ = false;
    }
    if (result.draft) {
        history_snapshot_valid_ = false;
    }

    overlays_.hide();
    sequencer_.stepEdit.reset();
    return true;
}

FLASHMEM void SequencerStepEditHandler::setFocusedValue(float normalized) {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (chordEditorActive()) {
        setFocusedChordFieldValue(normalized);
        return;
    }
    if (step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get())) {
        return;
    }

    uint8_t abs = 0;
    if (!editedStepInRange(abs)) return;
    step_value_row_workflow::setFocusedRowValue(
        sequencer_,
        abs,
        effectiveScaleSettings(sequencer_, tracks_),
        normalized
    );
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedRow() {
    if (chordEditorActive()) {
        configureOptForFocusedChordField();
        return;
    }
    if (step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get())) {
        return;
    }

    uint8_t abs = 0;
    if (!editedStepInRange(abs)) return;
    step_value_row_workflow::configureFocusedRowEncoder(
        encoders_,
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
        sequencer_,
        abs,
        effectiveScaleSettings(sequencer_, tracks_)
    );
}

FLASHMEM void SequencerStepEditHandler::openChordEditor() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    const bool existed = editedStepHasChordState(sequencer_, step);
    const auto nodeId =
        core::state::sequencer::activeContentStepNodeId(sequencer_, step);
    bool startedDraft = false;
    if (!existed && !sequencer_.stepContentDraft.active.get()) {
        startedDraft = core::state::sequencer::beginStepContentDraft(
            sequencer_,
            core::state::sequencer::SequencerStepContentDraftKind::CHORD,
            step,
            nodeId
        );
        if (!startedDraft) return;
    }

    step_chord_editor_workflow::open(sequencer_);
    if (!existed) {
        // The seeded musical default is the pristine creation baseline: Back
        // immediately after opening abandons it without a confirmation.
        if (core::state::sequencer::isRootContentView(sequencer_)) {
            step_chord_editor_workflow::setFocusedFieldValue(
                sequencer_,
                step,
                effectiveScaleSettings(sequencer_, tracks_),
                1.0f
            );
        } else {
            const auto scale = effectiveScaleSettings(sequencer_, tracks_);
            auto chord = core::state::sequencer::resolveStepChordUiState(
                sequencer_,
                step
            );
            const auto projection =
                core::state::sequencer::resolveActiveContentStepProjection(
                    sequencer_,
                    step,
                    scale
                );
            core::state::sequencer::resolveStepChordPreview(
                chord,
                projection,
                scale
            );
            if (core::state::sequencer::setAuthoringNodeChordSpec(
                    sequencer_,
                    nodeId,
                    chord.spec
                )) {
                core::state::sequencer::notifyStepContentDraftMutation(sequencer_);
            }
        }
        if (startedDraft) {
            core::state::sequencer::markStepContentDraftPristine(sequencer_);
        }
    }
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::closeChordEditor() {
    step_chord_editor_workflow::close(sequencer_);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::applyStepContentDraft() {
    history_.commitCoalescedPatternEdit();
    if (!sequencer::step_content_draft_workflow::apply(
            sequencer_,
            tracks_,
            history_
        )) {
        return;
    }

    // The draft owns its single Undo entry. Rebase the surrounding Step Edit
    // session so closing the editor cannot record the same change twice.
    history_snapshot_valid_ = core::state::sequencer::captureHistorySnapshot(
        sequencer_,
        history_snapshot_
    );
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::confirmStepContentDraftExitChoice() {
    const bool wasChordEditor = chordEditorActive();
    const bool wasChildContent = core::state::sequencer::isChildContentView(
        sequencer_
    );
    const auto result = sequencer::step_content_draft_workflow::applyExitChoice(
        sequencer_,
        tracks_,
        history_
    );
    using Result = sequencer::step_content_draft_workflow::BackResult;
    if (result != Result::DISCARDED && result != Result::SAVED) return;

    if (result == Result::SAVED) {
        history_snapshot_valid_ = core::state::sequencer::captureHistorySnapshot(
            sequencer_,
            history_snapshot_
        );
    }
    if (wasChordEditor) {
        closeChordEditor();
        return;
    }
    if (wasChildContent && step_edit_session_workflow::backToParentContent(
            sequencer_,
            history_,
            history_snapshot_,
            history_snapshot_valid_
        )) {
        configureOptForFocusedRow();
    }
}

FLASHMEM void SequencerStepEditHandler::moveChordEditorFocus(float delta) {
    step_chord_editor_workflow::moveFocus(sequencer_, delta);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::setFocusedChordFieldValue(float normalized) {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_chord_editor_workflow::setFocusedFieldValue(
        sequencer_,
        step,
        effectiveScaleSettings(sequencer_, tracks_),
        normalized
    );
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedChordField() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_chord_editor_workflow::configureFocusedFieldEncoder(
        encoders_,
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
        sequencer_,
        step,
        effectiveScaleSettings(sequencer_, tracks_)
    );
}

FLASHMEM void SequencerStepEditHandler::resetFocusedChordFieldToDefault() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    if (!step_chord_editor_workflow::resetFocusedFieldToDefault(
            sequencer_,
            step,
            effectiveScaleSettings(sequencer_, tracks_)
        )) return;
    configureOptForFocusedRow();
}

FLASHMEM bool SequencerStepEditHandler::chordEditorActive() const {
    return step_chord_editor_workflow::active(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::editedStepInRange(uint8_t& step) const {
    return step_edit_session_workflow::editedStepInRange(sequencer_, step);
}

FLASHMEM void SequencerStepEditHandler::maybeCloseFromMacro(uint8_t indexInPage) {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (step_edit_session_workflow::shouldCloseFromMacro(
            sequencer_,
            indexInPage
        )) {
        closeStepEdit();
    }
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsValueRow() const {
    if (chordEditorActive()) return true;
    return !step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get()) &&
           step_value_row_workflow::focusedRowIsValue(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsContextRow() const {
    if (chordEditorActive()) return false;
    return step_context_row_workflow::focusedRowIsContext(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowSupportsLocalVariation() const {
    if (chordEditorActive()) return false;
    return step_value_row_workflow::focusedRowSupportsLocalVariation(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::focusedContextHasChild() const {
    if (!focusedRowIsContextRow()) return false;

    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    return step_context_row_workflow::focusedContextHasChild(sequencer_, step);
}

FLASHMEM bool SequencerStepEditHandler::canPasteFocusedStepContent() const {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    return step_context_row_workflow::canPasteFocusedContextChild(
        sequencer_,
        step,
        structure_clipboard_
    );
}

FLASHMEM void SequencerStepEditHandler::resetFocusedValueRowToDefault() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedRowIsValueRow()) return;

    if (chordEditorActive()) {
        resetFocusedChordFieldToDefault();
        return;
    }

    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    if (!step_value_row_workflow::resetFocusedRowToDefault(sequencer_, step)) return;
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::recordContextMutation(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    bool beforeCaptured
) {
    if (!beforeCaptured) return;

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    if (!core::state::sequencer::captureHistorySnapshot(sequencer_, after)) return;
    if (core::state::sequencer::sameMusicalHistorySnapshot(before, after)) return;

    history_.recordPattern(
        std::move(before),
        std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = sequencer_.stepEdit.stepIndex.get(),
            .property = core::state::sequencer::StepProperty::NOTE,
            .hasValue = false,
        }
    );

    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
}

FLASHMEM void SequencerStepEditHandler::clearFocusedContextChild() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedContextHasChild()) return;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    history_.commitCoalescedPatternEdit();

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    bool beforeCaptured = false;
    if (history_snapshot_valid_) {
        before = std::move(history_snapshot_);
        beforeCaptured = true;
        history_snapshot_valid_ = false;
    } else {
        beforeCaptured = core::state::sequencer::captureHistorySnapshot(sequencer_, before);
    }

    const bool changed = step_context_row_workflow::clearFocusedContextChild(
        sequencer_,
        step
    );
    if (!changed) {
        if (!history_snapshot_valid_ && beforeCaptured) {
            history_snapshot_ = std::move(before);
            history_snapshot_valid_ = true;
        }
        return;
    }
    recordContextMutation(std::move(before), beforeCaptured);
}

FLASHMEM void SequencerStepEditHandler::copyFocusedStepContent() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedContextHasChild()) return;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_context_row_workflow::copyFocusedContextChildToClipboard(
        sequencer_,
        step,
        structure_clipboard_
    );
}

FLASHMEM void SequencerStepEditHandler::pasteFocusedStepContent() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!canPasteFocusedStepContent()) return;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    history_.commitCoalescedPatternEdit();

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    bool beforeCaptured = false;
    if (history_snapshot_valid_) {
        before = std::move(history_snapshot_);
        beforeCaptured = true;
        history_snapshot_valid_ = false;
    } else {
        beforeCaptured = core::state::sequencer::captureHistorySnapshot(sequencer_, before);
    }

    const bool changed = step_context_row_workflow::pasteFocusedContextChildFromClipboard(
        sequencer_,
        step,
        structure_clipboard_
    );
    if (!changed) {
        if (!history_snapshot_valid_ && beforeCaptured) {
            history_snapshot_ = std::move(before);
            history_snapshot_valid_ = true;
        }
        return;
    }
    recordContextMutation(std::move(before), beforeCaptured);
}

}  // namespace core::handler

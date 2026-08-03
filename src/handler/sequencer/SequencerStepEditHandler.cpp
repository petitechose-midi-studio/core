#include "SequencerStepEditHandler.hpp"

#include "SequencerChordEditOps.hpp"
#include "SequencerStepChordEditorWorkflow.hpp"
#include "SequencerStepContentDraftWorkflow.hpp"
#include "SequencerStepContextRowWorkflow.hpp"
#include "SequencerStepEditSessionWorkflow.hpp"
#include "SequencerStepValueRowWorkflow.hpp"

#include <algorithm>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler {
namespace step_chord_editor_workflow = core::handler::sequencer::step_chord_editor_workflow;
namespace step_context_row_workflow = core::handler::sequencer::step_context_row_workflow;
namespace step_edit_session_workflow = core::handler::sequencer::step_edit_session_workflow;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace step_value_row_workflow = core::handler::sequencer::step_value_row_workflow;

namespace {

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        tracks.projectScaleSettings(),
        core::state::sequencer::authoringPattern(sequencer).scalePolicy,
        core::state::sequencer::authoringPattern(sequencer).scaleOverride);
}

FLASHMEM bool editedStepHasChordState(const core::state::sequencer::SequencerState& sequencer,
                                      uint8_t step) {
    const auto* graph =
        core::state::sequencer::graphView(core::state::sequencer::authoringPattern(sequencer));
    if (graph == nullptr) return false;
    const auto* node =
        graph->stepNode(core::state::sequencer::activeContentStepNodeId(sequencer, step));
    return node != nullptr && (node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE) ||
                               node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
}

FLASHMEM core::state::sequencer::SequencerCoalescedPatternPayloadPlan stepEditPayloadPlan(
    const core::state::sequencer::SequencerState& sequencer, bool mayGrowGraph) {
    using Plan = core::state::sequencer::SequencerCoalescedPatternPayloadPlan;
    if (mayGrowGraph) return Plan::FullWithProspectiveGraph;
    return core::state::sequencer::isChildContentView(sequencer) ? Plan::FullCurrentPayload
                                                                 : Plan::FlatOnly;
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor stepEditDescriptor(uint8_t step) {
    return core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
        .stepIndex = step,
        .property = core::state::sequencer::StepProperty::NOTE,
    };
}

FLASHMEM core::state::sequencer::SequencerCoalescedPatternPayloadPlan stepResetPayloadPlan(
    const core::state::sequencer::SequencerState& sequencer, uint8_t step) {
    using Plan = core::state::sequencer::SequencerCoalescedPatternPayloadPlan;
    if (core::state::sequencer::isChildContentView(sequencer)) { return Plan::FullCurrentPayload; }

    const uint8_t row = sequencer.stepEdit.focusedRow.get();
    if (!step_edit_rows::isProperty(row)) return Plan::FlatOnly;

    const auto* graph =
        core::state::sequencer::graphView(core::state::sequencer::authoringPattern(sequencer));
    const auto* node =
        graph == nullptr
            ? nullptr
            : graph->stepNode(core::state::sequencer::activeContentStepNodeId(sequencer, step));
    return node != nullptr && core::state::sequencer::nodeLocalVariationRange(
                                  *node, step_edit_rows::propertyForRow(row)) != 0U
               ? Plan::FullCurrentPayload
               : Plan::FlatOnly;
}

}  // namespace

FLASHMEM SequencerStepEditHandler::SequencerStepEditHandler(
    StateRefs state, oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope, oc::type::ScopeID overlayScope,
    oc::type::ScopeID presetLibraryOverlayScope, TimeProviderFn timeProvider)
    : overlay_state_(state.overlays), sequencer_(state.sequencer), tracks_(state.tracks),
      structure_clipboard_(state.structureClipboard), track_ui_(state.trackNavigation),
      pattern_pitch_settings_(state.patternPitchSettings), navigation_focus_(state.navigationFocus),
      history_(state.history), step_presets_(state.stepPresets), chord_presets_(state.chordPresets),
      step_preset_library_adapter_(sequencer_, step_presets_),
      chord_preset_library_adapter_(sequencer_, chord_presets_),
      preset_library_(sequencer_, overlays), overlays_(overlays), encoders_(encoders),
      buttons_(buttons), sequencer_view_scope_(sequencerViewScope), overlay_scope_(overlayScope),
      preset_library_overlay_scope_(presetLibraryOverlayScope),
      time_provider_(timeProvider ? timeProvider : core::time_compat::millis) {
    setupBindings();
}

void SequencerStepEditHandler::update(uint32_t nowMs) {
    if (pitch_context_settings_open_ && pattern_pitch_settings_.flowPhase.get() ==
                                            core::state::PatternPitchSettingsFlowPhase::CLOSED) {
        pitch_context_settings_open_ = false;
        configureOptForFocusedRow();
    }

    const auto presetResult = preset_library_.update(nowMs);
    if (presetResult.outcome == SequencerPresetLibraryOutcome::LOADED ||
        presetResult.outcome == SequencerPresetLibraryOutcome::QUEUED ||
        presetResult.outcome == SequencerPresetLibraryOutcome::CANCELLED) {
        handlePresetLibraryResult(presetResult);
    }
    if (preset_library_auto_close_pending_ && !preset_library_action_press_active_ &&
        oc::time::deadlineReachedMs(nowMs, preset_library_auto_close_at_ms_)) {
        closePresetLibrary();
    }
}

FLASHMEM void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    if (!step_edit_session_workflow::openForMacroInPage(sequencer_, history_, overlays_,
                                                        indexInPage)) {
        return;
    }
    step_retarget_active_ = false;
    configureOptForFocusedRow();
}

FLASHMEM bool SequencerStepEditHandler::openFocusedStepAtRow(uint8_t row) {
    const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
    if (length == 0) return false;
    const uint8_t focused =
        std::min<uint8_t>(sequencer_.focusedStep.get(), static_cast<uint8_t>(length - 1U));
    if (!step_edit_session_workflow::openForStep(sequencer_, history_, overlays_, focused)) {
        return false;
    }
    step_retarget_active_ = false;
    sequencer_.stepEdit.focusedRow.set(row);
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool SequencerStepEditHandler::openFocusedStepContentAtRow(uint8_t row) {
    if (!step_edit_rows::isChord(row) && !step_edit_rows::isContext(row)) { return false; }
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

FLASHMEM bool SequencerStepEditHandler::commitStepEditHistory() {
    return step_edit_session_workflow::commitHistory(sequencer_, history_);
}

FLASHMEM bool SequencerStepEditHandler::beginPreparedPatternMutation(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    core::state::sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal) {
    if (sequencer_.stepContentDraft.active.get()) return true;
    const auto outcome =
        history_.beginPreparedPatternEdit(owner, key, payloadPlan, descriptor,
                                             compactGraphOnSeal);
    if (core::state::sequencer::sequencerHistoryOpenAccepted(outcome)) return true;
    sequencer_.historyFeedback.showRejection(outcome, oc::time::millis());
    return false;
}

FLASHMEM bool SequencerStepEditHandler::sealPreparedPatternMutation(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key, bool changed,
    core::state::sequencer::SequencerHistoryDescriptor descriptor) {
    if (sequencer_.stepContentDraft.active.get()) return true;
    const auto outcome = history_.sealPreparedPatternEdit(owner, key, changed, descriptor);
    if (!core::state::sequencer::sequencerPreparedPatternEditSealFailed(outcome)) return true;
    sequencer_.historyFeedback.showRejection(
        core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
        oc::time::millis());
    return false;
}

FLASHMEM bool SequencerStepEditHandler::commitPreparedPatternMutation(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner) {
    if (sequencer_.stepContentDraft.active.get()) return true;
    if (history_.commitPreparedPatternEdit(owner) !=
           core::state::sequencer::SequencerPreparedPatternEditCommitOutcome::Failed) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(
        core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
        oc::time::millis());
    return false;
}

FLASHMEM void SequencerStepEditHandler::backFromStepEdit() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        // Back from the explicit decision surface means Continue editing.
        sequencer_.stepContentDraft.hideExitPrompt();
        return;
    }

    if (chordEditorActive() && step_chord_editor_workflow::cancelSubEditor(sequencer_)) {
        configureOptForFocusedRow();
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
        const auto result = sequencer::step_content_draft_workflow::requestBack(sequencer_);
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
        const auto result = sequencer::step_content_draft_workflow::requestBack(sequencer_);
        if (result != sequencer::step_content_draft_workflow::BackResult::DISCARDED) { return; }
        if (step_edit_session_workflow::backToParentContent(sequencer_, history_)) {
            configureOptForFocusedRow();
        }
        return;
    }

    if (step_edit_session_workflow::backToParentContent(sequencer_, history_)) {
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
    if (!step_edit_session_workflow::close(sequencer_, history_, context_release_latch_,
                                           overlays_)) {
        return;
    }
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

    const int current = step_edit_rows::navigationIndexForRow(sequencer_.stepEdit.focusedRow.get());
    const int next = nav::nextWrappedIndex(
        delta, current, static_cast<int>(step_edit_rows::NAVIGATION_ORDER.size()));
    sequencer_.stepEdit.contextHold.clear();
    sequencer_.stepEdit.localVariationEditActive.set(false);
    sequencer_.stepEdit.focusedRow.set(step_edit_rows::rowForNavigationIndex(next));

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::retargetEditedStep(float delta) {
    if (!nav::hasTurnDelta(delta) || chordEditorActive()) return;
    if (step_edit_session_workflow::retargetRootStep(sequencer_, history_, nav::turnStep(delta))) {
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
        if (!step_chord_editor_workflow::formulaEditorActive(sequencer_) &&
            !step_chord_editor_workflow::sourceSelectorActive(sequencer_) &&
            sequencer_.stepEdit.chordEditor.focusedField.get() ==
                core::state::sequencer::SequencerChordEditField::PITCH_CONTEXT) {
            openPitchContextSettings();
            return;
        }
        uint8_t step = 0;
        if (editedStepInRange(step) &&
            step_chord_editor_workflow::activateFocusedItem(
                sequencer_, step, effectiveScaleSettings(sequencer_, tracks_))) {
            configureOptForFocusedRow();
            return;
        }
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
        const bool before = core::state::sequencer::activeContentStepEnabled(sequencer_, abs);
        auto descriptor = stepEditDescriptor(abs);
        descriptor.hasValue = true;
        descriptor.beforeValue = before ? 1 : 0;
        descriptor.afterValue = before ? 0 : 1;
        constexpr auto owner =
            core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
        if (!beginPreparedPatternMutation(owner, abs, stepEditPayloadPlan(sequencer_, false),
                                          descriptor)) {
            return;
        }
        const bool changed = core::state::sequencer::toggleActiveContentStep(sequencer_, abs);
        if (!sealPreparedPatternMutation(owner, abs, changed, descriptor)) { return; }
        if (changed) { configureOptForFocusedRow(); }
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
    if (!commitStepEditHistory()) return false;

    const auto result =
        step_context_row_workflow::openOrCreateFocusedContextChild(sequencer_, step);
    if (!result.opened) return false;

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
    if (step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get())) { return; }

    uint8_t abs = 0;
    if (!editedStepInRange(abs)) return;
    auto descriptor = stepEditDescriptor(abs);
    if (step_edit_rows::isProperty(sequencer_.stepEdit.focusedRow.get())) {
        descriptor.property = step_edit_rows::propertyForRow(sequencer_.stepEdit.focusedRow.get());
    }
    const bool mayGrowGraph =
        sequencer_.stepEdit.localVariationEditActive.get() && focusedRowSupportsLocalVariation();
    constexpr auto owner =
        core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
    if (!beginPreparedPatternMutation(owner, abs, stepEditPayloadPlan(sequencer_, mayGrowGraph),
                                      descriptor)) {
        return;
    }
    const bool changed = step_value_row_workflow::setFocusedRowValue(
        sequencer_, abs, effectiveScaleSettings(sequencer_, tracks_), normalized);
    if (!sealPreparedPatternMutation(owner, abs, changed, descriptor)) return;
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedRow() {
    if (chordEditorActive()) {
        configureOptForFocusedChordField();
        return;
    }
    if (step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get())) { return; }

    uint8_t abs = 0;
    if (!editedStepInRange(abs)) return;
    step_value_row_workflow::configureFocusedRowEncoder(
        encoders_, static_cast<oc::type::EncoderID>(Config::EncoderID::OPT), sequencer_, abs,
        effectiveScaleSettings(sequencer_, tracks_));
}

FLASHMEM void SequencerStepEditHandler::openChordEditor() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    const bool existed = editedStepHasChordState(sequencer_, step);
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer_, step);
    bool startedDraft = false;
    // Every Chord editor is transactional. Existing Local/Parent state is
    // copied into the same lightweight Chord draft used for creation; a Chord
    // opened inside a Micro/Cycle draft keeps using that outer draft.
    if (!sequencer_.stepContentDraft.active.get()) {
        if (!commitStepEditHistory()) return;
        startedDraft = core::state::sequencer::beginStepContentDraft(
            sequencer_, core::state::sequencer::SequencerStepContentDraftKind::CHORD, step, nodeId);
        if (!startedDraft) return;
    }

    step_chord_editor_workflow::open(sequencer_);
    if (!existed) {
        // The seeded musical default is the pristine creation baseline: Back
        // immediately after opening abandons it without a confirmation.
        if (core::state::sequencer::isRootContentView(sequencer_)) {
            const auto scale = effectiveScaleSettings(sequencer_, tracks_);
            (void)core::handler::sequencer::chord_edit_ops::createDefaultLocalChord(
                sequencer_, step,
                core::state::sequencer::pitchContextUsesScaleDegrees(
                    core::state::sequencer::authoringPattern(sequencer_).pitchEditMode, scale));
        }
        if (startedDraft) { core::state::sequencer::markStepContentDraftPristine(sequencer_); }
    }
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::closeChordEditor() {
    step_chord_editor_workflow::close(sequencer_);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::applyStepContentDraft() {
    if (!commitStepEditHistory()) return;
    if (!sequencer::step_content_draft_workflow::apply(sequencer_, tracks_, history_)) { return; }

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::confirmStepContentDraftExitChoice() {
    const bool wasChordEditor = chordEditorActive();
    const bool wasChildContent = core::state::sequencer::isChildContentView(sequencer_);
    const auto result =
        sequencer::step_content_draft_workflow::applyExitChoice(sequencer_, tracks_, history_);
    using Result = sequencer::step_content_draft_workflow::BackResult;
    if (result != Result::DISCARDED && result != Result::SAVED) return;

    if (wasChordEditor) {
        closeChordEditor();
        return;
    }
    if (wasChildContent && step_edit_session_workflow::backToParentContent(sequencer_, history_)) {
        configureOptForFocusedRow();
    }
}

FLASHMEM void SequencerStepEditHandler::moveChordEditorFocus(float delta) {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    step_chord_editor_workflow::moveFocus(sequencer_, step,
                                          effectiveScaleSettings(sequencer_, tracks_), delta);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::setFocusedChordFieldValue(float normalized) {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_chord_editor_workflow::setFocusedFieldValue(
        sequencer_, step, effectiveScaleSettings(sequencer_, tracks_), normalized);
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedChordField() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_chord_editor_workflow::configureFocusedFieldEncoder(
        encoders_, static_cast<oc::type::EncoderID>(Config::EncoderID::OPT), sequencer_, step,
        effectiveScaleSettings(sequencer_, tracks_));
}

FLASHMEM void SequencerStepEditHandler::resetFocusedChordFieldToDefault() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    if (!step_chord_editor_workflow::resetFocusedFieldToDefault(
            sequencer_, step, effectiveScaleSettings(sequencer_, tracks_)))
        return;
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::toggleChordSourceSelector() {
    if (!chordEditorActive() || sequencer_.stepContentDraft.exitPromptVisible.get()) { return; }
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    step_chord_editor_workflow::toggleSourceSelector(sequencer_, step,
                                                     effectiveScaleSettings(sequencer_, tracks_));
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::openPitchContextSettings() {
    if (!chordEditorActive() || pitch_context_settings_open_) return;

    // Pitch Context is Pattern-global, so it remains a separate Undo record
    // even when reached from a Chord draft. The projection service also
    // adapts the current draft formula without publishing it.
    if (!sequencer_.stepContentDraft.active.get()) {
        if (!commitStepEditHistory()) return;
    }
    sequencer_.stepPropertyInlineSelector.reset();
    pattern_pitch_settings_.openOverlay();
    pattern_pitch_settings_.focusedRow.set(3U);
    overlays_.show(core::ui::OverlayType::PATTERN_PITCH_SETTINGS, true);
    pitch_context_settings_open_ = true;
}

FLASHMEM bool SequencerStepEditHandler::chordEditorActive() const {
    return step_chord_editor_workflow::active(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::editedStepInRange(uint8_t& step) const {
    return step_edit_session_workflow::editedStepInRange(sequencer_, step);
}

FLASHMEM void SequencerStepEditHandler::maybeCloseFromMacro(uint8_t indexInPage) {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (step_edit_session_workflow::shouldCloseFromMacro(sequencer_, indexInPage)) {
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

    return step_context_row_workflow::canPasteFocusedContextChild(sequencer_, step,
                                                                  structure_clipboard_);
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

    auto descriptor = stepEditDescriptor(step);
    if (step_edit_rows::isProperty(sequencer_.stepEdit.focusedRow.get())) {
        descriptor.property = step_edit_rows::propertyForRow(sequencer_.stepEdit.focusedRow.get());
    }
    constexpr auto owner =
        core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
    if (!beginPreparedPatternMutation(owner, step, stepResetPayloadPlan(sequencer_, step),
                                      descriptor)) {
        return;
    }
    const bool changed = step_value_row_workflow::resetFocusedRowToDefault(sequencer_, step);
    if (!sealPreparedPatternMutation(owner, step, changed, descriptor)) return;
    if (!changed) return;
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::clearFocusedContextChild() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedContextHasChild()) return;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    if (!commitStepEditHistory()) return;

    constexpr auto owner = core::state::sequencer::SequencerPreparedPatternEditOwner::StepContent;
    const auto descriptor = stepEditDescriptor(step);
    if (!beginPreparedPatternMutation(
            owner, step,
            core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload,
            descriptor, true)) {
        return;
    }
    const bool changed = step_context_row_workflow::clearFocusedContextChild(sequencer_, step);
    if (!sealPreparedPatternMutation(owner, step, changed, descriptor)) return;
    if (!commitPreparedPatternMutation(owner)) return;
}

FLASHMEM void SequencerStepEditHandler::copyFocusedStepContent() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedContextHasChild()) return;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_context_row_workflow::copyFocusedContextChildToClipboard(sequencer_, step,
                                                                  structure_clipboard_);
}

FLASHMEM void SequencerStepEditHandler::pasteFocusedStepContent() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!canPasteFocusedStepContent()) return;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    if (!commitStepEditHistory()) return;

    constexpr auto owner = core::state::sequencer::SequencerPreparedPatternEditOwner::StepContent;
    const auto descriptor = stepEditDescriptor(step);
    const auto payloadPlan =
        core::state::sequencer::graphView(sequencer_.pattern) == nullptr
            ? core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph
            : core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload;
    if (!beginPreparedPatternMutation(owner, step, payloadPlan, descriptor, true)) { return; }
    const bool changed = step_context_row_workflow::pasteFocusedContextChildFromClipboard(
        sequencer_, step, structure_clipboard_);
    if (!sealPreparedPatternMutation(owner, step, changed, descriptor)) return;
    if (!commitPreparedPatternMutation(owner)) return;
}

}  // namespace core::handler

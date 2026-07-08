#include "SequencerStepEditHandler.hpp"

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include <utility>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "SequencerStepChordEditorWorkflow.hpp"
#include "SequencerStepContextRowWorkflow.hpp"
#include "SequencerStepEditSessionWorkflow.hpp"
#include "SequencerStepValueRowWorkflow.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler {
namespace interaction_policy = core::handler::sequencer::interaction_policy;
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
        sequencer.pattern.scalePolicy,
        sequencer.pattern.scaleOverride
    );
}

inline oc::type::IsActiveFn canOpenStepEdit(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus]() {
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return interaction_policy::canOpenStepEditor(policy);
    };
}

}  // namespace

FLASHMEM SequencerStepEditHandler::SequencerStepEditHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID overlayScope,
    oc::type::ScopeID stepPresetOverlayScope
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
{
    setupBindings();
}

FLASHMEM void SequencerStepEditHandler::setupBindings() {
    // ===== SEQUENCER VIEW SCOPE =====
    // MACRO_i long press: open STEP EDIT for step i in the current page.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(sequencer_view_scope_)
            .when(canOpenStepEdit(overlay_state_, sequencer_, track_ui_, navigation_focus_))
            .then([this, i]() { openForMacroInPage(i); });
    }

    // ===== OVERLAY SCOPE =====
    // NAV encoder: focus row
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    // OPT encoder: edit focused value
    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .then([this](float value) { setFocusedValue(value); });

    // Pressing the currently edited step closes; value edits are already live.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .release()
            .scope(overlay_scope_)
            .then([this, i]() { maybeCloseFromMacro(i); });
    }

    // Close. Property edits are applied immediately while turning OPT.
    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(overlay_scope_)
        .then([this]() { activateFocusedRowOrClose(); });

    // Close without reverting live edits.
    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .then([this]() { backFromStepEdit(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(overlay_scope_)
        .then([this]() { backFromStepEdit(); });

    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .release()
        .scope(overlay_scope_)
        .then([this]() { openStepPresetPicker(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .scope(overlay_scope_)
        .when([this]() { return focusedRowSupportsLocalVariation(); })
        .then([this]() {
            sequencer_.stepEdit.localVariationEditActive.set(true);
            configureOptForFocusedRow();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return sequencer_.stepEdit.localVariationEditActive.get(); })
        .then([this]() {
            sequencer_.stepEdit.localVariationEditActive.set(false);
            configureOptForFocusedRow();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsValueRow(); })
        .then([this]() { resetFocusedValueRowToDefault(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsContextRow(); })
        .then([this]() {
            if (focusedContextHasChild()) {
                sequencer_.stepEdit.contextHold.begin(
                    core::state::StructureHoldAction::REMOVE,
                    oc::time::millis()
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsContextRow(); })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            if (context_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                return;
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsContextRow() && focusedContextHasChild(); })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            context_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
            clearFocusedContextChild();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsContextRow(); })
        .then([this]() {
            if (canPasteFocusedStepContent()) {
                sequencer_.stepEdit.contextHold.begin(
                    core::state::StructureHoldAction::PASTE,
                    oc::time::millis()
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsContextRow(); })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            if (context_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
                return;
            }
            copyFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() { return focusedRowIsContextRow() && canPasteFocusedStepContent(); })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            context_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
            pasteFocusedStepContent();
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(step_preset_overlay_scope_)
        .then([this](float delta) { moveStepPresetItem(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { executeStepPresetAction(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { closeStepPresetPicker(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { closeStepPresetPicker(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { closeStepPresetPicker(); });

    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { toggleStepPresetMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { executeStepPresetAction(); });
}

FLASHMEM void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    if (!step_edit_session_workflow::openForMacroInPage(
            sequencer_,
            history_,
            open_release_latch_,
            overlays_,
            history_snapshot_,
            history_snapshot_valid_,
            indexInPage
        )) {
        return;
    }
    configureOptForFocusedRow();
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
    if (chordEditorActive()) {
        closeChordEditor();
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
    step_edit_session_workflow::close(
        sequencer_,
        history_,
        open_release_latch_,
        context_release_latch_,
        overlays_,
        history_snapshot_,
        history_snapshot_valid_
    );
}

FLASHMEM void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

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

FLASHMEM void SequencerStepEditHandler::activateFocusedRowOrClose() {
    auto& edit = sequencer_.stepEdit;
    const uint8_t focusedRow = edit.focusedRow.get();

    if (chordEditorActive()) {
        closeChordEditor();
        return;
    }

    if (step_edit_rows::isProperty(focusedRow)) {
        closeStepEdit();
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
        activateFocusedContextRow();
        return;
    }
}

FLASHMEM void SequencerStepEditHandler::activateFocusedContextRow() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    const auto result = step_context_row_workflow::openOrCreateFocusedContextChild(
        sequencer_,
        step
    );
    if (!result.opened) return;

    if (history_snapshot_valid_) {
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

    overlays_.hide();
    sequencer_.stepEdit.reset();
}

FLASHMEM void SequencerStepEditHandler::setFocusedValue(float normalized) {
    if (chordEditorActive()) {
        setFocusedChordFieldValue(normalized);
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
    step_chord_editor_workflow::open(sequencer_);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::closeChordEditor() {
    step_chord_editor_workflow::close(sequencer_);
    configureOptForFocusedRow();
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

    if (!step_chord_editor_workflow::resetFocusedFieldToDefault(sequencer_, step)) return;
    configureOptForFocusedRow();
}

FLASHMEM bool SequencerStepEditHandler::chordEditorActive() const {
    return step_chord_editor_workflow::active(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::editedStepInRange(uint8_t& step) const {
    return step_edit_session_workflow::editedStepInRange(sequencer_, step);
}

FLASHMEM void SequencerStepEditHandler::maybeCloseFromMacro(uint8_t indexInPage) {
    if (step_edit_session_workflow::shouldCloseFromMacro(
            open_release_latch_,
            sequencer_,
            indexInPage
        )) {
        closeStepEdit();
    }
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsValueRow() const {
    if (chordEditorActive()) return true;
    return step_value_row_workflow::focusedRowIsValue(sequencer_);
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

FLASHMEM void SequencerStepEditHandler::openStepPresetPicker() {
    step_preset_picker_.open();
}

FLASHMEM void SequencerStepEditHandler::closeStepPresetPicker() {
    step_preset_picker_.close();
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::moveStepPresetItem(float delta) {
    step_preset_picker_.move(delta);
}

FLASHMEM void SequencerStepEditHandler::toggleStepPresetMode() {
    step_preset_picker_.toggleMode();
}

FLASHMEM void SequencerStepEditHandler::executeStepPresetAction() {
    if (step_preset_picker_.shouldCommitBeforeLoad()) {
        commitStepEditHistory();
        history_.commitCoalescedPatternEdit();
    }

    const auto outcome = step_preset_picker_.execute();
    if (outcome != SequencerStepPresetPickerOutcome::LOADED) return;

    sequencer_.invalidateVariationTelemetry();
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    closeStepPresetPicker();
}

}  // namespace core::handler

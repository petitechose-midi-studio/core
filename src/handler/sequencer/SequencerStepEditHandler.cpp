#include "SequencerStepEditHandler.hpp"

#include <algorithm>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include <utility>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerChordEditOps.hpp"
#include "SequencerInputUtils.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "SequencerStepChordEditorWorkflow.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler {
namespace chord_edit_ops = core::handler::sequencer::chord_edit_ops;
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;
namespace step_chord_editor_workflow =
    core::handler::sequencer::step_chord_editor_workflow;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

namespace {

FLASHMEM bool isActivatedRow(uint8_t row) {
    return step_edit_rows::isActivated(row);
}

FLASHMEM bool isPropertyRow(uint8_t row) {
    return step_edit_rows::isProperty(row);
}

FLASHMEM bool isChordRow(uint8_t row) {
    return step_edit_rows::isChord(row);
}

FLASHMEM core::state::sequencer::StepProperty propertyForRow(uint8_t row) {
    return step_edit_rows::propertyForRow(row);
}

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

template <typename EncoderIdT>
FLASHMEM void configureStepEditEncoder(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    core::state::sequencer::StepProperty property,
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    const auto config = input_utils::encoderConfigForProperty(
        property,
        sequencer.pattern.pitchEditMode,
        scaleSettings
    );
    encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
    encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
    encoders.setDiscreteSteps(encoderId, config.discreteSteps);
    encoders.setPosition(
        encoderId,
        core::state::sequencer::activeContentStepPropertyToNormalized(
            sequencer,
            step,
            property,
            sequencer.pattern.pitchEditMode,
            scaleSettings
        )
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
    history_.commitCoalescedPatternEdit();

    uint8_t abs = 0;
    if (!core::state::sequencer::resolveActiveContentStepInPage(
            sequencer_,
            sequencer_.page.get(),
            indexInPage,
            abs
        )) {
        return;
    }

    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);

    sequencer_.focusedStep.set(abs);

    auto& o = sequencer_.stepEdit;
    o.reset();
    o.focusedRow.set(step_edit_rows::rowForNavigationIndex(0));
    o.stepIndex.set(abs);

    // longPress() fires while button is still pressed; don't immediately close on release.
    open_release_latch_.arm(Config::MACRO_BUTTONS[indexInPage]);

    overlays_.show(core::ui::OverlayType::SEQ_STEP_EDIT);

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::commitStepEditHistory() {
    if (history_snapshot_valid_) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        if (core::state::sequencer::captureHistorySnapshot(sequencer_, after) &&
            !core::state::sequencer::sameMusicalHistorySnapshot(history_snapshot_, after)) {
            history_.recordPattern(
                std::move(history_snapshot_),
                std::move(after),
                core::state::sequencer::SequencerHistoryDescriptor{
                    .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                    .stepIndex = sequencer_.stepEdit.stepIndex.get(),
                }
            );
        }
    }

    history_snapshot_valid_ = false;
}

FLASHMEM void SequencerStepEditHandler::backFromStepEdit() {
    if (chordEditorActive()) {
        closeChordEditor();
        return;
    }

    if (core::state::sequencer::isChildContentView(sequencer_)) {
        uint8_t parentContextRow = sequencer_.stepEdit.focusedRow.get();
        if (const auto* frame = sequencer_.contentView.currentFrame()) {
            parentContextRow =
                frame->kind == core::state::sequencer::SequencerContentViewKind::MICRO_SEQUENCE
                    ? step_edit_rows::MICRO_SEQUENCE
                    : step_edit_rows::CYCLE_STATES;
        }
        commitStepEditHistory();
        history_.commitCoalescedPatternEdit();
        if (core::state::sequencer::leaveContentView(sequencer_)) {
            sequencer_.stepEdit.contextHold.clear();
            sequencer_.stepEdit.localVariationEditActive.set(false);
            sequencer_.stepEdit.stepIndex.set(sequencer_.focusedStep.get());
            sequencer_.stepEdit.focusedRow.set(parentContextRow);
            history_snapshot_valid_ =
                core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
            configureOptForFocusedRow();
            return;
        }
    }

    closeStepEdit();
}

FLASHMEM void SequencerStepEditHandler::closeStepEdit() {
    commitStepEditHistory();
    open_release_latch_.clear();
    context_release_latch_.clear();
    overlays_.hide();
    sequencer_.stepEdit.reset();
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

    if (isPropertyRow(focusedRow)) {
        closeStepEdit();
        return;
    }

    if (isChordRow(focusedRow)) {
        openChordEditor();
        return;
    }

    if (isActivatedRow(focusedRow)) {
        uint8_t abs = 0;
        if (!editedStepInRange(abs)) return;
        if (core::state::sequencer::toggleActiveContentStep(sequencer_, abs)) {
            configureOptForFocusedRow();
        }
        return;
    }

    if (focusedRow == step_edit_rows::MICRO_SEQUENCE) {
        activateFocusedContextRow(
            core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
            core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
        );
        return;
    }

    if (focusedRow == step_edit_rows::CYCLE_STATES) {
        activateFocusedContextRow(
            core::state::sequencer::StepContentChildKind::CYCLE_STATES,
            core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
        );
        return;
    }
}

FLASHMEM void SequencerStepEditHandler::activateFocusedContextRow(
    core::state::sequencer::StepContentChildKind childKind,
    uint8_t defaultLength
) {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    const auto result = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer_,
        step,
        childKind,
        defaultLength
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
    auto& edit = sequencer_.stepEdit;

    if (chordEditorActive()) {
        setFocusedChordFieldValue(normalized);
        return;
    }

    const uint8_t focusedRow = edit.focusedRow.get();

    uint8_t abs = 0;
    if (isActivatedRow(focusedRow)) {
        if (!editedStepInRange(abs)) return;

        core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            abs,
            normalized >= 0.5f
        );
        return;
    }

    if (!editedStepInRange(abs)) return;

    if (isChordRow(focusedRow)) {
        const auto chord = core::state::sequencer::resolveStepChordUiState(sequencer_, abs);
        const int choice = input_utils::normalizedToIndex(
            normalized,
            chord_edit_ops::quickChoiceCount(chord.rootContext)
        );
        chord_edit_ops::applyQuickChoice(sequencer_, abs, choice);
        return;
    }

    if (!isPropertyRow(focusedRow)) return;

    const auto property = propertyForRow(focusedRow);

    if (edit.localVariationEditActive.get() &&
        core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer_, abs);
        const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
        if (core::state::sequencer::setNodeLocalVariationRange(
                sequencer_.pattern,
                nodeId,
                property,
                range
            )) {
            sequencer_.invalidateVariationTelemetry();
        }
        return;
    }

    core::state::sequencer::setActiveContentStepFromNormalized(
        sequencer_,
        abs,
        property,
        normalized,
        sequencer_.pattern.pitchEditMode,
        effectiveScaleSettings(sequencer_, tracks_)
    );
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedRow() {
    auto& edit = sequencer_.stepEdit;

    if (chordEditorActive()) {
        configureOptForFocusedChordField();
        return;
    }

    const uint8_t focusedRow = edit.focusedRow.get();

    uint8_t abs = 0;
    if (isActivatedRow(focusedRow)) {
        if (!editedStepInRange(abs)) return;

        const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
            sequencer_,
            abs,
            effectiveScaleSettings(sequencer_, tracks_)
        );
        if (!projection.valid) return;
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, 8);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, 0.25f);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 2);
        encoders_.setPosition(Config::EncoderID::OPT, projection.enabled ? 1.0f : 0.0f);
        return;
    }

    if (isChordRow(focusedRow)) {
        if (!editedStepInRange(abs)) return;

        const auto chord = core::state::sequencer::resolveStepChordUiState(sequencer_, abs);
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, 8);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, 0.25f);
        encoders_.setDiscreteSteps(
            Config::EncoderID::OPT,
            static_cast<uint8_t>(chord_edit_ops::quickChoiceCount(chord.rootContext))
        );
        encoders_.setPosition(
            Config::EncoderID::OPT,
            input_utils::indexToNormalized(
                chord_edit_ops::quickChoiceIndex(chord),
                chord_edit_ops::quickChoiceCount(chord.rootContext)
            )
        );
        return;
    }

    if (!isPropertyRow(focusedRow)) return;

    const auto property = propertyForRow(focusedRow);

    if (!editedStepInRange(abs)) return;

    if (edit.localVariationEditActive.get() &&
        core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        const auto config = input_utils::encoderConfigForVariationRange(property);
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);

        uint8_t range = 0;
        const auto* graph = core::state::sequencer::graphView(sequencer_.pattern);
        const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer_, abs);
        if (graph != nullptr) {
            const auto* node = graph->stepNode(nodeId);
            if (node != nullptr) {
                range = core::state::sequencer::nodeLocalVariationRange(*node, property);
            }
        }
        encoders_.setPosition(
            Config::EncoderID::OPT,
            input_utils::variationRangeToNormalized(property, range)
        );
        return;
    }

    configureStepEditEncoder(
        encoders_,
        Config::EncoderID::OPT,
        property,
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
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    if (len == 0) return false;

    step = sequencer_.stepEdit.stepIndex.get();
    return step < len;
}

FLASHMEM void SequencerStepEditHandler::maybeCloseFromMacro(uint8_t indexInPage) {
    if (open_release_latch_.consume(Config::MACRO_BUTTONS[indexInPage])) {
        return;
    }

    auto& edit = sequencer_.stepEdit;
    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t abs = edit.stepIndex.get();
    const uint8_t currentIndexInPage = static_cast<uint8_t>(abs % stepsPerPage);

    if (indexInPage != currentIndexInPage) return;
    closeStepEdit();
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsValueRow() const {
    if (chordEditorActive()) return true;
    const uint8_t focusedRow = sequencer_.stepEdit.focusedRow.get();
    return isActivatedRow(focusedRow) || isPropertyRow(focusedRow) || isChordRow(focusedRow);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsContextRow() const {
    if (chordEditorActive()) return false;
    const uint8_t focusedRow = sequencer_.stepEdit.focusedRow.get();
    return step_edit_rows::isContext(focusedRow);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowSupportsLocalVariation() const {
    if (chordEditorActive()) return false;
    const uint8_t focusedRow = sequencer_.stepEdit.focusedRow.get();
    if (!isPropertyRow(focusedRow)) return false;
    return core::state::sequencer::stepPropertySupportsLocalVariation(
        propertyForRow(focusedRow)
    );
}

FLASHMEM bool SequencerStepEditHandler::focusedContextHasChild() const {
    if (!focusedRowIsContextRow()) return false;

    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    const auto childKind = step_edit_rows::childKindForContextRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    return core::state::sequencer::activeContentStepHasChildContent(
        sequencer_,
        step,
        childKind
    );
}

FLASHMEM bool SequencerStepEditHandler::canPasteFocusedStepContent() const {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    const auto childKind = step_edit_rows::childKindForContextRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    return focusedRowIsContextRow() &&
           core::state::sequencer::clipboardCanPasteActiveContentChild(
               structure_clipboard_,
               childKind
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               sequencer_,
               step
           );
}

FLASHMEM void SequencerStepEditHandler::resetFocusedValueRowToDefault() {
    if (!focusedRowIsValueRow()) return;

    if (chordEditorActive()) {
        resetFocusedChordFieldToDefault();
        return;
    }

    auto& edit = sequencer_.stepEdit;
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    bool changed = false;
    const uint8_t row = edit.focusedRow.get();
    if (isActivatedRow(row)) {
        changed = core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            step,
            false
        );
    } else if (isChordRow(row)) {
        changed = core::state::sequencer::clearNodeChordState(
            sequencer_.pattern,
            core::state::sequencer::activeContentStepNodeId(sequencer_, step)
        );
    } else if (isPropertyRow(row)) {
        const auto property = propertyForRow(row);
        changed = core::state::sequencer::resetActiveContentStepPropertyToDefault(
            sequencer_,
            step,
            property
        );
    }

    if (!changed) return;
    sequencer_.invalidateVariationTelemetry();
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

    const auto childKind = step_edit_rows::childKindForContextRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    const bool changed = core::state::sequencer::clearActiveContentChild(
        sequencer_,
        step,
        childKind
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

    const auto childKind = step_edit_rows::childKindForContextRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    core::state::sequencer::copyActiveContentChildToClipboard(
        sequencer_,
        step,
        childKind,
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

    const auto childKind = step_edit_rows::childKindForContextRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    const bool changed = core::state::sequencer::pasteActiveContentChildFromClipboard(
        sequencer_,
        step,
        childKind,
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

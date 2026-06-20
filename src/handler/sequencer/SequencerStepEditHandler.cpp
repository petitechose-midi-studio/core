#include "SequencerStepEditHandler.hpp"

#include <algorithm>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include <utility>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler {
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

namespace {

FLASHMEM bool isActivatedRow(uint8_t row) {
    return step_edit_rows::isActivated(row);
}

FLASHMEM bool isPropertyRow(uint8_t row) {
    return step_edit_rows::isProperty(row);
}

FLASHMEM bool propertySupportsLocalVariation(core::state::sequencer::StepProperty property) {
    return property != core::state::sequencer::StepProperty::PROBABILITY;
}

FLASHMEM core::state::SequencerStepContentClipboardKind clipboardKindForContextRow(uint8_t row) {
    if (row == step_edit_rows::MICRO_SEQUENCE) {
        return core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE;
    }
    if (row == step_edit_rows::CYCLE_STATES) {
        return core::state::SequencerStepContentClipboardKind::CYCLE_STATES;
    }
    return core::state::SequencerStepContentClipboardKind::NONE;
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
    oc::type::ScopeID overlayScope
)
    : overlay_state_(state.overlays)
    , sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , structure_clipboard_(state.structureClipboard)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , history_(state.history)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , overlay_scope_(overlayScope)
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
        .then([this]() { closeStepEdit(); });

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

FLASHMEM void SequencerStepEditHandler::closeStepEdit() {
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
    open_release_latch_.clear();
    context_release_latch_.clear();
    overlays_.hide();
    sequencer_.stepEdit.reset();
}

FLASHMEM void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

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

    if (isPropertyRow(focusedRow)) {
        closeStepEdit();
        return;
    }

    if (isActivatedRow(focusedRow)) {
        const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
        const uint8_t abs = edit.stepIndex.get();
        if (len == 0 || abs >= len) return;
        if (core::state::sequencer::toggleActiveContentStep(sequencer_, abs)) {
            configureOptForFocusedRow();
        }
        return;
    }

    if (focusedRow == step_edit_rows::MICRO_SEQUENCE) {
        const auto availability = core::state::sequencer::activeContentChildCreationAvailability(
            sequencer_,
            edit.stepIndex.get(),
            core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
            core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
        );
        if (!availability.canCreateOrOpen) return;
        const auto ownerNodeId =
            core::state::sequencer::activeContentStepNodeId(sequencer_, edit.stepIndex.get());
        const auto result = core::state::sequencer::createMicroSequence(
            sequencer_.pattern,
            ownerNodeId,
            core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
        );
        if (result.ok) {
            if (history_snapshot_valid_) {
                core::state::sequencer::SequencerHistoryPatternSnapshot after;
                if (core::state::sequencer::captureHistorySnapshot(sequencer_, after)) {
                    history_.recordPattern(
                        std::move(history_snapshot_),
                        std::move(after),
                        core::state::sequencer::SequencerHistoryDescriptor{
                            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                            .stepIndex = edit.stepIndex.get(),
                            .property = core::state::sequencer::StepProperty::NOTE,
                        }
                    );
                }
                history_snapshot_valid_ = false;
            }
            core::state::sequencer::enterMicroSequenceContentView(
                sequencer_,
                ownerNodeId,
                result.id
            );
            overlays_.hide();
            edit.reset();
        }
        return;
    }

    if (focusedRow == step_edit_rows::CYCLE_STATES) {
        const auto availability = core::state::sequencer::activeContentChildCreationAvailability(
            sequencer_,
            edit.stepIndex.get(),
            core::state::sequencer::StepContentChildKind::CYCLE_STATES,
            core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
        );
        if (!availability.canCreateOrOpen) return;
        const auto ownerNodeId =
            core::state::sequencer::activeContentStepNodeId(sequencer_, edit.stepIndex.get());
        const auto result = core::state::sequencer::createCycleStateSet(
            sequencer_.pattern,
            ownerNodeId,
            core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
        );
        if (result.ok) {
            if (history_snapshot_valid_) {
                core::state::sequencer::SequencerHistoryPatternSnapshot after;
                if (core::state::sequencer::captureHistorySnapshot(sequencer_, after)) {
                    history_.recordPattern(
                        std::move(history_snapshot_),
                        std::move(after),
                        core::state::sequencer::SequencerHistoryDescriptor{
                            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                            .stepIndex = edit.stepIndex.get(),
                            .property = core::state::sequencer::StepProperty::NOTE,
                        }
                    );
                }
                history_snapshot_valid_ = false;
            }
            core::state::sequencer::enterCycleStatesContentView(
                sequencer_,
                ownerNodeId,
                result.id
            );
            overlays_.hide();
            edit.reset();
        }
        return;
    }
}

FLASHMEM void SequencerStepEditHandler::setFocusedValue(float normalized) {
    auto& edit = sequencer_.stepEdit;
    const uint8_t focusedRow = edit.focusedRow.get();

    if (isActivatedRow(focusedRow)) {
        const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
        if (len == 0) return;

        const uint8_t abs = edit.stepIndex.get();
        if (abs >= len) return;

        core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            abs,
            normalized >= 0.5f
        );
        return;
    }

    if (!isPropertyRow(focusedRow)) return;

    const auto property = propertyForRow(focusedRow);

    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    if (len == 0) return;

    const uint8_t abs = edit.stepIndex.get();
    if (abs >= len) return;

    if (edit.localVariationEditActive.get() && propertySupportsLocalVariation(property)) {
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
    const uint8_t focusedRow = edit.focusedRow.get();

    if (isActivatedRow(focusedRow)) {
        const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
        if (len == 0) return;

        const uint8_t abs = edit.stepIndex.get();
        if (abs >= len) return;

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

    if (!isPropertyRow(focusedRow)) return;

    const auto property = propertyForRow(focusedRow);

    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    if (len == 0) return;

    const uint8_t abs = edit.stepIndex.get();
    if (abs >= len) return;

    if (edit.localVariationEditActive.get() && propertySupportsLocalVariation(property)) {
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
    const uint8_t focusedRow = sequencer_.stepEdit.focusedRow.get();
    return isActivatedRow(focusedRow) || isPropertyRow(focusedRow);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsContextRow() const {
    const uint8_t focusedRow = sequencer_.stepEdit.focusedRow.get();
    return step_edit_rows::isContext(focusedRow);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowSupportsLocalVariation() const {
    const uint8_t focusedRow = sequencer_.stepEdit.focusedRow.get();
    if (!isPropertyRow(focusedRow)) return false;
    return propertySupportsLocalVariation(propertyForRow(focusedRow));
}

FLASHMEM bool SequencerStepEditHandler::focusedContextHasChild() const {
    if (!focusedRowIsContextRow()) return false;

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer_,
        sequencer_.stepEdit.stepIndex.get(),
        effectiveScaleSettings(sequencer_, tracks_)
    );
    if (!projection.valid) return false;

    return core::state::sequencer::stepContentProjectionHasChild(
        projection,
        step_edit_rows::childKindForContextRow(sequencer_.stepEdit.focusedRow.get())
    );
}

FLASHMEM bool SequencerStepEditHandler::canPasteFocusedStepContent() const {
    const auto clipboardKind = clipboardKindForContextRow(sequencer_.stepEdit.focusedRow.get());
    return focusedRowIsContextRow() &&
           structure_clipboard_.hasSequencerStepContent(clipboardKind) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               sequencer_,
               sequencer_.stepEdit.stepIndex.get()
           );
}

FLASHMEM void SequencerStepEditHandler::resetFocusedValueRowToDefault() {
    if (!focusedRowIsValueRow()) return;

    auto& edit = sequencer_.stepEdit;
    const uint8_t step = edit.stepIndex.get();
    if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

    bool changed = false;
    const uint8_t row = edit.focusedRow.get();
    if (isActivatedRow(row)) {
        changed = core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            step,
            false
        );
    } else if (isPropertyRow(row)) {
        const auto property = propertyForRow(row);
        if (core::state::sequencer::isRootContentView(sequencer_)) {
            switch (property) {
                case core::state::sequencer::StepProperty::NOTE:
                    changed = sequencer_.setStepNoteAt(
                        step,
                        core::state::sequencer::SequencerState::DEFAULT_NOTE
                    );
                    break;
                case core::state::sequencer::StepProperty::VELOCITY:
                    changed = sequencer_.setStepVelocityAt(
                        step,
                        core::state::sequencer::SequencerState::DEFAULT_VELOCITY
                    );
                    break;
                case core::state::sequencer::StepProperty::GATE:
                    changed = sequencer_.setStepGateAt(
                        step,
                        core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT
                    );
                    break;
                case core::state::sequencer::StepProperty::NUDGE:
                    changed = sequencer_.setStepNudgeAt(step, 0);
                    break;
                case core::state::sequencer::StepProperty::PROBABILITY:
                    changed = sequencer_.setStepProbabilityAt(
                        step,
                        core::state::sequencer::SequencerState::DEFAULT_PROBABILITY
                    );
                    break;
            }
        } else {
            const auto nodeId = core::state::sequencer::activeContentStepNodeId(
                sequencer_,
                step
            );
            switch (property) {
                case core::state::sequencer::StepProperty::NOTE:
                    changed = core::state::sequencer::setNodeNoteOffset(
                        sequencer_.pattern,
                        nodeId,
                        0
                    );
                    break;
                case core::state::sequencer::StepProperty::VELOCITY:
                    changed = core::state::sequencer::setNodeVelocityOffset(
                        sequencer_.pattern,
                        nodeId,
                        0
                    );
                    break;
                case core::state::sequencer::StepProperty::GATE:
                    changed = core::state::sequencer::setNodeGateOffset(
                        sequencer_.pattern,
                        nodeId,
                        0
                    );
                    break;
                case core::state::sequencer::StepProperty::NUDGE:
                    changed = core::state::sequencer::setNodeNudgeOffset(
                        sequencer_.pattern,
                        nodeId,
                        0
                    );
                    break;
                case core::state::sequencer::StepProperty::PROBABILITY:
                    changed = core::state::sequencer::setNodeProbabilityOffset(
                        sequencer_.pattern,
                        nodeId,
                        0
                    );
                    break;
            }
            if (changed) sequencer_.contentView.bump();
        }

        changed = core::state::sequencer::setNodeLocalVariationRange(
            sequencer_.pattern,
            core::state::sequencer::activeContentStepNodeId(sequencer_, step),
            property,
            0
        ) || changed;
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

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer_,
        sequencer_.stepEdit.stepIndex.get()
    );
    const bool changed = sequencer_.stepEdit.focusedRow.get() == step_edit_rows::MICRO_SEQUENCE
        ? core::state::sequencer::clearNodeChildSequence(sequencer_.pattern, nodeId)
        : core::state::sequencer::clearNodeCycleStateSet(sequencer_.pattern, nodeId);
    if (!changed) {
        if (!history_snapshot_valid_ && beforeCaptured) {
            history_snapshot_ = std::move(before);
            history_snapshot_valid_ = true;
        }
        return;
    }

    core::state::sequencer::compactSequencerGraph(sequencer_);
    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.contentView.bump();
    recordContextMutation(std::move(before), beforeCaptured);
}

FLASHMEM void SequencerStepEditHandler::copyFocusedStepContent() {
    if (!focusedContextHasChild()) return;
    const auto* graph = core::state::sequencer::graphView(sequencer_.pattern);
    if (graph == nullptr) return;

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer_,
        sequencer_.stepEdit.stepIndex.get()
    );
    if (!structure_clipboard_.storeSequencerStepContent(
        *graph,
        nodeId,
        clipboardKindForContextRow(sequencer_.stepEdit.focusedRow.get())
    )) {
        return;
    }
}

FLASHMEM void SequencerStepEditHandler::pasteFocusedStepContent() {
    if (!canPasteFocusedStepContent()) return;
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

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer_,
        sequencer_.stepEdit.stepIndex.get()
    );
    const auto clipboardKind = clipboardKindForContextRow(sequencer_.stepEdit.focusedRow.get());
    const bool changed = clipboardKind == core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE
        ? core::state::sequencer::copyNodeChildSequenceFromGraph(
              sequencer_.pattern,
              nodeId,
              *structure_clipboard_.sequencerGraph,
              structure_clipboard_.sequencerStepContentNodeId
          )
        : core::state::sequencer::copyNodeCycleStateSetFromGraph(
              sequencer_.pattern,
              nodeId,
              *structure_clipboard_.sequencerGraph,
              structure_clipboard_.sequencerStepContentNodeId
          );
    if (!changed) {
        if (!history_snapshot_valid_ && beforeCaptured) {
            history_snapshot_ = std::move(before);
            history_snapshot_valid_ = true;
        }
        return;
    }

    core::state::sequencer::compactSequencerGraph(sequencer_);
    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.contentView.bump();
    recordContextMutation(std::move(before), beforeCaptured);
}

}  // namespace core::handler

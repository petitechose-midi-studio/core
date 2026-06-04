#include "SequencerStepEditHandler.hpp"

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>

#include <utility>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr uint8_t ROW_COUNT =
    static_cast<uint8_t>(core::state::sequencer::StepProperty::PROBABILITY) + 1;

template <typename EncoderIdT>
void configureStepEditEncoder(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    core::state::sequencer::StepProperty property,
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    const auto config = input_utils::encoderConfigForProperty(property);
    encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
    encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
    encoders.setDiscreteSteps(encoderId, config.discreteSteps);
    encoders.setPosition(encoderId, input_utils::stepPropertyToNormalized(sequencer, step, property));
}

inline oc::type::IsActiveFn canOpenStepEdit(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi
) {
    return [&overlays, &sequencer, &trackUi]() {
        return !overlays.hasVisible() &&
               !sequencer.structureUi.pageSelection.active.get() &&
               !trackUi.selection.active.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !sequencer.stepPropertyInlineSelector.selecting.get();
    };
}

}  // namespace

SequencerStepEditHandler::SequencerStepEditHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID overlayScope
)
    : overlay_state_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
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
            .when(canOpenStepEdit(overlay_state_, sequencer_, track_ui_))
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

    // Pressing the currently edited step closes + applies
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .release()
            .scope(overlay_scope_)
            .then([this, i]() { maybeCloseApplyFromMacro(i); });
    }

    // Apply + close
    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(overlay_scope_)
        .then([this]() { closeApply(); });

    // Cancel + close
    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .then([this]() { closeCancel(); });

}

void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;

    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);

    sequencer_.focusedStep.set(abs);

    auto& o = sequencer_.stepEdit;
    o.reset();
    o.stepIndex.set(abs);

    o.snapshotNote = sequencer_.pattern.note[abs];
    o.snapshotVelocity = sequencer_.pattern.velocity[abs];
    o.snapshotGate = sequencer_.pattern.gate[abs];
    o.snapshotNudge = sequencer_.pattern.nudge[abs];
    o.snapshotProbability = sequencer_.pattern.probability[abs];
    o.snapshotValid = true;

    // longPress() fires while button is still pressed; don't immediately close on release.
    ignore_open_release_ = true;
    ignore_open_macro_index_in_page_ = indexInPage;

    overlays_.show(core::ui::OverlayType::SEQ_STEP_EDIT);

    configureOptForFocusedRow();
}

void SequencerStepEditHandler::closeApply() {
    if (history_snapshot_valid_) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        if (core::state::sequencer::captureHistorySnapshot(sequencer_, after)) {
            history_.recordPattern(std::move(history_snapshot_), std::move(after));
        }
    }

    history_snapshot_valid_ = false;
    ignore_open_release_ = false;
    overlays_.hide();
    sequencer_.stepEdit.reset();
}

void SequencerStepEditHandler::closeCancel() {
    auto& o = sequencer_.stepEdit;

    const uint8_t abs = o.stepIndex.get();
    if (o.snapshotValid && abs < core::state::sequencer::SequencerState::MAX_STEPS) {
        sequencer_.setStepDataAt(
            abs,
            o.snapshotNote,
            o.snapshotVelocity,
            o.snapshotGate,
            o.snapshotNudge,
            o.snapshotProbability
        );
    }

    history_snapshot_valid_ = false;
    ignore_open_release_ = false;
    overlays_.hide();
    o.reset();
}

void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(sequencer_.stepEdit.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    sequencer_.stepEdit.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

void SequencerStepEditHandler::setFocusedValue(float normalized) {
    const uint8_t len = sequencer_.pattern.length.get();
    if (len == 0) return;

    const uint8_t abs = sequencer_.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    input_utils::applyNormalizedToStep(
        sequencer_,
        abs,
        input_utils::stepEditRowToProperty(sequencer_.stepEdit.focusedRow.get()),
        normalized
    );
}

void SequencerStepEditHandler::configureOptForFocusedRow() {
    const uint8_t len = sequencer_.pattern.length.get();
    if (len == 0) return;

    const uint8_t abs = sequencer_.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    configureStepEditEncoder(
        encoders_,
        Config::EncoderID::OPT,
        input_utils::stepEditRowToProperty(sequencer_.stepEdit.focusedRow.get()),
        sequencer_,
        abs
    );
}

void SequencerStepEditHandler::maybeCloseApplyFromMacro(uint8_t indexInPage) {
    if (ignore_open_release_ && indexInPage == ignore_open_macro_index_in_page_) {
        ignore_open_release_ = false;
        return;
    }

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t abs = sequencer_.stepEdit.stepIndex.get();
    const uint8_t currentIndexInPage = static_cast<uint8_t>(abs % stepsPerPage);

    if (indexInPage != currentIndexInPage) return;
    closeApply();
}

}  // namespace core::handler

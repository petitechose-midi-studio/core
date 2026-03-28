#include "SequencerStepEditHandler.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/App.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
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

inline oc::type::IsActiveFn canOpenStepEdit(core::state::CoreState& state) {
    return [&state]() {
        return !state.overlays.hasVisible() &&
               !state.sequencer.patternQuickControls.selecting.get() &&
               !state.sequencer.stepPropertyInlineSelector.selecting.get() &&
               !state.sequencer.rangeSelection.active();
    };
}

}  // namespace

SequencerStepEditHandler::SequencerStepEditHandler(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* sequencerViewScope,
    lv_obj_t* overlayScope
)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , overlay_scope_(overlayScope)
{
    setupBindings();
}

void SequencerStepEditHandler::setupBindings() {
    // ===== SEQUENCER VIEW SCOPE =====
    // MACRO_i long press: open STEP EDIT for step i in the current page.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(scope(sequencer_view_scope_))
            .when(canOpenStepEdit(state_))
            .then([this, i]() { openForMacroInPage(i); });
    }

    // ===== OVERLAY SCOPE =====
    // NAV encoder: focus row
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(overlay_scope_))
        .then([this](float delta) { moveFocus(delta); });

    // OPT encoder: edit focused value
    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope(overlay_scope_))
        .then([this](float value) { setFocusedValue(value); });

    // Pressing the currently edited step closes + applies
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .release()
            .scope(scope(overlay_scope_))
            .then([this, i]() { maybeCloseApplyFromMacro(i); });
    }

    // Apply + close
    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { closeApply(); });

    // Cancel + close
    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { closeCancel(); });

    OC_LOG_DEBUG("[SequencerStepEditHandler] Bindings setup complete");
}

void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!state_.sequencer.resolveStepInPage(state_.sequencer.page.get(), indexInPage, abs)) return;

    state_.sequencer.focusedStep.set(abs);

    auto& o = state_.sequencer.stepEdit;
    o.reset();
    o.stepIndex.set(abs);

    o.snapshotNote = state_.sequencer.note[abs];
    o.snapshotVelocity = state_.sequencer.velocity[abs];
    o.snapshotGate = state_.sequencer.gate[abs];
    o.snapshotNudge = state_.sequencer.nudge[abs];
    o.snapshotProbability = state_.sequencer.probability[abs];
    o.snapshotValid = true;

    // longPress() fires while button is still pressed; don't immediately close on release.
    ignore_open_release_ = true;
    ignore_open_macro_index_in_page_ = indexInPage;

    overlays_.show(core::ui::OverlayType::SEQ_STEP_EDIT);

    configureOptForFocusedRow();
}

void SequencerStepEditHandler::closeApply() {
    ignore_open_release_ = false;
    overlays_.hide();
    state_.sequencer.stepEdit.reset();
}

void SequencerStepEditHandler::closeCancel() {
    auto& o = state_.sequencer.stepEdit;

    const uint8_t abs = o.stepIndex.get();
    if (o.snapshotValid && abs < core::state::sequencer::SequencerState::MAX_STEPS) {
        state_.sequencer.setStepDataAt(
            abs,
            o.snapshotNote,
            o.snapshotVelocity,
            o.snapshotGate,
            o.snapshotNudge,
            o.snapshotProbability
        );
    }

    ignore_open_release_ = false;
    overlays_.hide();
    o.reset();
}

void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(state_.sequencer.stepEdit.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    state_.sequencer.stepEdit.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

void SequencerStepEditHandler::setFocusedValue(float normalized) {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t abs = state_.sequencer.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    input_utils::applyNormalizedToStep(
        state_.sequencer,
        abs,
        input_utils::stepEditRowToProperty(state_.sequencer.stepEdit.focusedRow.get()),
        normalized
    );
}

void SequencerStepEditHandler::configureOptForFocusedRow() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t abs = state_.sequencer.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    configureStepEditEncoder(
        encoders_,
        Config::EncoderID::OPT,
        input_utils::stepEditRowToProperty(state_.sequencer.stepEdit.focusedRow.get()),
        state_.sequencer,
        abs
    );
}

void SequencerStepEditHandler::maybeCloseApplyFromMacro(uint8_t indexInPage) {
    if (ignore_open_release_ && indexInPage == ignore_open_macro_index_in_page_) {
        ignore_open_release_ = false;
        return;
    }

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t abs = state_.sequencer.stepEdit.stepIndex.get();
    const uint8_t currentIndexInPage = static_cast<uint8_t>(abs % stepsPerPage);

    if (indexInPage != currentIndexInPage) return;
    closeApply();
}

}  // namespace core::handler

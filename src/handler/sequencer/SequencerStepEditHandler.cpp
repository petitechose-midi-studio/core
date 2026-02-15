#include "SequencerStepEditHandler.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/util/Index.hpp>

#include <config/InputIDs.hpp>

#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using oc::util::wrapIndex;
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr uint8_t ROW_COUNT = 3;

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
            .longPress()
            .scope(scope(sequencer_view_scope_))
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
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t pageCount = static_cast<uint8_t>((len + stepsPerPage - 1) / stepsPerPage);
    const uint8_t page = static_cast<uint8_t>(state_.sequencer.page.get() % pageCount);

    const uint8_t abs = static_cast<uint8_t>(page * stepsPerPage + indexInPage);
    if (abs >= len) return;

    state_.sequencer.focusedStep.set(abs);

    auto& o = state_.sequencer.stepEdit;
    o.reset();
    o.stepIndex.set(abs);

    o.snapshotNote = state_.sequencer.note[abs];
    o.snapshotVelocity = state_.sequencer.velocity[abs];
    o.snapshotGate = state_.sequencer.gate[abs];
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
        state_.sequencer.note[abs] = o.snapshotNote;
        state_.sequencer.velocity[abs] = o.snapshotVelocity;
        state_.sequencer.gate[abs] = o.snapshotGate;
        bumpStepDataRevision();
    }

    ignore_open_release_ = false;
    overlays_.hide();
    o.reset();
}

void SequencerStepEditHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;
    const int step = (delta > 0.0f) ? 1 : -1;

    const int current = static_cast<int>(state_.sequencer.stepEdit.focusedRow.get());
    const int next = wrapIndex(current + step, ROW_COUNT);
    state_.sequencer.stepEdit.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

void SequencerStepEditHandler::setFocusedValue(float normalized) {
    const float value = input_utils::clampNormalized(normalized);

    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t abs = state_.sequencer.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const uint8_t row = state_.sequencer.stepEdit.focusedRow.get();

    if (row == 0) {
        state_.sequencer.note[abs] = input_utils::normalizedToMidi7(value);
        bumpStepDataRevision();
    } else if (row == 1) {
        state_.sequencer.velocity[abs] = input_utils::normalizedToMidi7(value);
        bumpStepDataRevision();
    } else if (row == 2) {
        state_.sequencer.gate[abs] = input_utils::normalizedToGatePercent(value);
        bumpStepDataRevision();
    }
}

void SequencerStepEditHandler::configureOptForFocusedRow() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t abs = state_.sequencer.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const uint8_t row = state_.sequencer.stepEdit.focusedRow.get();

    if (row == 0) {
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 128);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            input_utils::indexToNormalized(state_.sequencer.note[abs], 128)
        );
    } else if (row == 1) {
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 128);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            input_utils::indexToNormalized(state_.sequencer.velocity[abs], 128)
        );
    } else if (row == 2) {
        encoders_.setDiscreteSteps(
            Config::EncoderID::OPT,
            static_cast<uint8_t>(core::state::sequencer::SequencerState::MAX_GATE_PERCENT + 1)
        );
        encoders_.setPosition(
            Config::EncoderID::OPT,
            input_utils::gatePercentToNormalized(state_.sequencer.gate[abs])
        );
    }
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

void SequencerStepEditHandler::bumpStepDataRevision() {
    state_.sequencer.stepDataRevision.set(state_.sequencer.stepDataRevision.get() + 1);
}

}  // namespace core::handler

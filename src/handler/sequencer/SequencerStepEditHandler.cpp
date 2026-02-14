#include "SequencerStepEditHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;

namespace {

constexpr uint8_t ROW_COUNT = 3;

int wrapIndex(int idx, int count) {
    if (count <= 0) return 0;
    idx %= count;
    if (idx < 0) idx += count;
    return idx;
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
        .then([this](float delta) { adjustValue(delta); });

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

    overlays_.show(core::ui::OverlayType::SEQ_STEP_EDIT);
}

void SequencerStepEditHandler::closeApply() {
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

    overlays_.hide();
    o.reset();
}

void SequencerStepEditHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;
    int step = (delta > 0.0f) ? 1 : -1;

    const int current = static_cast<int>(state_.sequencer.stepEdit.focusedRow.get());
    const int next = wrapIndex(current + step, ROW_COUNT);
    state_.sequencer.stepEdit.focusedRow.set(static_cast<uint8_t>(next));
}

void SequencerStepEditHandler::adjustValue(float delta) {
    if (delta == 0.0f) return;
    int step = (delta > 0.0f) ? 1 : -1;

    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t abs = state_.sequencer.stepEdit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const uint8_t row = state_.sequencer.stepEdit.focusedRow.get();

    if (row == 0) {
        int note = static_cast<int>(state_.sequencer.note[abs]) + step;
        note = std::clamp(note, 0, 127);
        state_.sequencer.note[abs] = static_cast<uint8_t>(note);
        bumpStepDataRevision();
    } else if (row == 1) {
        int vel = static_cast<int>(state_.sequencer.velocity[abs]) + step;
        vel = std::clamp(vel, 0, 127);
        state_.sequencer.velocity[abs] = static_cast<uint8_t>(vel);
        bumpStepDataRevision();
    } else if (row == 2) {
        int gate = static_cast<int>(state_.sequencer.gate[abs]) + step;
        gate = std::clamp(gate, 0, 100);
        state_.sequencer.gate[abs] = static_cast<uint16_t>(gate);
        bumpStepDataRevision();
    }
}

void SequencerStepEditHandler::bumpStepDataRevision() {
    state_.sequencer.stepDataRevision.set(state_.sequencer.stepDataRevision.get() + 1);
}

}  // namespace core::handler

#include "SequencerPatternConfigHandler.hpp"

#include <algorithm>
#include <array>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;

namespace {

constexpr uint8_t ROW_COUNT = 3;
constexpr std::array<uint8_t, 6> STEPS_PER_BEAT_CHOICES = {1, 2, 3, 4, 6, 8};

int wrapIndex(int idx, int count) {
    if (count <= 0) return 0;
    idx %= count;
    if (idx < 0) idx += count;
    return idx;
}

uint8_t nextStepsPerBeat(uint8_t current, int step) {
    int found = -1;
    for (size_t i = 0; i < STEPS_PER_BEAT_CHOICES.size(); ++i) {
        if (STEPS_PER_BEAT_CHOICES[i] == current) {
            found = static_cast<int>(i);
            break;
        }
    }
    if (found < 0) {
        // If state contains an unexpected value, snap to the closest common default.
        found = 3;  // 4 steps/beat = 1/16
    }

    const int next = wrapIndex(found + step, static_cast<int>(STEPS_PER_BEAT_CHOICES.size()));
    return STEPS_PER_BEAT_CHOICES[static_cast<size_t>(next)];
}

}  // namespace

SequencerPatternConfigHandler::SequencerPatternConfigHandler(
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

void SequencerPatternConfigHandler::setupBindings() {
    // ===== SEQUENCER VIEW SCOPE =====
    // Open PATTERN CONFIG
    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope(sequencer_view_scope_))
        .then([this]() { open(); });

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

    OC_LOG_DEBUG("[SequencerPatternConfigHandler] Bindings setup complete");
}

void SequencerPatternConfigHandler::open() {
    auto& o = state_.sequencer.patternConfig;
    o.reset();

    o.snapshotLength = state_.sequencer.length.get();
    o.snapshotStepsPerBeat = state_.sequencer.stepsPerBeat.get();
    o.snapshotMidiChannel = state_.sequencer.midiChannel.get();
    o.snapshotValid = true;

    overlays_.show(core::ui::OverlayType::SEQ_PATTERN_CONFIG);
}

void SequencerPatternConfigHandler::closeApply() {
    overlays_.hide();
    state_.sequencer.patternConfig.reset();
}

void SequencerPatternConfigHandler::closeCancel() {
    auto& o = state_.sequencer.patternConfig;
    if (o.snapshotValid) {
        state_.sequencer.length.set(o.snapshotLength);
        state_.sequencer.stepsPerBeat.set(o.snapshotStepsPerBeat);
        state_.sequencer.midiChannel.set(o.snapshotMidiChannel);
        clampFocusToLength();
    }

    overlays_.hide();
    o.reset();
}

void SequencerPatternConfigHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;
    int step = (delta > 0.0f) ? 1 : -1;

    const int current = static_cast<int>(state_.sequencer.patternConfig.focusedRow.get());
    const int next = wrapIndex(current + step, ROW_COUNT);
    state_.sequencer.patternConfig.focusedRow.set(static_cast<uint8_t>(next));
}

void SequencerPatternConfigHandler::adjustValue(float delta) {
    if (delta == 0.0f) return;
    int step = (delta > 0.0f) ? 1 : -1;

    const uint8_t row = state_.sequencer.patternConfig.focusedRow.get();

    if (row == 0) {
        // LEN: [1..MAX_STEPS]
        int len = static_cast<int>(state_.sequencer.length.get()) + step;
        len = std::clamp(len, 1, static_cast<int>(core::state::sequencer::SequencerState::MAX_STEPS));
        state_.sequencer.length.set(static_cast<uint8_t>(len));
        clampFocusToLength();
    } else if (row == 1) {
        // DIV: discrete steps-per-beat choices
        const uint8_t cur = state_.sequencer.stepsPerBeat.get();
        state_.sequencer.stepsPerBeat.set(nextStepsPerBeat(cur, step));
    } else if (row == 2) {
        // CH: 0..15 (displayed 1..16)
        int ch = static_cast<int>(state_.sequencer.midiChannel.get()) + step;
        ch = std::clamp(ch, 0, 15);
        state_.sequencer.midiChannel.set(static_cast<uint8_t>(ch));
    }
}

void SequencerPatternConfigHandler::clampFocusToLength() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    uint8_t focused = state_.sequencer.focusedStep.get();
    if (focused >= len) {
        focused = static_cast<uint8_t>(len - 1);
        state_.sequencer.focusedStep.set(focused);
    }

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    state_.sequencer.page.set(static_cast<uint8_t>(focused / stepsPerPage));
}

}  // namespace core::handler

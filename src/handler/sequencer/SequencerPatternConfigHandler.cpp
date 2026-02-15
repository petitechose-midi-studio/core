#include "SequencerPatternConfigHandler.hpp"

#include <array>

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
constexpr std::array<uint8_t, 6> STEPS_PER_BEAT_CHOICES = {1, 2, 3, 4, 6, 8};

uint8_t findStepsPerBeatChoiceIndex(uint8_t stepsPerBeat) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(STEPS_PER_BEAT_CHOICES.size()); ++i) {
        if (STEPS_PER_BEAT_CHOICES[i] == stepsPerBeat) return i;
    }
    return 3;  // Default: 4 steps/beat = 1/16
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
        .press()
        .latch()
        .scope(scope(sequencer_view_scope_))
        .then([this]() { open(); });

    // Close + apply on release (latch toggle)
    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { closeApply(); });

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

    configureOptForFocusedRow();
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
    const int step = (delta > 0.0f) ? 1 : -1;

    const int current = static_cast<int>(state_.sequencer.patternConfig.focusedRow.get());
    const int next = wrapIndex(current + step, ROW_COUNT);
    state_.sequencer.patternConfig.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

void SequencerPatternConfigHandler::setFocusedValue(float normalized) {
    const float value = input_utils::clampNormalized(normalized);

    const uint8_t row = state_.sequencer.patternConfig.focusedRow.get();

    if (row == 0) {
        // LEN: [1..MAX_STEPS] (discrete)
        constexpr int steps = core::state::sequencer::SequencerState::MAX_STEPS;
        const int idx = input_utils::normalizedToIndex(value, steps);
        const uint8_t len = static_cast<uint8_t>(idx + 1);
        state_.sequencer.length.set(len);
        clampFocusToLength();
    } else if (row == 1) {
        // DIV: discrete steps-per-beat choices
        constexpr int steps = static_cast<int>(STEPS_PER_BEAT_CHOICES.size());
        const int idx = input_utils::normalizedToIndex(value, steps);
        state_.sequencer.stepsPerBeat.set(STEPS_PER_BEAT_CHOICES[static_cast<size_t>(idx)]);
    } else if (row == 2) {
        // CH: 0..15 (displayed 1..16)
        const int ch = input_utils::normalizedToInclusiveInt(value, 15);
        state_.sequencer.midiChannel.set(static_cast<uint8_t>(ch));
    }
}

void SequencerPatternConfigHandler::configureOptForFocusedRow() {
    const uint8_t row = state_.sequencer.patternConfig.focusedRow.get();
    float pos = 0.0f;
    uint8_t steps = 0;

    if (row == 0) {
        // LEN: [1..MAX_STEPS]
        steps = core::state::sequencer::SequencerState::MAX_STEPS;
        const uint8_t len = state_.sequencer.length.get();
        const uint8_t idx = (len > 0) ? static_cast<uint8_t>(len - 1) : 0;
        pos = input_utils::indexToNormalized(idx, steps);
    } else if (row == 1) {
        // DIV: choices index
        steps = static_cast<uint8_t>(STEPS_PER_BEAT_CHOICES.size());
        const uint8_t cur = state_.sequencer.stepsPerBeat.get();
        const uint8_t idx = findStepsPerBeatChoiceIndex(cur);
        pos = input_utils::indexToNormalized(idx, steps);
    } else if (row == 2) {
        // CH: 0..15
        steps = 16;
        const uint8_t ch = state_.sequencer.midiChannel.get();
        pos = input_utils::indexToNormalized(ch, steps);
    }

    if (steps <= 1) return;
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
    encoders_.setPosition(Config::EncoderID::OPT, pos);
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

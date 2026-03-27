#include "SequencerPatternQuickControlsHandler.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
using Item = core::state::sequencer::PatternQuickControlItem;

namespace {

constexpr int ITEM_COUNT = 3;

inline oc::type::IsActiveFn selectingPredicate(core::state::CoreState& state) {
    return [&state]() { return state.sequencer.patternQuickControls.selecting.get(); };
}

}  // namespace

SequencerPatternQuickControlsHandler::SequencerPatternQuickControlsHandler(
    core::state::CoreState& state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* sequencerViewScope
)
    : state_(state)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope) {
    setupBindings();
}

void SequencerPatternQuickControlsHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope(sequencer_view_scope_))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(scope(sequencer_view_scope_))
        .then([this]() { closeApply(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this](float delta) { navigate(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this]() { closeCancel(); });

    OC_LOG_DEBUG("[SequencerPatternQuickControlsHandler] Bindings setup complete");
}

void SequencerPatternQuickControlsHandler::open() {
    auto& quick = state_.sequencer.patternQuickControls;
    quick.reset();
    quick.selecting.set(true);
    quick.snapshotLength = state_.sequencer.length.get();
    quick.snapshotStepsPerBeat = state_.sequencer.stepsPerBeat.get();
    quick.snapshotMidiChannel = state_.sequencer.midiChannel.get();
    quick.snapshotValid = true;
    configureOptForFocusedItem();
}

void SequencerPatternQuickControlsHandler::closeApply() {
    auto& quick = state_.sequencer.patternQuickControls;
    if (!quick.selecting.get()) return;
    quick.reset();
}

void SequencerPatternQuickControlsHandler::closeCancel() {
    auto& quick = state_.sequencer.patternQuickControls;
    if (!quick.selecting.get()) return;

    if (quick.snapshotValid) {
        state_.sequencer.length.set(quick.snapshotLength);
        state_.sequencer.stepsPerBeat.set(quick.snapshotStepsPerBeat);
        state_.sequencer.midiChannel.set(quick.snapshotMidiChannel);
        clampFocusToLength();
    }

    quick.reset();
}

void SequencerPatternQuickControlsHandler::navigate(float delta) {
    if (!state_.sequencer.patternQuickControls.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(state_.sequencer.patternQuickControls.focusedItem.get());
    const int next = nav::nextWrappedIndex(delta, current, ITEM_COUNT);
    state_.sequencer.patternQuickControls.focusedItem.set(static_cast<Item>(next));
    configureOptForFocusedItem();
}

void SequencerPatternQuickControlsHandler::setFocusedValue(float normalized) {
    auto item = state_.sequencer.patternQuickControls.focusedItem.get();
    input_utils::applyNormalizedToQuickControl(state_.sequencer, item, normalized);
    if (item == Item::LENGTH) {
        clampFocusToLength();
    }
}

void SequencerPatternQuickControlsHandler::configureOptForFocusedItem() {
    const auto item = state_.sequencer.patternQuickControls.focusedItem.get();
    const auto config = input_utils::encoderConfigForQuickControl(item);
    encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
    encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
    encoders_.setPosition(
        Config::EncoderID::OPT,
        input_utils::quickControlToNormalized(state_.sequencer, item)
    );
}

void SequencerPatternQuickControlsHandler::clampFocusToLength() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    uint8_t focused = state_.sequencer.focusedStep.get();
    if (focused >= len) {
        focused = static_cast<uint8_t>(len - 1);
        state_.sequencer.focusedStep.set(focused);
    }

    state_.sequencer.page.set(state_.sequencer.pageForStep(focused));
}

}  // namespace core::handler

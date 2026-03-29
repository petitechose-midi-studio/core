#include "SequencerPatternQuickControlsHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
using Item = core::state::sequencer::PatternQuickControlItem;

namespace {

constexpr int ITEM_COUNT = static_cast<int>(core::state::sequencer::QUICK_CONTROL_VISUAL_ORDER.size());

inline oc::type::IsActiveFn selectingPredicate(core::state::CoreState& state) {
    return [&state]() { return state.sequencer.patternQuickControls.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenQuickControls(core::state::CoreState& state) {
    return [&state]() {
        return !state.overlays.hasVisible() &&
               !state.sequencer.stepPropertyInlineSelector.selecting.get() &&
               !state.sequencer.rangeSelection.active();
    };
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

FLASHMEM void SequencerPatternQuickControlsHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope(sequencer_view_scope_))
        .when(canOpenQuickControls(state_))
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
}

void SequencerPatternQuickControlsHandler::open() {
    auto& quick = state_.sequencer.patternQuickControls;
    quick.reset();
    quick.selecting.set(true);
    core::state::sequencer::captureSnapshot(state_.sequencer, cancel_snapshot_);
    core::state::sequencer::captureSnapshot(state_.sequencer, offset_snapshot_);
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

    core::state::sequencer::applySnapshot(state_.sequencer, cancel_snapshot_);
    clampFocusToLength();

    quick.reset();
}

void SequencerPatternQuickControlsHandler::navigate(float delta) {
    if (!state_.sequencer.patternQuickControls.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = focusedItemOrderIndex();
    const int next = nav::nextWrappedIndex(delta, current, ITEM_COUNT);
    const auto nextItem = core::state::sequencer::quickControlAtOrderIndex(static_cast<size_t>(next));
    setFocusedItemByOrderIndex(next);
    if (nextItem == Item::OFFSET) {
        core::state::sequencer::captureSnapshot(state_.sequencer, offset_snapshot_);
        state_.sequencer.patternQuickControls.offsetSteps.set(0);
    }
    configureOptForFocusedItem();
}

void SequencerPatternQuickControlsHandler::setFocusedValue(float normalized) {
    auto item = state_.sequencer.patternQuickControls.focusedItem.get();
    if (item == Item::OFFSET) {
        const int offsetSteps = normalizedToOffset(normalized);
        if (state_.sequencer.patternQuickControls.offsetSteps.get() == offsetSteps) {
            return;
        }
        state_.sequencer.patternQuickControls.offsetSteps.set(static_cast<int8_t>(offsetSteps));
        applyOffsetFromSnapshot(offsetSteps);
        return;
    }

    input_utils::applyNormalizedToQuickControl(state_.sequencer, item, normalized);
    core::state::sequencer::captureSnapshot(state_.sequencer, offset_snapshot_);
    state_.sequencer.patternQuickControls.offsetSteps.set(0);
    if (item == Item::LENGTH) {
        clampFocusToLength();
    }
}

void SequencerPatternQuickControlsHandler::configureOptForFocusedItem() {
    const auto item = state_.sequencer.patternQuickControls.focusedItem.get();
    if (item == Item::OFFSET) {
        input_utils::StepPropertyEncoderConfig config;
        config.discreteSteps = static_cast<uint8_t>((currentOffsetMax() * 2) + 1);
        config.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
        config.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            offsetToNormalized(state_.sequencer.patternQuickControls.offsetSteps.get())
        );
        return;
    }

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

int SequencerPatternQuickControlsHandler::focusedItemOrderIndex() const {
    const auto focused = state_.sequencer.patternQuickControls.focusedItem.get();
    return static_cast<int>(core::state::sequencer::quickControlOrderIndex(focused));
}

void SequencerPatternQuickControlsHandler::setFocusedItemByOrderIndex(int index) {
    const int clamped = std::clamp(index, 0, ITEM_COUNT - 1);
    state_.sequencer.patternQuickControls.focusedItem.set(
        core::state::sequencer::quickControlAtOrderIndex(static_cast<size_t>(clamped))
    );
}

int SequencerPatternQuickControlsHandler::currentOffsetMax() const {
    const uint8_t len = state_.sequencer.length.get();
    return (len > 0) ? static_cast<int>(len - 1) : 0;
}

float SequencerPatternQuickControlsHandler::offsetToNormalized(int offsetSteps) const {
    const int maxOffset = currentOffsetMax();
    if (maxOffset <= 0) return 0.5f;
    const int clamped = std::clamp(offsetSteps, -maxOffset, maxOffset);
    return static_cast<float>(clamped + maxOffset) / static_cast<float>(maxOffset * 2);
}

int SequencerPatternQuickControlsHandler::normalizedToOffset(float normalized) const {
    const int maxOffset = currentOffsetMax();
    if (maxOffset <= 0) return 0;
    const int itemCount = (maxOffset * 2) + 1;
    const int index = input_utils::normalizedToIndex(normalized, itemCount);
    return index - maxOffset;
}

void SequencerPatternQuickControlsHandler::applyOffsetFromSnapshot(int offsetSteps) {
    core::state::sequencer::applySnapshot(state_.sequencer, offset_snapshot_);
    if (offsetSteps != 0) {
        core::state::sequencer::rotatePattern(state_.sequencer, offsetSteps);
    }
    clampFocusToLength();
}

}  // namespace core::handler

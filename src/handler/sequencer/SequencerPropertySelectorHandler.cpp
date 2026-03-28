#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {
constexpr int PROPERTY_COUNT =
    static_cast<int>(core::state::sequencer::StepProperty::PROBABILITY) + 1;

inline oc::type::IsActiveFn selectingPredicate(core::state::CoreState& state) {
    return [&state]() { return state.sequencer.stepPropertyInlineSelector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenPropertySelector(core::state::CoreState& state) {
    return [&state]() {
        return !state.overlays.hasVisible() &&
               !state.sequencer.patternQuickControls.selecting.get() &&
               !state.sequencer.rangeSelection.active();
    };
}

}  // namespace

SequencerPropertySelectorHandler::SequencerPropertySelectorHandler(
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

void SequencerPropertySelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope(sequencer_view_scope_))
        .when(canOpenPropertySelector(state_))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope(sequencer_view_scope_))
        .then([this]() { closeApply(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this](float delta) { navigate(delta); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this]() { closeCancel(); });

    OC_LOG_DEBUG("[SequencerPropertySelectorHandler] Bindings setup complete");
}

void SequencerPropertySelectorHandler::open() {
    auto& o = state_.sequencer.stepPropertyInlineSelector;
    o.reset();
    o.selecting.set(true);

    const int active = static_cast<int>(state_.sequencer.activeStepProperty.get());
    o.snapshotIndex = active;
    o.snapshotValid = true;
    o.selectedIndex.set(active);
}

void SequencerPropertySelectorHandler::navigate(float delta) {
    if (!state_.sequencer.stepPropertyInlineSelector.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = state_.sequencer.stepPropertyInlineSelector.selectedIndex.get();
    const int next = nav::nextWrappedIndex(delta, current, PROPERTY_COUNT);
    state_.sequencer.stepPropertyInlineSelector.selectedIndex.set(next);
    state_.sequencer.activeStepProperty.set(
        static_cast<core::state::sequencer::StepProperty>(next)
    );
}

void SequencerPropertySelectorHandler::closeApply() {
    if (!state_.sequencer.stepPropertyInlineSelector.selecting.get()) return;
    state_.sequencer.stepPropertyInlineSelector.reset();
}

void SequencerPropertySelectorHandler::closeCancel() {
    auto& o = state_.sequencer.stepPropertyInlineSelector;
    if (!o.selecting.get()) return;
    if (o.snapshotValid) {
        const int restored = std::clamp(o.snapshotIndex, 0, PROPERTY_COUNT - 1);
        state_.sequencer.activeStepProperty.set(static_cast<core::state::sequencer::StepProperty>(restored));
    }
    o.reset();
}

}  // namespace core::handler

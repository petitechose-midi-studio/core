#include "SequencerTrackSelectorHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

inline oc::type::IsActiveFn selectingPredicate(core::state::CoreState& state) {
    return [&state]() { return state.sequencerTracks.selector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenTrackSelector(core::state::CoreState& state) {
    return [&state]() {
        return !state.overlays.hasVisible() &&
               !state.sequencer.stepEdit.visible.get() &&
               !state.sequencer.rangeSelection.active() &&
               !state.sequencerTracks.selector.selecting.get();
    };
}

}  // namespace

SequencerTrackSelectorHandler::SequencerTrackSelectorHandler(
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

FLASHMEM void SequencerTrackSelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(scope(sequencer_view_scope_))
        .when([this]() {
            return canOpenTrackSelector(state_)() &&
                   buttons_.isPressed(ButtonID::LEFT_BOTTOM);
        })
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .scope(scope(sequencer_view_scope_))
        .when([this]() {
            return canOpenTrackSelector(state_)() &&
                   buttons_.isPressed(ButtonID::LEFT_CENTER);
        })
        .then([this]() { open(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this](float delta) { navigate(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this]() { toggleSelectedTrackEnabled(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this]() { closeApplyIfReleased(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this]() { closeApplyIfReleased(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(sequencer_view_scope_))
        .when(selectingPredicate(state_))
        .then([this]() { closeCancel(); });
}

void SequencerTrackSelectorHandler::open() {
    auto& selector = state_.sequencerTracks.selector;
    if (selector.selecting.get()) return;

    state_.sequencer.patternQuickControls.reset();
    state_.sequencer.stepPropertyInlineSelector.reset();

    const uint8_t activeTrack = state_.sequencerTracks.activeTrack.get();
    selector.reset(activeTrack);
    selector.selecting.set(true);
    selector.selectedTrack.set(activeTrack);
    selector.snapshotTrack = activeTrack;
    selector.snapshotEnabledMask = state_.sequencerTracks.enabledMask.get();
}

void SequencerTrackSelectorHandler::navigate(float delta) {
    auto& selector = state_.sequencerTracks.selector;
    if (!selector.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = selector.selectedTrack.get();
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    selector.selectedTrack.set(static_cast<uint8_t>(next));
}

void SequencerTrackSelectorHandler::toggleSelectedTrackEnabled() {
    auto& selector = state_.sequencerTracks.selector;
    if (!selector.selecting.get()) return;
    state_.sequencerTracks.toggleTrackEnabled(selector.selectedTrack.get());
}

void SequencerTrackSelectorHandler::closeApplyIfReleased() {
    auto& selector = state_.sequencerTracks.selector;
    if (!selector.selecting.get()) return;
    if (buttons_.isPressed(ButtonID::LEFT_CENTER) || buttons_.isPressed(ButtonID::LEFT_BOTTOM)) {
        return;
    }

    core::state::sequencer::switchActiveTrack(
        state_.sequencerTracks,
        state_.sequencer,
        selector.selectedTrack.get()
    );
}

void SequencerTrackSelectorHandler::closeCancel() {
    auto& selector = state_.sequencerTracks.selector;
    if (!selector.selecting.get()) return;
    state_.sequencerTracks.enabledMask.set(selector.snapshotEnabledMask);
    selector.reset(selector.snapshotTrack);
    selector.snapshotEnabledMask = state_.sequencerTracks.enabledMask.get();
}

}  // namespace core::handler

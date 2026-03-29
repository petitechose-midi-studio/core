#include "SequencerTrackSelectorHandler.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerTrackBankState& tracks) {
    return [&tracks]() { return tracks.selector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenTrackSelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) {
    return [&overlays, &sequencer, &tracks]() {
        return !overlays.hasVisible() &&
               !sequencer.stepEdit.visible.get() &&
               !sequencer.rangeSelection.active() &&
               !tracks.selector.selecting.get();
    };
}

}  // namespace

SequencerTrackSelectorHandler::SequencerTrackSelectorHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerTrackSelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return canOpenTrackSelector(overlays_, sequencer_, tracks_)() &&
                   buttons_.isPressed(ButtonID::LEFT_BOTTOM);
        })
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return canOpenTrackSelector(overlays_, sequencer_, tracks_)() &&
                   buttons_.isPressed(ButtonID::LEFT_CENTER);
        })
        .then([this]() { open(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selectingPredicate(tracks_))
        .then([this](float delta) { navigate(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(tracks_))
        .then([this]() { toggleSelectedTrackEnabled(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(tracks_))
        .then([this]() { closeApplyIfReleased(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(tracks_))
        .then([this]() { closeApplyIfReleased(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(tracks_))
        .then([this]() { closeCancel(); });
}

void SequencerTrackSelectorHandler::open() {
    auto& selector = tracks_.selector;
    if (selector.selecting.get()) return;

    sequencer_.patternQuickControls.reset();
    sequencer_.stepPropertyInlineSelector.reset();

    const uint8_t activeTrack = tracks_.activeTrack.get();
    selector.reset(activeTrack);
    selector.selecting.set(true);
    selector.selectedTrack.set(activeTrack);
    selector.snapshotTrack = activeTrack;
    selector.snapshotEnabledMask = tracks_.enabledMask.get();
}

void SequencerTrackSelectorHandler::navigate(float delta) {
    auto& selector = tracks_.selector;
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
    auto& selector = tracks_.selector;
    if (!selector.selecting.get()) return;
    tracks_.toggleTrackEnabled(selector.selectedTrack.get());
}

void SequencerTrackSelectorHandler::closeApplyIfReleased() {
    auto& selector = tracks_.selector;
    if (!selector.selecting.get()) return;
    if (buttons_.isPressed(ButtonID::LEFT_CENTER) || buttons_.isPressed(ButtonID::LEFT_BOTTOM)) {
        return;
    }

    core::state::sequencer::switchActiveTrack(
        tracks_,
        sequencer_,
        selector.selectedTrack.get()
    );
}

void SequencerTrackSelectorHandler::closeCancel() {
    auto& selector = tracks_.selector;
    if (!selector.selecting.get()) return;
    tracks_.enabledMask.set(selector.snapshotEnabledMask);
    selector.reset(selector.snapshotTrack);
    selector.snapshotEnabledMask = tracks_.enabledMask.get();
}

}  // namespace core::handler

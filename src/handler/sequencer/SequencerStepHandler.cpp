#include "SequencerStepHandler.hpp"

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace {

inline oc::type::IsActiveFn notSelectingStepProperty(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) {
    return [&sequencer, &tracks]() {
        return !tracks.selector.selecting.get() &&
               !sequencer.stepPropertyInlineSelector.selecting.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !sequencer.rangeSelection.active();
    };
}

uint8_t countEnabledTracks(uint16_t enabledMask) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
}

}  // namespace

SequencerStepHandler::SequencerStepHandler(StateRefs state,
                                           oc::api::EncoderAPI& encoders,
                                           oc::api::ButtonAPI& buttons,
                                           oc::type::ScopeID scopeId)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerStepHandler::setupBindings() {
    // Toggle step (release = future-proof vs long-press overlays)
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when(notSelectingStepProperty(sequencer_, tracks_))
            .then([this, i]() { toggleStep(i); });
    }

    // Page navigation + toggle on NAV
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this](float delta) {
            if (buttons_.isPressed(Config::ButtonID::NAV)) {
                nav_modifier_used_ = true;
                moveTrack(delta);
                return;
            }
            movePage(delta);
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() {
            if (nav_modifier_used_) {
                nav_modifier_used_ = false;
                return;
            }
            toggleActiveTrackEnabled();
        });
}

void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;

    sequencer_.focusedStep.set(abs);
    sequencer_.toggle(abs);
}

void SequencerStepHandler::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    if (delta > 0.0f) {
        nextPage();
        return;
    }

    prevPage();
}

void SequencerStepHandler::moveTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = tracks_.activeTrack.get();
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    if (next == current) return;

    core::state::sequencer::switchActiveTrack(
        tracks_,
        sequencer_,
        static_cast<uint8_t>(next)
    );
}

void SequencerStepHandler::toggleActiveTrackEnabled() {
    const uint8_t activeTrack = tracks_.activeTrack.get();
    const uint16_t enabledMask = tracks_.enabledMask.get();
    const uint16_t bit = static_cast<uint16_t>(1U << activeTrack);
    const bool currentlyEnabled = (enabledMask & bit) != 0;

    if (currentlyEnabled && countEnabledTracks(enabledMask) <= 1U) {
        return;
    }

    tracks_.setTrackEnabled(activeTrack, !currentlyEnabled);
}

void SequencerStepHandler::prevPage() {
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount <= 1) return;

    const uint8_t current = sequencer_.normalizePage(sequencer_.page.get());
    const uint8_t next = (current == 0) ? (pageCount - 1) : (current - 1);
    sequencer_.page.set(next);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(next));
}

void SequencerStepHandler::nextPage() {
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount <= 1) return;

    const uint8_t current = sequencer_.normalizePage(sequencer_.page.get());
    const uint8_t next = static_cast<uint8_t>((current + 1) % pageCount);
    sequencer_.page.set(next);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(next));
}

}  // namespace core::handler

#include "SequencerStepHandler.hpp"

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"

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
        .then([this](float delta) { movePage(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() { toggleFocusedStep(); });
}

void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;

    sequencer_.focusedStep.set(abs);
    sequencer_.toggle(abs);
}

void SequencerStepHandler::toggleFocusedStep() {
    const uint8_t len = sequencer_.length.get();
    if (len == 0) return;

    const uint8_t abs = sequencer_.focusedStep.get();
    if (abs >= len) return;
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

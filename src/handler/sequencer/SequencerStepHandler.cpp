#include "SequencerStepHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;

namespace {

inline oc::type::IsActiveFn notSelectingStepProperty(core::state::CoreState& state) {
    return [&state]() {
        return !state.sequencerTracks.selector.selecting.get() &&
               !state.sequencer.stepPropertyInlineSelector.selecting.get() &&
               !state.sequencer.patternQuickControls.selecting.get() &&
               !state.sequencer.rangeSelection.active();
    };
}

}  // namespace

SequencerStepHandler::SequencerStepHandler(core::state::CoreState& state,
                                           oc::api::EncoderAPI& encoders,
                                           oc::api::ButtonAPI& buttons,
                                           lv_obj_t* scopeElement)
    : state_(state)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(scopeElement) {
    setupBindings();
}

FLASHMEM void SequencerStepHandler::setupBindings() {
    // Toggle step (release = future-proof vs long-press overlays)
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope(scope_element_))
            .when(notSelectingStepProperty(state_))
            .then([this, i]() { toggleStep(i); });
    }

    // Page navigation + toggle on NAV
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(scope_element_))
        .when(notSelectingStepProperty(state_))
        .then([this](float delta) { movePage(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope(scope_element_))
        .when(notSelectingStepProperty(state_))
        .then([this]() { toggleFocusedStep(); });
}

void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!state_.sequencer.resolveStepInPage(state_.sequencer.page.get(), indexInPage, abs)) return;

    state_.sequencer.focusedStep.set(abs);
    state_.sequencer.toggle(abs);
}

void SequencerStepHandler::toggleFocusedStep() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t abs = state_.sequencer.focusedStep.get();
    if (abs >= len) return;
    state_.sequencer.toggle(abs);
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
    const uint8_t pageCount = state_.sequencer.activePageCount();
    if (pageCount <= 1) return;

    const uint8_t current = state_.sequencer.normalizePage(state_.sequencer.page.get());
    const uint8_t next = (current == 0) ? (pageCount - 1) : (current - 1);
    state_.sequencer.page.set(next);
    state_.sequencer.focusedStep.set(state_.sequencer.pageStartStep(next));
}

void SequencerStepHandler::nextPage() {
    const uint8_t pageCount = state_.sequencer.activePageCount();
    if (pageCount <= 1) return;

    const uint8_t current = state_.sequencer.normalizePage(state_.sequencer.page.get());
    const uint8_t next = static_cast<uint8_t>((current + 1) % pageCount);
    state_.sequencer.page.set(next);
    state_.sequencer.focusedStep.set(state_.sequencer.pageStartStep(next));
}

}  // namespace core::handler

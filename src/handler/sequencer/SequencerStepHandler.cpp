#include "SequencerStepHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>
#include <oc/util/Index.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;
using oc::util::wrapIndex;

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

void SequencerStepHandler::setupBindings() {
    // Toggle step (release = future-proof vs long-press overlays)
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope(scope_element_))
            .then([this, i]() { toggleStep(i); });
    }

    // Page switch
    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope(scope_element_))
        .then([this]() { prevPage(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope(scope_element_))
        .then([this]() { nextPage(); });

    // Focus navigation + toggle on NAV
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(scope_element_))
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope(scope_element_))
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

void SequencerStepHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;
    const int step = (delta > 0.0f) ? 1 : -1;

    const int len = static_cast<int>(state_.sequencer.length.get());
    if (len <= 0) return;

    const int current = static_cast<int>(state_.sequencer.focusedStep.get());
    const int safeCurrent = (current < 0 || current >= len) ? 0 : current;
    const int next = wrapIndex(safeCurrent + step, len);
    const uint8_t nextStep = static_cast<uint8_t>(next);
    state_.sequencer.focusedStep.set(nextStep);
    state_.sequencer.page.set(state_.sequencer.pageForStep(nextStep));
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

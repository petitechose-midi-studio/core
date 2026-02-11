#include "SequencerStepHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;

static int wrapIndex(int idx, int count) {
    if (count <= 0) return 0;
    idx %= count;
    if (idx < 0) idx += count;
    return idx;
}

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
    constexpr uint8_t pageCount = core::state::sequencer::SequencerState::PAGE_COUNT;
    const uint8_t page = (pageCount == 0)
        ? 0
        : static_cast<uint8_t>(state_.sequencer.page.get() % pageCount);

    const uint8_t abs = page * core::state::sequencer::SequencerState::STEPS_PER_PAGE + indexInPage;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    state_.sequencer.focusedStep.set(abs);
    state_.sequencer.toggle(abs);
}

void SequencerStepHandler::toggleFocusedStep() {
    const uint8_t abs = state_.sequencer.focusedStep.get();
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;
    state_.sequencer.toggle(abs);
}

void SequencerStepHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;
    int step = (delta > 0.0f) ? 1 : -1;

    constexpr int maxSteps = core::state::sequencer::SequencerState::MAX_STEPS;
    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;

    const int current = static_cast<int>(state_.sequencer.focusedStep.get());
    const int next = wrapIndex(current + step, maxSteps);
    state_.sequencer.focusedStep.set(static_cast<uint8_t>(next));
    state_.sequencer.page.set(static_cast<uint8_t>(next / stepsPerPage));
}

void SequencerStepHandler::prevPage() {
    constexpr uint8_t pageCount = core::state::sequencer::SequencerState::PAGE_COUNT;
    if (pageCount <= 1) return;

    const uint8_t current = static_cast<uint8_t>(state_.sequencer.page.get() % pageCount);
    const uint8_t next = (current == 0) ? (pageCount - 1) : (current - 1);
    state_.sequencer.page.set(next);
    state_.sequencer.focusedStep.set(next * core::state::sequencer::SequencerState::STEPS_PER_PAGE);
}

void SequencerStepHandler::nextPage() {
    constexpr uint8_t pageCount = core::state::sequencer::SequencerState::PAGE_COUNT;
    if (pageCount <= 1) return;

    const uint8_t current = static_cast<uint8_t>(state_.sequencer.page.get() % pageCount);
    const uint8_t next = static_cast<uint8_t>((current + 1) % pageCount);
    state_.sequencer.page.set(next);
    state_.sequencer.focusedStep.set(next * core::state::sequencer::SequencerState::STEPS_PER_PAGE);
}

}  // namespace core::handler

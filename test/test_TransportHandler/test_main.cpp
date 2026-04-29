#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/transport/TransportHandler.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

using test_support::TestButtonHardware;

struct TransportHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 1001;
    static constexpr oc::type::ScopeID SEQUENCER_VIEW_SCOPE = 1002;

    core::state::StatusBarState statusBar;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    oc::api::ButtonAPI buttons;
    core::handler::TransportHandler handler;

    TransportHarness()
        : inputBinding(eventBus)
        , buttons(inputBinding, buttonHw)
        , handler(core::handler::TransportHandler::StateRefs{statusBar},
                  buttons,
                  core::handler::TransportHandler::ViewScopes{
                      MACRO_VIEW_SCOPE,
                      SEQUENCER_VIEW_SCOPE,
                  }) {}

    void press(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(buttonId, true));
    }

    void release(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(buttonId));
    }

    void tap(Config::ButtonID id) {
        press(id);
        release(id);
    }
};

void test_bottom_center_toggles_play_state() {
    TransportHarness h;
    assert(!h.statusBar.playing.get());

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(h.statusBar.playing.get());

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(!h.statusBar.playing.get());

    std::cout << "[PASS] test_bottom_center_toggles_play_state\n";
}

void test_transport_lock_blocks_play_toggle() {
    TransportHarness h;
    h.statusBar.transportLocked.set(true);

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(!h.statusBar.playing.get());

    h.statusBar.transportLocked.set(false);
    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(h.statusBar.playing.get());

    std::cout << "[PASS] test_transport_lock_blocks_play_toggle\n";
}

}  // namespace

int main() {
    test_bottom_center_toggles_play_state();
    test_transport_lock_blocks_play_toggle();

    std::cout << "\nAll TransportHandler tests passed.\n";
    return 0;
}

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include "../../src/handler/macro/MacroDomainServices.hpp"
// Native tests only build selected source folders; include the implementation
// here so this handler-level service remains testable without widening the
// environment's global src filter.
#include "../../src/handler/macro/MacroDomainServices.cpp"
#include "../../src/handler/macro/MacroEditHandler.hpp"
// Same rationale for the handler itself: keep the native src filter narrow
// while still testing the real binding logic end to end.
#include "../../src/handler/macro/MacroEditHandler.cpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::CoreStorages;
using test_support::drainNotifications;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct MacroEditHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 401;
    static constexpr oc::type::ScopeID EDIT_SCOPE = 402;
    static constexpr oc::type::ScopeID VALUE_SCOPE = 403;
    static constexpr oc::type::ScopeID PAGE_SCOPE = 404;
    static constexpr oc::type::ScopeID TARGET_SCOPE = 405;

    CoreStorages storage;
    core::state::CoreState state;
    core::handler::MacroDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroEditHandler handler;

    MacroEditHarness()
        : state(storage.settings,
                storage.macroWorkspace,
                storage.macroLibrary,
                storage.sequencerWorkspace,
                storage.sequencerPatternLibrary,
                storage.sequencerSetLibrary)
        , services(core::handler::MacroDomainServices::fromCoreState(state))
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::MacroEditHandler::StateRefs{
                      state.macroEdit,
                      state.pages,
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  MACRO_VIEW_SCOPE,
                  EDIT_SCOPE,
                  VALUE_SCOPE,
                  PAGE_SCOPE,
                  TARGET_SCOPE,
                  mockTimeMs) {
        overlays.registerCleanup(core::ui::OverlayType::MACRO_EDIT, EDIT_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::MACRO_EDIT_SELECTOR, VALUE_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::PAGE_SELECTOR, PAGE_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, TARGET_SCOPE);
        overlays.setActiveViewProvider([]() { return MACRO_VIEW_SCOPE; });
    }

    void tick(uint32_t nowMs) {
        g_now_ms = nowMs;
        inputBinding.processTick();
    }

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

    void pressMacro(uint8_t index) {
        press(Config::MACRO_BUTTONS[index]);
    }

    void releaseMacro(uint8_t index) {
        release(Config::MACRO_BUTTONS[index]);
    }

    void tapMacro(uint8_t index) {
        tap(Config::MACRO_BUTTONS[index]);
    }

    void turn(Config::EncoderID id, float delta) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, delta);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, delta));
    }

    void flushState() {
        drainNotifications();
        state.flush();
    }
};

void openMacroEdit(MacroEditHarness& h, uint8_t macroIndex, uint32_t releaseAtMs) {
    h.tick(0);
    h.pressMacro(macroIndex);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(!h.state.macroEdit.visible.get());
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.editingIndex.get() == macroIndex);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    h.tick(releaseAtMs);
    h.releaseMacro(macroIndex);
}

void test_quick_release_keeps_macro_edit_open_and_left_top_closes() {
    MacroEditHarness h;

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );

    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.flushState();

    std::cout << "[PASS] test_quick_release_keeps_macro_edit_open_and_left_top_closes\n";
}

void test_slow_release_closes_macro_edit_immediately() {
    MacroEditHarness h;

    openMacroEdit(
        h,
        1,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 600U
    );

    assert(!h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.flushState();

    std::cout << "[PASS] test_slow_release_closes_macro_edit_immediately\n";
}

void test_macro_edit_live_and_selector_flows_apply_immediately() {
    MacroEditHarness h;

    h.state.pages.pages[2].channel[0] = 5;
    h.state.pages.pages[2].cc[0] = 67;
    h.state.pages.pages[2].channel[3] = 9;
    h.state.pages.pages[2].cc[3] = 99;
    std::strncpy(h.state.pages.pages[2].name, "Mix Bus", core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    assert(h.state.macroEdit.tempChannel.get() == 0);
    assert(h.state.macroEdit.tempCC.get() == 1);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.macroEdit.tempChannel.get() == 15);
    assert(h.services.activeConfig(0).channel == 15);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.focusedRow.get() == 1);
    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.macroEdit.tempCC.get() == 0);
    assert(h.services.activeConfig(0).cc == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::VALUE_SELECTOR);
    assert(h.state.macroEdit.selector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT_SELECTOR);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.selector.selectedIndex.get() == 1);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.state.macroEdit.tempCC.get() == 1);
    assert(h.services.activeConfig(0).cc == 1);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::PAGE_SELECTOR);
    assert(h.state.pages.selector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::PAGE_SELECTOR);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.pages.selector.selectedIndex.get() == 2);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.pages.activePage == 2);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Mix Bus") == 0);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.state.macroEdit.tempChannel.get() == 5);
    assert(h.state.macroEdit.tempCC.get() == 67);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::TARGET_SELECTOR);
    assert(h.state.macroEdit.macroSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.macroSelector.selectedIndex.get() == 3);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.state.macroEdit.editingIndex.get() == 3);
    assert(h.state.macroEdit.tempChannel.get() == 9);
    assert(h.state.macroEdit.tempCC.get() == 99);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(!h.state.pages.selector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.flushState();

    std::cout << "[PASS] test_macro_edit_live_and_selector_flows_apply_immediately\n";
}

}  // namespace

int main() {
    test_quick_release_keeps_macro_edit_open_and_left_top_closes();
    test_slow_release_closes_macro_edit_immediately();
    test_macro_edit_live_and_selector_flows_apply_immediately();
    std::cout << "\nAll MacroEditHandler tests passed.\n";
    return 0;
}

#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/view/ViewSwitcherHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct ViewSwitcherHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 801;
    static constexpr oc::type::ScopeID SEQUENCER_VIEW_SCOPE = 802;
    static constexpr oc::type::ScopeID PROJECT_VIEW_SCOPE = 803;
    static constexpr oc::type::ScopeID DEVICE_SETTINGS_VIEW_SCOPE = 804;
    static constexpr oc::type::ScopeID VIEW_SELECTOR_SCOPE = 805;

    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::ViewSwitcherHandler handler;

    ViewSwitcherHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::ViewSwitcherHandler::StateRefs{
                      state.overlays,
                      state.activeView,
                      state.viewSelector,
                      state.sequencerSettings,
                      state.sequencer.patternQuickControls,
                      state.sequencer.stepPropertyInlineSelector,
                      state.trackNavigation.selection,
                      state.macroUi.pageSelection,
                      state.sequencer.structureUi.pageSelection,
                      state.projectNavigation,
                  },
                  overlays,
                  encoders,
                  buttons,
                  core::handler::ViewSwitcherHandler::ViewScopes{
                      MACRO_VIEW_SCOPE,
                      SEQUENCER_VIEW_SCOPE,
                      PROJECT_VIEW_SCOPE,
                      DEVICE_SETTINGS_VIEW_SCOPE,
                  },
                  VIEW_SELECTOR_SCOPE) {
        overlays.registerCleanup(core::ui::OverlayType::VIEW_SELECTOR, VIEW_SELECTOR_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::SEQUENCER_SETTINGS, VIEW_SELECTOR_SCOPE);
        overlays.setActiveViewProvider([this]() {
            switch (state.activeView.get()) {
                case core::ui::ViewType::SEQUENCER:
                    return SEQUENCER_VIEW_SCOPE;
                case core::ui::ViewType::PROJECT:
                    return PROJECT_VIEW_SCOPE;
                case core::ui::ViewType::DEVICE_SETTINGS:
                    return DEVICE_SETTINGS_VIEW_SCOPE;
                case core::ui::ViewType::MACRO:
                default:
                    return MACRO_VIEW_SCOPE;
            }
        });
        g_now_ms = 0;
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

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

void openSelector(ViewSwitcherHarness& h) {
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);
}

void test_view_selector_opens_navigates_and_confirms_on_close() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 1);
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);

    std::cout << "[PASS] test_view_selector_opens_navigates_and_confirms_on_close\n";
}

void test_nav_release_confirms_without_closing_selector() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);
    assert(h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);

    std::cout << "[PASS] test_nav_release_confirms_without_closing_selector\n";
}

void test_project_item_switches_to_project_view() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 2);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::PROJECT);

    std::cout << "[PASS] test_project_item_switches_to_project_view\n";
}

void test_selector_uses_active_view_scope() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 1);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);

    std::cout << "[PASS] test_selector_uses_active_view_scope\n";
}

void test_selector_uses_project_active_view_scope() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::PROJECT);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 2);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);

    std::cout << "[PASS] test_selector_uses_project_active_view_scope\n";
}

void test_device_settings_item_switches_to_device_settings_view() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 3);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(!h.state.deviceSettings.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::DEVICE_SETTINGS);

    std::cout << "[PASS] test_device_settings_item_switches_to_device_settings_view\n";
}

void test_sequencer_item_no_longer_exposes_settings_action() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 1);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.viewSelector.visible.get());
    assert(!h.state.sequencerSettings.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);

    std::cout << "[PASS] test_sequencer_item_no_longer_exposes_settings_action\n";
}

void test_selector_does_not_open_when_overlay_or_structure_selection_is_active() {
    {
        ViewSwitcherHarness h;
        h.state.overlays.show(core::ui::OverlayType::DATA_MANAGER);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.trackNavigation.selection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::MACRO);
        h.state.macroUi.pageSelection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.structureUi.pageSelection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    std::cout << "[PASS] test_selector_does_not_open_when_overlay_or_structure_selection_is_active\n";
}

void test_selector_does_not_open_while_sequencer_inline_modes_are_active() {
    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.patternQuickControls.selecting.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    std::cout << "[PASS] test_selector_does_not_open_while_sequencer_inline_modes_are_active\n";
}

void test_selector_does_not_open_inside_project_folder() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::PROJECT);
    h.state.projectNavigation.activeTab.set(core::state::project::ProjectTab::MUSIC);
    h.state.projectNavigation.currentNode.set(core::state::project::ProjectNodeId::MUSIC_SCALE);
    h.state.projectNavigation.depth.set(1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());

    std::cout << "[PASS] test_selector_does_not_open_inside_project_folder\n";
}

}  // namespace

int main() {
    test_view_selector_opens_navigates_and_confirms_on_close();
    test_nav_release_confirms_without_closing_selector();
    test_project_item_switches_to_project_view();
    test_selector_uses_active_view_scope();
    test_selector_uses_project_active_view_scope();
    test_device_settings_item_switches_to_device_settings_view();
    test_sequencer_item_no_longer_exposes_settings_action();
    test_selector_does_not_open_when_overlay_or_structure_selection_is_active();
    test_selector_does_not_open_while_sequencer_inline_modes_are_active();
    test_selector_does_not_open_inside_project_folder();

    std::cout << "\nAll ViewSwitcherHandler tests passed.\n";
    return 0;
}

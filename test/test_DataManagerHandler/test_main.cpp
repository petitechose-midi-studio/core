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
#include <oc/time/Time.hpp>
#include "../../src/handler/settings/DataManagerDomainServices.hpp"
#include "../../src/handler/settings/DataManagerFeedbackFormatter.hpp"
#include "../../src/handler/settings/DataManagerHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerPersistenceWorkflow.hpp"
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

struct DataManagerHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 201;
    static constexpr oc::type::ScopeID SEQ_VIEW_SCOPE = 202;
    static constexpr oc::type::ScopeID MANAGER_SCOPE = 301;
    static constexpr oc::type::ScopeID DIALOG_SCOPE = 302;

    CoreStorages storage;
    core::state::CoreState state;
    core::handler::DataManagerDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::DataManagerHandler::ViewScopes viewScopes{
        MACRO_VIEW_SCOPE,
        SEQ_VIEW_SCOPE,
    };
    core::handler::DataManagerHandler handler;

    DataManagerHarness()
        : state(storage.settings,
                storage.macroWorkspace,
                storage.macroLibrary,
                storage.sequencerWorkspace,
                storage.sequencerPatternLibrary,
                storage.sequencerSetLibrary)
        , services(core::handler::DataManagerDomainServices::fromCoreState(state))
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::DataManagerHandler::StateRefs{
                      state.dataManager,
                      state.activeView,
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  viewScopes,
                  MANAGER_SCOPE,
                  DIALOG_SCOPE) {
        overlays.registerCleanup(core::ui::OverlayType::DATA_MANAGER, MANAGER_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::DATA_MANAGER_DIALOG, DIALOG_SCOPE);
        overlays.setActiveViewProvider([this]() {
            return state.activeView.get() == core::ui::ViewType::SEQUENCER
                       ? SEQ_VIEW_SCOPE
                       : MACRO_VIEW_SCOPE;
        });
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

void openManagerWithLongPress(DataManagerHarness& h, core::ui::ViewType view) {
    h.state.activeView.set(view);
    h.tick(0);
    h.press(Config::ButtonID::NAV);
    h.tick(1999);
    assert(!h.state.dataManager.visible.get());
    h.tick(2000);
    assert(h.state.dataManager.visible.get());
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);
    assert(h.overlays.current() == core::ui::OverlayType::DATA_MANAGER);
}

void test_long_press_opens_manager_with_active_view_context_and_ignores_release() {
    DataManagerHarness h;

    openManagerWithLongPress(h, core::ui::ViewType::SEQUENCER);
    assert(h.state.dataManager.context.get() == core::state::DataManagerContext::SEQUENCER);

    h.release(Config::ButtonID::NAV);
    assert(h.state.dataManager.visible.get());
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);
    assert(h.overlays.current() == core::ui::OverlayType::DATA_MANAGER);

    h.flushState();

    std::cout << "[PASS] test_long_press_opens_manager_with_active_view_context_and_ignores_release\n";
}

void test_macro_shortcut_save_then_confirm_cancel_flow() {
    DataManagerHarness h;

    openManagerWithLongPress(h, core::ui::ViewType::MACRO);
    h.release(Config::ButtonID::NAV);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::SLOT_PICKER);
    assert(h.state.dataManager.pendingCommand.get() == core::state::DataManagerCommand::MACRO_SAVE_SLOT);
    assert(h.state.dataManager.dialog.mode.get() == core::state::DataManagerDialogMode::SLOT_PICKER);
    assert(h.overlays.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG);

    h.tap(Config::ButtonID::NAV);
    assert(h.services.slotOccupied(core::state::DataManagerCommand::MACRO_SAVE_SLOT, 0));
    assert(std::strcmp(h.state.dataManager.feedback.get(), "Saved M01") == 0);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);
    assert(h.overlays.current() == core::ui::OverlayType::DATA_MANAGER);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::SLOT_PICKER);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::CONFIRM);
    assert(h.state.dataManager.dialog.mode.get() == core::state::DataManagerDialogMode::CONFIRM);

    h.tap(Config::ButtonID::NAV);
    assert(std::strcmp(h.state.dataManager.feedback.get(), "Cancelled") == 0);
    assert(h.state.dataManager.pendingCommand.get() == core::state::DataManagerCommand::NONE);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);
    assert(h.overlays.current() == core::ui::OverlayType::DATA_MANAGER);

    h.flushState();

    std::cout << "[PASS] test_macro_shortcut_save_then_confirm_cancel_flow\n";
}

void test_sequencer_command_palette_load_set_flow_uses_mode_selector() {
    DataManagerHarness h;

    h.state.sequencer.length.set(8);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(h.state, 0));
    h.state.sequencer.length.set(16);

    openManagerWithLongPress(h, core::ui::ViewType::SEQUENCER);
    h.release(Config::ButtonID::NAV);

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::COMMAND_PALETTE);
    assert(h.state.dataManager.dialog.mode.get() == core::state::DataManagerDialogMode::COMMAND_PALETTE);

    for (int i = 0; i < 4; ++i) {
        h.turn(Config::EncoderID::NAV, 1.0f);
    }
    assert(h.state.dataManager.dialog.selectedIndex.get() == 4);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.dataManager.pendingCommand.get() == core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::SLOT_PICKER);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::SET_LOAD_MODE);
    assert(h.state.dataManager.dialog.mode.get() == core::state::DataManagerDialogMode::SET_LOAD_MODE);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.length.get() == 8);
    assert(std::strcmp(h.state.dataManager.feedback.get(), "Loaded S01 R") == 0);
    assert(h.state.dataManager.pendingCommand.get() == core::state::DataManagerCommand::NONE);
    assert(h.state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);
    assert(h.overlays.current() == core::ui::OverlayType::DATA_MANAGER);

    h.flushState();

    std::cout << "[PASS] test_sequencer_command_palette_load_set_flow_uses_mode_selector\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_long_press_opens_manager_with_active_view_context_and_ignores_release();
    test_macro_shortcut_save_then_confirm_cancel_flow();
    test_sequencer_command_palette_load_set_flow_uses_mode_selector();
    std::cout << "\nAll DataManagerHandler tests passed.\n";
    return 0;
}

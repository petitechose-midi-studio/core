#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/macro/MacroAutomationHandler.hpp"
#include "../../src/handler/macro/MacroEditDomainServices.hpp"
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

struct MacroAutomationHarness {
    static constexpr oc::type::ScopeID AUTOMATION_SCOPE = 1401;

    CoreStorages storage;
    core::state::CoreState state;
    core::handler::MacroEditDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroAutomationHandler handler;

    MacroAutomationHarness()
        : state(storage.settings,
                storage.macroLibrary,
                storage.sequencerPatternLibrary,
                storage.sequencerSetLibrary)
        , services(core::handler::MacroEditDomainServices::fromCoreState(state))
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::MacroAutomationHandler::StateRefs{state.macroEdit},
                  services,
                  overlays,
                  encoders,
                  buttons,
                  AUTOMATION_SCOPE) {
        overlays.registerCleanup(core::ui::OverlayType::MACRO_AUTOMATION, AUTOMATION_SCOPE);
        overlays.setActiveViewProvider([]() { return AUTOMATION_SCOPE; });
        g_now_ms = 0;
    }

    void openAutomationEditor(uint8_t macroIndex = 0) {
        state.macroEdit.openEditor(macroIndex, 0, 0, 0);
        state.macroEdit.openAutomation();
        overlays.show(core::ui::OverlayType::MACRO_AUTOMATION);
    }

    void configureAutomation(uint8_t macroIndex = 0, float durationBeats = 2.0f) {
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            state.pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = state.pages.currentActiveTrack(),
                .page = state.pages.currentActivePage(),
                .macro = macroIndex,
            }
        );
        assert(slot != nullptr);
        core::state::macro::MacroAutomationLane lane;
        lane.durationBeats = durationBeats;
        assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
        assert(core::state::macro::macroAutomationAppendPoint(
            lane,
            durationBeats * 0.5f,
            1.0f
        ));
        assert(core::state::macro::macroAutomationAppendPoint(lane, durationBeats, 0.0f));
        assert(core::state::macro::macroAutomationAssignAutomation(
            state.pages.automation,
            *slot,
            lane
        ));
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
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

    void flushState() {
        drainNotifications();
        state.flush();
    }
};

void test_state_row_restores_auto_without_clearing_lane() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();
    h.state.macroUi.automationManualOverrideMask.set(0x0001);

    h.turn(Config::EncoderID::OPT, 1.0f);

    assert((h.state.macroUi.automationManualOverrideMask.get() & 0x0001) == 0);
    const auto* preserved = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(preserved != nullptr);
    assert(preserved->automation.active);
    assert(preserved->automation.pointCount == 3);

    h.flushState();

    std::cout << "[PASS] test_state_row_restores_auto_without_clearing_lane\n";
}

void test_clear_automation_clears_manual_override() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();
    h.state.macroUi.automationManualOverrideMask.set(0x0001);

    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert((h.state.macroUi.automationManualOverrideMask.get() & 0x0001) == 0);
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(!slot->automation.active);

    h.flushState();

    std::cout << "[PASS] test_clear_automation_clears_manual_override\n";
}

void test_length_row_resizes_automation_duration_without_scaling_points() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 1);
    h.turn(Config::EncoderID::OPT, 2.0f / 63.0f);

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(slot->automation.active);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 3.0f);

    h.turn(Config::EncoderID::OPT, 1.0f);

    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 64.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.sourceDurationTicks) == 2.0f);
    assert(slot->automation.pointCount == 3);

    core::state::macro::MacroCurvePoint point{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        h.state.pages.automation.pointPool,
        1,
        false,
        point
    ));
    assert(point.beat == 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.windowOffsetTicks) == 1.0f);

    h.flushState();

    std::cout << "[PASS] test_length_row_resizes_automation_duration_without_scaling_points\n";
}

void test_left_center_enables_coarse_length_and_offset_steps_temporarily() {
    MacroAutomationHarness h;
    h.configureAutomation(0, 8.0f);
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 1);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 16);
    h.turn(Config::EncoderID::OPT, 1.0f / 15.0f);

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 8.0f);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 2);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.windowOffsetTicks) == 4.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.flushState();

    std::cout << "[PASS] test_left_center_enables_coarse_length_and_offset_steps_temporarily\n";
}

}  // namespace

int main() {
    test_state_row_restores_auto_without_clearing_lane();
    test_clear_automation_clears_manual_override();
    test_length_row_resizes_automation_duration_without_scaling_points();
    test_left_center_enables_coarse_length_and_offset_steps_temporarily();
    std::cout << "\nAll MacroAutomationHandler tests passed.\n";
    return 0;
}

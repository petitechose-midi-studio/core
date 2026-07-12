#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Result.hpp>

#include "../../src/handler/macro/MacroValueHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

class MockMidiTransport : public oc::interface::IMidi {
public:
    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
        ccCount += 1;
        lastChannel = channel;
        lastCc = cc;
        lastValue = value;
    }
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void sendSysEx(const uint8_t*, size_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendPitchBend(uint8_t, int16_t) override {}
    void sendChannelPressure(uint8_t, uint8_t) override {}
    void sendClock() override {}
    void sendStart() override {}
    void sendStop() override {}
    void sendContinue() override {}
    void setOnCC(CCCallback) override {}
    void setOnNoteOn(NoteCallback) override {}
    void setOnNoteOff(NoteCallback) override {}
    void setOnSysEx(SysExCallback) override {}
    void setOnClock(ClockCallback) override {}
    void setOnStart(RealtimeCallback) override {}
    void setOnStop(RealtimeCallback) override {}
    void setOnContinue(RealtimeCallback) override {}

    int ccCount = 0;
    uint8_t lastChannel = 0;
    uint8_t lastCc = 0;
    uint8_t lastValue = 0;
};

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct MacroValueHarness {
    static constexpr oc::type::ScopeID MACRO_SCOPE = 1201;

    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroValueHandler handler;

    MacroValueHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , midi(midiTransport)
        , overlays(state.overlays, buttons)
        , handler(core::handler::MacroValueHandler::StateRefs{
                      state.macroUi,
                      state.activeView,
                      state.macroEdit,
                  },
                  core::handler::MacroPerformanceDomainServices::fromCoreState(state),
                  overlays,
                  encoders,
                  buttons,
                  midi,
                  MACRO_SCOPE) {
        state.activeView.set(core::ui::ViewType::MACRO);
        g_now_ms = 0;
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
};

void configureAutomation(core::state::CoreState& state, uint8_t macroIndex = 0) {
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
    lane.durationBeats = 2.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *slot,
        lane
    ));
}

void test_macro_encoder_updates_value_and_sends_cc() {
    MacroValueHarness h;

    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(std::fabs(h.state.macros[0].value.get() - 1.0f) < 0.0005f);
    assert(h.midiTransport.ccCount == 1);
    assert(h.midiTransport.lastChannel == 0);
    assert(h.midiTransport.lastCc == 0);
    assert(h.midiTransport.lastValue == 127);
    assert(h.state.statusBar.ccOutActive.get());
    assert(std::fabs(h.state.pages.activePageData().values[0] - 1.0f) < 0.0005f);
    assert(h.state.hasPendingProjectMutationCoalescing());

    std::cout << "[PASS] test_macro_encoder_updates_value_and_sends_cc\n";
}

void test_macro_encoder_does_not_activate_empty_or_add_slots() {
    MacroValueHarness h;

    assert(h.state.pages.isMacroSlotActive(0));
    assert(!h.state.pages.isMacroSlotActive(1));
    assert(!h.state.pages.isMacroSlotActive(2));

    h.turn(Config::EncoderID::MACRO_3, 1.0f);
    assert(!h.state.pages.isMacroSlotActive(2));
    assert(h.midiTransport.ccCount == 0);

    h.turn(Config::EncoderID::MACRO_2, 1.0f);
    assert(!h.state.pages.isMacroSlotActive(1));
    assert(h.state.pages.nextAddMacroIndex() == 1);
    assert(h.midiTransport.ccCount == 0);

    std::cout << "[PASS] test_macro_encoder_does_not_activate_empty_or_add_slots\n";
}

void test_macro_value_handler_respects_modal_guards() {
    {
        MacroValueHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(std::fabs(h.state.macros[0].value.get() - 0.5f) < 0.0005f);
        assert(h.midiTransport.ccCount == 0);
    }

    {
        MacroValueHarness h;
        h.state.overlays.show(core::ui::OverlayType::DATA_MANAGER);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(std::fabs(h.state.macros[0].value.get() - 0.5f) < 0.0005f);
        assert(h.midiTransport.ccCount == 0);
    }

    std::cout << "[PASS] test_macro_value_handler_respects_modal_guards\n";
}

void test_macro_encoder_feeds_armed_automation_recording() {
    MacroValueHarness h;
    auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(h.state);

    h.state.statusBar.tempo.set(120.0f);
    assert(services.beginAutomationRecording(0, 0));

    g_now_ms = 500;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(services.commitAutomationRecording(1000));

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
    assert(slot->automation.pointCount == 2);
    core::state::macro::MacroCurvePoint firstPoint{};
    core::state::macro::MacroCurvePoint secondPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        h.state.pages.automation.pointPool,
        0,
        false,
        firstPoint
    ));
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        h.state.pages.automation.pointPool,
        1,
        false,
        secondPoint
    ));
    assert(std::fabs(firstPoint.value - 0.5f) < 0.0001f);
    assert(std::fabs(secondPoint.beat - 1.0f) < 0.0001f);
    assert(std::fabs(secondPoint.value - 1.0f) < 0.0001f);

    std::cout << "[PASS] test_macro_encoder_feeds_armed_automation_recording\n";
}

void test_macro_button_hold_records_value_automation() {
    MacroValueHarness h;

    h.state.statusBar.tempo.set(120.0f);
    h.press(Config::ButtonID::MACRO_1);
    assert(!h.state.macroUi.automationRecording.active);

    g_now_ms = 500;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(h.state.macroUi.automationRecording.active);
    assert(h.state.macroUi.automationRecording.address.macro == 0);

    g_now_ms = 1000;
    h.release(Config::ButtonID::MACRO_1);
    assert(!h.state.macroUi.automationRecording.active);

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
    assert(slot->automation.pointCount == 1);
    assert(std::fabs(core::state::macro::macroAutomationBeatsFromTicks(
                         slot->automation.durationTicks
                     ) - 1.0f) < 0.0001f);
    core::state::macro::MacroCurvePoint recordedPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        h.state.pages.automation.pointPool,
        0,
        false,
        recordedPoint
    ));
    assert(std::fabs(recordedPoint.beat - 0.0f) < 0.0001f);
    assert(std::fabs(recordedPoint.value - 1.0f) < 0.0001f);
    assert(h.state.project.metadata.dirty);

    h.turn(Config::EncoderID::MACRO_1, 0.0f);
    assert(std::fabs(h.state.macros[0].value.get() - 1.0f) < 0.0005f);

    std::cout << "[PASS] test_macro_button_hold_records_value_automation\n";
}

void test_turning_an_automated_macro_enters_manual_override() {
    MacroValueHarness h;

    configureAutomation(h.state);

    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert((h.state.macroUi.automationManualOverrideMask.get() & 0x0001) != 0);
    assert(h.midiTransport.ccCount == 1);
    assert(h.midiTransport.lastValue == 127);

    std::cout << "[PASS] test_turning_an_automated_macro_enters_manual_override\n";
}

void test_macro_automation_property_button_restores_auto_without_clearing_lane() {
    MacroValueHarness h;

    configureAutomation(h.state);

    h.state.macroUi.clutchActive.set(true);
    h.state.macroUi.activeProperty.set(
        core::state::macro::MacroPerformanceProperty::AUTOMATION
    );
    h.state.macroUi.automationManualOverrideMask.set(0x0001);

    h.press(Config::ButtonID::MACRO_1);
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
    assert(preserved->automation.pointCount == 2);
    assert(h.midiTransport.ccCount == 0);

    std::cout << "[PASS] test_macro_automation_property_button_restores_auto_without_clearing_lane\n";
}

void test_macro_button_hold_without_turn_discards_recording() {
    MacroValueHarness h;

    h.press(Config::ButtonID::MACRO_1);
    assert(!h.state.macroUi.automationRecording.active);
    g_now_ms = 1000;
    h.release(Config::ButtonID::MACRO_1);
    assert(!h.state.macroUi.automationRecording.active);

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot == nullptr);
    assert(!h.state.project.metadata.dirty);

    std::cout << "[PASS] test_macro_button_hold_without_turn_discards_recording\n";
}

void test_post_record_input_guard_survives_millisecond_rollover() {
    MacroValueHarness h;

    h.state.statusBar.tempo.set(120.0f);
    h.press(Config::ButtonID::MACRO_1);
    g_now_ms = 0xFFFF'FF00U;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    g_now_ms = 0xFFFF'FF40U;
    h.turn(Config::EncoderID::MACRO_1, 0.75f);
    g_now_ms = 0xFFFF'FF88U;
    h.release(Config::ButtonID::MACRO_1);

    const float recordedValue = h.state.macros[0].value.get();
    const int ccCountAfterRecording = h.midiTransport.ccCount;
    g_now_ms = 0xFFFF'FFF0U;
    h.turn(Config::EncoderID::MACRO_1, 0.0f);
    assert(std::fabs(h.state.macros[0].value.get() - recordedValue) < 0.0005f);
    assert(h.midiTransport.ccCount == ccCountAfterRecording);

    g_now_ms = 0x0000'0000U;
    h.turn(Config::EncoderID::MACRO_1, 0.0f);
    assert(std::fabs(h.state.macros[0].value.get()) < 0.0005f);
    assert(h.midiTransport.ccCount == ccCountAfterRecording + 1);

    std::cout << "[PASS] test_post_record_input_guard_survives_millisecond_rollover\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_macro_encoder_updates_value_and_sends_cc();
    test_macro_encoder_does_not_activate_empty_or_add_slots();
    test_macro_value_handler_respects_modal_guards();
    test_macro_encoder_feeds_armed_automation_recording();
    test_macro_button_hold_records_value_automation();
    test_turning_an_automated_macro_enters_manual_override();
    test_macro_automation_property_button_restores_auto_without_clearing_lane();
    test_macro_button_hold_without_turn_discards_recording();
    test_post_record_input_guard_survives_millisecond_rollover();

    std::cout << "\nAll MacroValueHandler tests passed.\n";
    return 0;
}

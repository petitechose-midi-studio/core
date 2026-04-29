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
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
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
};

void test_macro_encoder_updates_value_and_sends_cc() {
    MacroValueHarness h;

    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(std::fabs(h.state.macros[0].value.get() - 1.0f) < 0.0005f);
    assert(h.midiTransport.ccCount == 1);
    assert(h.midiTransport.lastChannel == 0);
    assert(h.midiTransport.lastCc == 0);
    assert(h.midiTransport.lastValue == 127);
    assert(h.state.statusBar.ccOutActive.get());

    std::cout << "[PASS] test_macro_encoder_updates_value_and_sends_cc\n";
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

    {
        MacroValueHarness h;
        h.state.macroUi.quickControlsSelecting.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(std::fabs(h.state.macros[0].value.get() - 0.5f) < 0.0005f);
        assert(h.midiTransport.ccCount == 0);
    }

    std::cout << "[PASS] test_macro_value_handler_respects_modal_guards\n";
}

}  // namespace

int main() {
    test_macro_encoder_updates_value_and_sends_cc();
    test_macro_value_handler_respects_modal_guards();

    std::cout << "\nAll MacroValueHandler tests passed.\n";
    return 0;
}

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

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

#include "../../src/sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "../../src/handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "../../src/handler/macro/MacroValueHandler.hpp"
#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/ProjectControlTestUtils.hpp"
#include "../support/ProjectTrackRuntimeSnapshotTestFixture.hpp"

namespace {

class MockMidiTransport : public oc::interface::IMidi {
public:
    using MidiOutputAcceptance = oc::interface::MidiOutputAcceptance;

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    MidiOutputAcceptance sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
        ccCount += 1;
        lastChannel = channel;
        lastCc = cc;
        lastValue = value;
        return MidiOutputAcceptance::ACCEPTED;
    }
    MidiOutputAcceptance sendNoteOn(uint8_t, uint8_t, uint8_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendNoteOff(uint8_t, uint8_t, uint8_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendSysEx(const uint8_t*, size_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendProgramChange(uint8_t, uint8_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendPitchBend(uint8_t, int16_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendChannelPressure(uint8_t, uint8_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendClock() override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendStart() override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendStop() override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendContinue() override { return MidiOutputAcceptance::ACCEPTED; }
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
    core::handler::MacroPerformanceDomainServices services;
    core::sequencer::RealtimeMidiQueue queue;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator;
    core::handler::MacroMidiCcRuntimeAdapter adapter;
    core::handler::MacroValueHandler handler;
    core::sequencer::ProjectTrackRuntimeSnapshot runtimeTracks{
        test_support::makeAllAudibleProjectTrackRuntimeSnapshot()
    };
    uint32_t deadlineUs = 0U;

    MacroValueHarness()
        : state(storages.settings)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , midi(midiTransport)
        , overlays(state.overlays, buttons)
        , services(
              core::handler::MacroPerformanceDomainServices::fromCoreState(
                  state
              )
          )
        , coordinator(queue)
        , adapter(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  state.pages,
                  state.projectTracks,
              },
              services,
              coordinator
          )
        , handler(core::handler::MacroValueHandler::StateRefs{
                      state.macroUi,
                      state.activeView,
                      state.macroEdit,
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  adapter,
                  MACRO_SCOPE) {
        state.activeView.set(core::ui::ViewType::MACRO);
        g_now_ms = 0;
        const auto& config = services.activeConfig(0U);
        const core::state::shared::MidiCcCandidate initialAuthor{
            .destination = {
                .identity = {
                    .port = core::sequencer::MidiCcGlobalFrameCoordinator::
                        OUTPUT_PORT,
                    .channel = services.activeTrackChannel(),
                    .controller = config.cc,
                },
                .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
            },
            .author = {
                .candidateClass =
                    core::state::shared::MidiCcCandidateClass::MACRO_STATIC,
                .stableAddress =
                    core::handler::MacroMidiCcRuntimeAdapter::stableAddress(
                        state.pages.currentActiveTrack(),
                        state.pages.currentActivePage(),
                        0U
                    ),
            },
            .localValue = 0U,
        };
        assert(coordinator.publishPersistentAuthors(&initialAuthor, 1U));
        assert(coordinator.resolveLive(0U, runtimeTracks).ok());
        queue.drainDue(midi, 0U, UINT32_MAX);
        // The harness starts after the authoritative playback frame has been
        // established; individual assertions count only gesture emissions.
        midiTransport.ccCount = 0;
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
        flushMidi();
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

    void flushMidi() {
        deadlineUs += 1000U;
        assert(coordinator.resolveLive(deadlineUs, runtimeTracks).ok());
        queue.drainDue(midi, deadlineUs, UINT32_MAX);
    }
};

void configureAutomation(core::state::CoreState& state, uint8_t macroIndex = 0) {
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = macroIndex,
    };
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 2.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
    assert(test_support::project_control::assignAutomation(
        state.pages.control,
        address,
        lane
    ));
}

void test_registers_no_macro_button_record_bindings() {
    MacroValueHarness h;

    assert(
        h.inputBinding.buttonBindingCount() == 0U
    );

    std::cout << "[PASS] no Macro button record bindings\n";
}

void test_macro_encoder_updates_value_and_sends_cc() {
    MacroValueHarness h;

    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(std::fabs(h.state.macros[0].value.get() - 1.0f) < 0.0005f);
    assert(h.midiTransport.ccCount == 1);
    assert(h.midiTransport.lastChannel == 0);
    assert(h.midiTransport.lastCc == 0);
    assert(h.midiTransport.lastValue == 127);
    assert(std::fabs(h.state.pages.activePageData().values[0] - 1.0f) < 0.0005f);
    assert(h.state.hasPendingProjectMutationCoalescing());

    std::cout << "[PASS] test_macro_encoder_updates_value_and_sends_cc\n";
}

void test_macro_encoder_sanitizes_non_finite_values_before_midi_conversion() {
    const float invalidValues[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    for (const float invalid : invalidValues) {
        MacroValueHarness h;

        h.turn(Config::EncoderID::MACRO_1, invalid);

        assert(std::isfinite(h.state.macros[0].value.get()));
        assert(std::fabs(h.state.macros[0].value.get()) < 0.0005f);
        assert(std::isfinite(h.state.pages.activePageData().values[0]));
        assert(std::fabs(h.state.pages.activePageData().values[0]) < 0.0005f);
        assert(h.midiTransport.ccCount == 0);
        assert(h.midiTransport.lastValue == 0);

        core::state::macro::MacroWorkflow::setRuntimeValue(h.state.macros, 0, invalid);
        assert(std::isfinite(h.state.macros[0].value.get()));
        assert(std::fabs(h.state.macros[0].value.get()) < 0.0005f);
    }

    std::cout << "[PASS] test_macro_encoder_sanitizes_non_finite_values_before_midi_conversion\n";
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
        h.state.overlays.show(core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);
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
    assert(services.armAutomationTake());

    g_now_ms = 500;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(services.releaseAutomationTake(1000U));

    const auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    );
    assert(slot.automation.enabled);
    assert(slot.automation.pointCount == 2);
    const auto firstPoint = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        slot.automation.id,
        0,
        false
    );
    const auto secondPoint = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        slot.automation.id,
        1,
        false
    );
    // The first physical movement defines t0, so its authored value is the
    // first value of the first-joined Macro (late joiners retain their Base).
    assert(std::fabs(firstPoint.value - 1.0f) < 0.0001f);
    assert(std::fabs(secondPoint.beat - 1.0f) < 0.0001f);
    assert(std::fabs(secondPoint.value - 1.0f) < 0.0001f);

    std::cout << "[PASS] test_macro_encoder_feeds_armed_automation_recording\n";
}

void test_macro_button_hold_cannot_record_and_turn_stays_manual() {
    MacroValueHarness h;

    h.state.statusBar.tempo.set(120.0f);
    h.press(Config::ButtonID::MACRO_1);
    assert(!h.services.automationTakeArmed());

    g_now_ms = 500;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(!h.services.automationTakeRecording());

    g_now_ms = 1000;
    h.release(Config::ButtonID::MACRO_1);
    assert(!h.services.automationTakeArmed());

    const auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    );
    assert(!slot.automation.stored());
    assert(std::fabs(h.state.pages.activePageData().values[0] - 0.5f) < 0.0001f);

    h.turn(Config::EncoderID::MACRO_1, 0.0f);
    assert(std::fabs(h.state.macros[0].value.get()) < 0.0005f);

    std::cout << "[PASS] Macro button hold cannot record\n";
}

void test_recording_cadence_preserves_plateau_before_later_motion() {
    MacroValueHarness h;
    h.state.statusBar.tempo.set(120.0f);

    assert(h.services.armAutomationTake());
    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 0.2f);
    assert(h.services.automationTakeRecording());

    // No encoder event occurs during this hold. The shared 16 ms sampler must
    // still author a stationary anchor before the next movement.
    h.handler.update(400);
    g_now_ms = 500;
    h.turn(Config::EncoderID::MACRO_1, 0.8f);
    g_now_ms = 600;
    assert(h.services.releaseAutomationTake(600U));

    const auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    );
    assert(slot.automation.pointCount >= 3U);
    const float held = core::state::modulation::evaluateProjectControlCurve(
        h.state.pages.control,
        slot.automation.id,
        0.3f,
        0.5f
    );
    const float afterMotion = core::state::modulation::evaluateProjectControlCurve(
        h.state.pages.control,
        slot.automation.id,
        0.9f,
        0.5f
    );
    assert(held < 0.3f);
    assert(afterMotion > 0.7f);

    std::cout
        << "[PASS] recording cadence preserves hold before later motion\n";
}

void test_turning_an_automated_macro_enters_manual_override() {
    MacroValueHarness h;

    configureAutomation(h.state);
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert((h.state.macroUi.automationManualOverrideMask.get() & 0x0001) != 0);
    assert(h.state.macroUi.manualOverrides.activeFor(
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    ));
    assert(std::fabs(h.state.pages.activePageData().values[0] - 1.0f) < 0.0001f);
    assert(h.state.hasPendingProjectMutationCoalescing());
    assert(h.midiTransport.ccCount == 1);
    assert(h.midiTransport.lastValue == 127);

    h.turn(Config::EncoderID::MACRO_1, 0.25f);
    float overrideValue = 0.0f;
    assert(h.state.macroUi.manualOverrides.valueFor(
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        },
        overrideValue
    ));
    const float quantized = core::midi::fromCC(core::midi::toCC(0.25f));
    assert(std::fabs(overrideValue - quantized) < 0.0001f);
    assert(std::fabs(h.state.pages.activePageData().values[0] - quantized) < 0.0001f);
    assert(h.midiTransport.ccCount == 2);

    std::cout << "[PASS] test_turning_an_automated_macro_enters_manual_override\n";
}

void test_macro_automation_property_button_restores_auto_without_clearing_lane() {
    MacroValueHarness h;

    configureAutomation(h.state);

    h.state.macroUi.clutchActive.set(true);
    h.state.macroUi.activeProperty.set(
        core::state::macro::MacroPerformanceProperty::AUTOMATION
    );
    auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(h.state);
    assert(services.takeManualControl(0, 0.7f));

    h.press(Config::ButtonID::MACRO_1);
    assert((h.state.macroUi.automationManualOverrideMask.get() & 0x0001) == 0);

    const auto preserved = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    );
    assert(preserved.automation.enabled);
    assert(preserved.automation.pointCount == 2);
    assert(h.midiTransport.ccCount == 0);

    std::cout << "[PASS] test_macro_automation_property_button_restores_auto_without_clearing_lane\n";
}

void test_macro_button_hold_without_turn_discards_recording() {
    MacroValueHarness h;

    h.press(Config::ButtonID::MACRO_1);
    assert(!h.services.automationTakeArmed());
    g_now_ms = 1000;
    h.release(Config::ButtonID::MACRO_1);
    assert(!h.services.automationTakeRecording());

    const auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    );
    assert(!slot.present());
    assert(!h.state.project.metadata.dirty);

    std::cout << "[PASS] test_macro_button_hold_without_turn_discards_recording\n";
}

void test_post_record_input_guard_survives_millisecond_rollover() {
    MacroValueHarness h;

    h.state.statusBar.tempo.set(120.0f);
    assert(h.services.setAutomationTakeTiming(
        core::state::macro::MacroAutomationTakeTiming::NOTE_1_4
    ));
    assert(h.services.armAutomationTake());
    g_now_ms = 0xFFFF'FF00U;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    g_now_ms = 0xFFFF'FF40U;
    h.turn(Config::EncoderID::MACRO_1, 0.75f);
    g_now_ms = 0xFFFF'FF88U;
    assert(h.services.releaseAutomationTake(g_now_ms));

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
    test_registers_no_macro_button_record_bindings();
    test_macro_encoder_updates_value_and_sends_cc();
    test_macro_encoder_sanitizes_non_finite_values_before_midi_conversion();
    test_macro_encoder_does_not_activate_empty_or_add_slots();
    test_macro_value_handler_respects_modal_guards();
    test_macro_encoder_feeds_armed_automation_recording();
    test_macro_button_hold_cannot_record_and_turn_stays_manual();
    test_recording_cadence_preserves_plateau_before_later_motion();
    test_turning_an_automated_macro_enters_manual_override();
    test_macro_button_hold_without_turn_discards_recording();

    std::cout << "\nAll MacroValueHandler tests passed.\n";
    return 0;
}

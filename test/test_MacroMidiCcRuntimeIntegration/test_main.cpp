#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
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
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Result.hpp>

#include "handler/common/MidiCcRuntimeAggregator.hpp"
#include "handler/macro/MacroAutomationPlaybackService.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroValueHandler.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "support/InputTestHardware.hpp"

namespace {

class MockMidiTransport final : public oc::interface::IMidi {
public:
    struct CcMessage {
        uint8_t channel = 0;
        uint8_t controller = 0;
        uint8_t value = 0;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
        assert(count < messages.size());
        messages[count++] = CcMessage{channel, cc, value};
    }
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void sendSysEx(const uint8_t*, std::size_t) override {}
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

    std::array<CcMessage, 16> messages{};
    uint8_t count = 0;
};

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

struct Harness {
    static constexpr oc::type::ScopeID MACRO_SCOPE = 812;

    core::state::MacroState macros;
    core::state::macro::MacroPagesState pages;
    core::state::macro::MacroUiState macroUi;
    core::state::MacroEditState macroEdit;
    core::state::StatusBarState statusBar;
    oc::state::Signal<core::ui::ViewType, 8> activeView{core::ui::ViewType::MACRO};
    oc::state::Signal<uint32_t> configRevision{0};
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlayState;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestButtonHardware buttonHardware;
    test_support::TestEncoderHardware encoderHardware;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    MockMidiTransport transport;
    oc::api::MidiAPI midi;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroPerformanceDomainServices services;
    core::handler::MidiCcRuntimeAggregator aggregator;
    core::handler::MacroMidiCcRuntimeAdapter adapter;
    core::handler::MacroValueHandler valueHandler;
    core::handler::MacroAutomationPlaybackService playback;

    Harness()
        : inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHardware)
        , encoders(inputBinding, encoderHardware)
        , midi(transport)
        , overlays(overlayState, buttons)
        , services(
              core::handler::MacroPerformanceDomainServices::StateRefs{
                  macros,
                  pages,
                  macroUi,
                  configRevision,
                  statusBar,
              },
              core::handler::MacroPerformanceDomainServices::Operations{}
          )
        , aggregator(midi)
        , adapter(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  pages,
                  macroUi,
              },
              services,
              aggregator
          )
        , valueHandler(
              core::handler::MacroValueHandler::StateRefs{
                  macroUi,
                  activeView,
                  macroEdit,
              },
              services,
              overlays,
              encoders,
              buttons,
              adapter,
              MACRO_SCOPE
          )
        , playback(
              core::handler::MacroAutomationPlaybackService::StateRefs{
                  pages,
                  macroUi,
                  statusBar,
              },
              services,
              adapter
          ) {
        pages.setMacroSlotActive(0, true);
        pages.setMacroSlotActive(1, true);
        pages.activePageData().cc[0] = 74;
        pages.activePageData().cc[1] = 74;
        pages.updateActiveConfigs();
        configureAutomation(0);
        configureAutomation(1);
        statusBar.tempo.set(60.0f);
        statusBar.playing.set(true);
    }

    void configureAutomation(uint8_t macroIndex) {
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = pages.currentActiveTrack(),
                .page = pages.currentActivePage(),
                .macro = macroIndex,
            }
        );
        assert(slot != nullptr);
        core::state::macro::MacroAutomationLane lane;
        assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
        assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
        assert(core::state::macro::macroAutomationAssignAutomation(
            pages.automation,
            *slot,
            lane
        ));
    }

    void turn(uint8_t macroIndex, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(
            Config::MACRO_ENCODERS[macroIndex]
        );
        encoderHardware.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

void test_manual_and_playback_share_one_resolved_destination_cache() {
    Harness h;

    h.playback.update(1000);
    assert(h.transport.count == 1);
    assert(h.transport.messages[0].channel == 0);
    assert(h.transport.messages[0].controller == 74);
    assert(h.transport.messages[0].value == 0);

    g_now_ms = 1100;
    h.turn(1, 1.0f);
    assert(h.transport.count == 2);
    assert(h.transport.messages[1].value == 127);
    assert((h.macroUi.automationManualOverrideMask.get() & 0x0002) != 0);

    const auto& manualTelemetry = h.adapter.telemetry();
    assert(manualTelemetry.destinationCount == 1);
    assert(manualTelemetry.destinations[0].winner.author.candidateClass ==
           core::state::shared::MidiCcCandidateClass::LIVE_MANUAL);
    assert(manualTelemetry.destinations[0].winner.author.stableAddress == 1);
    assert(manualTelemetry.destinations[0].conflict);

    // Playback evaluates both automation lanes at the next 16 ms frame, but
    // Manual remains the final 127. The shared cache prevents a second send.
    h.playback.update(1500);
    assert(h.transport.count == 2);
    const auto& playbackTelemetry = h.adapter.telemetry();
    assert(playbackTelemetry.candidateCount == 3);
    assert(playbackTelemetry.destinations[0].winner.author.candidateClass ==
           core::state::shared::MidiCcCandidateClass::LIVE_MANUAL);
    assert(playbackTelemetry.destinations[0].finalValue == 127);

    h.services.setAutomationManualOverride(1, false);
    h.playback.update(3000);
    assert(h.transport.count == 3);
    assert(h.transport.messages[2].value == 0);
    assert(h.adapter.telemetry().destinations[0].winner.author.candidateClass ==
           core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED);
    assert(h.adapter.telemetry().destinations[0].winner.author.stableAddress == 0);

    std::cout << "[PASS] test_manual_and_playback_share_one_resolved_destination_cache\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_manual_and_playback_share_one_resolved_destination_cache();
    std::cout << "\nAll MacroMidiCcRuntimeIntegration tests passed.\n";
    return 0;
}

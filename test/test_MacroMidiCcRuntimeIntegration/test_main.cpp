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

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "handler/macro/MacroAutomationPlaybackService.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroValueHandler.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "support/InputTestHardware.hpp"
#include "support/ProjectControlTestUtils.hpp"

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
    core::sequencer::RealtimeMidiQueue queue;
    core::handler::MidiCcGlobalFrameCoordinator coordinator;
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
        , coordinator(queue)
        , adapter(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  pages,
                  macroUi,
              },
              services,
              coordinator
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

    core::handler::MidiCcGlobalFrameResult resolveAndDrain(
        uint32_t deadlineUs
    ) {
        const auto result = coordinator.resolveLive(deadlineUs);
        queue.drainDue(midi, deadlineUs, UINT32_MAX);
        return result;
    }

    void configureAutomation(uint8_t macroIndex) {
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = pages.currentActiveTrack(),
            .page = pages.currentActivePage(),
            .macro = macroIndex,
        };
        core::state::macro::MacroAutomationLane lane;
        assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
        assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
        assert(test_support::project_control::assignAutomation(
            pages.control,
            address,
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

    h.coordinator.publishProjectControlClock(0, true, 1000000, 41667);
    h.playback.update(1000);
    assert(h.resolveAndDrain(1000).ok());
    assert(h.transport.count == 1);
    assert(h.transport.messages[0].channel == 0);
    assert(h.transport.messages[0].controller == 74);
    assert(h.transport.messages[0].value == 0);

    g_now_ms = 1100;
    h.turn(1, 1.0f);
    assert(h.resolveAndDrain(1100).ok());
    assert(h.transport.count == 2);
    assert(h.transport.messages[1].value == 127);
    assert((h.macroUi.automationManualOverrideMask.get() & 0x0002) != 0);

    {
        auto telemetry = h.coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinationCount == 1);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               core::state::shared::MidiCcCandidateClass::LIVE_MANUAL);
        assert(telemetry->destinations[0].winner.author.stableAddress == 1);
        assert(telemetry->destinations[0].conflict);
    }

    // Playback evaluates both lanes at the next 16 ms frame. The manual lane
    // stays a single Live author carrying its resolved Base + Modulation Out,
    // and the shared cache prevents a second send.
    h.coordinator.publishProjectControlClock(12, true, 1500000, 41667);
    h.playback.update(1500);
    assert(h.resolveAndDrain(1500).ok());
    assert(h.transport.count == 2);
    {
        auto telemetry = h.coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->candidateCount == 2);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               core::state::shared::MidiCcCandidateClass::LIVE_MANUAL);
        assert(telemetry->destinations[0].finalValue == 127);
    }

    h.statusBar.playing.set(false);
    h.coordinator.publishProjectControlClock(12, false, 2000000, 41667);
    h.playback.update(2000);
    assert(h.resolveAndDrain(2000).ok());
    assert(h.transport.count == 2);
    assert(h.services.manualOverrideActiveFor(1));

    h.statusBar.playing.set(true);
    h.coordinator.publishProjectControlClock(12, true, 2200000, 41667);
    h.playback.update(2200);
    assert(h.resolveAndDrain(2200).ok());
    assert(h.transport.count == 2);
    assert(h.services.manualOverrideActiveFor(1));
    {
        auto telemetry = h.coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               core::state::shared::MidiCcCandidateClass::LIVE_MANUAL);
    }

    assert(h.services.resumeComputedSources(1));
    h.coordinator.publishProjectControlClock(60, true, 4200000, 41667);
    h.playback.update(4200);
    assert(h.resolveAndDrain(4200).ok());
    assert(h.transport.count == 3);
    // Stop/start preserves the 500 ms phase accumulated before the stop; the
    // authoritative clock then advances two full beats while Manual owns the
    // destination, so Automation resumes at the same midpoint.
    assert(h.transport.messages[2].value == 64);
    {
        auto telemetry = h.coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].finalValue == 64);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED);
        assert(telemetry->destinations[0].winner.author.stableAddress == 0);
    }

    std::cout << "[PASS] test_manual_and_playback_share_one_resolved_destination_cache\n";
}

void test_dispatched_note_edges_bypass_frame_throttle_and_drive_adsr() {
    namespace mod = core::state::modulation;
    Harness h;
    h.pages.control.clear();
    h.pages.setMacroSlotActive(1U, false);
    h.pages.activePageData().values[0] = 0.5f;

    mod::ModulatorAdsrDraft source{};
    source.name = "Gate";
    source.reach.kind = mod::ModulatorReachKind::PROJECT;
    source.parameters.attack = 0U;
    source.parameters.decay = 0U;
    source.parameters.sustainQ15 = 8192U;
    source.parameters.release = 0U;
    source.parameters.curve = mod::ModulatorAdsrCurve::LINEAR;
    const auto created = mod::createAdsrModulator(
        h.pages.control.authored.modulation,
        source
    );
    assert(created.changed());

    mod::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = {
        mod::ModulationDestinationKind::MACRO_SLOT,
        0U,
        0U,
        0U,
    };
    binding.amountQ15 = 32767;
    binding.application = mod::ModulationApplication::NATURAL;
    assert(mod::addProjectModulationBinding(
        h.pages.control.authored.modulation,
        binding
    ).changed());

    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = created.sourceId;
    trigger.trigger = {
        mod::ModulationTriggerKind::TRACK_NOTE,
        0U,
        mod::PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        mod::PROJECT_MODULATION_TRIGGER_ANY_NOTE,
    };
    assert(mod::addProjectModulationTrigger(
        h.pages.control.authored.modulation,
        trigger
    ).changed());
    h.pages.control.markAuthoredMutation();

    g_now_ms = 1000U;
    h.coordinator.publishProjectControlClock(0U, true, 1000000U, 41667U);
    h.playback.update(g_now_ms);
    assert(h.resolveAndDrain(1000000U).ok());
    assert(h.transport.count == 1U);
    assert(h.transport.messages[0].value == 64U);

    const core::sequencer::RealtimeMidiEvent noteOn{
        .deadlineUs = 1001000U,
        .type = core::sequencer::RealtimeMidiEventType::NoteOn,
        .channel = 9U,
        .note = 67U,
        .velocity = 111U,
        .trackIndex = 0U,
    };
    assert(h.queue.push(noteOn));
    g_now_ms = 1001U;
    h.queue.drainDue(h.midi, noteOn.deadlineUs, UINT32_MAX);
    assert(h.coordinator.hasPendingProjectModulationTriggers());
    h.coordinator.publishProjectControlClock(
        0U,
        true,
        noteOn.deadlineUs,
        41667U
    );
    h.playback.update(g_now_ms);
    assert(!h.coordinator.hasPendingProjectModulationTriggers());
    assert(h.resolveAndDrain(noteOn.deadlineUs).ok());
    assert(h.transport.count == 2U);
    assert(h.transport.messages[1].value == 95U);
    assert(h.pages.control.runtime.sources[0].payload.adsr.heldNoteCount == 1U);

    const core::sequencer::RealtimeMidiEvent noteOff{
        .deadlineUs = 1002000U,
        .type = core::sequencer::RealtimeMidiEventType::NoteOff,
        .channel = 9U,
        .note = 67U,
        .velocity = 0U,
        .trackIndex = 0U,
    };
    assert(h.queue.push(noteOff));
    g_now_ms = 1002U;
    h.queue.drainDue(h.midi, noteOff.deadlineUs, UINT32_MAX);
    h.coordinator.publishProjectControlClock(
        0U,
        true,
        noteOff.deadlineUs,
        41667U
    );
    h.playback.update(g_now_ms);
    assert(h.resolveAndDrain(noteOff.deadlineUs).ok());
    assert(h.transport.count == 3U);
    assert(h.transport.messages[2].value == 64U);
    assert(h.pages.control.runtime.sources[0].payload.adsr.heldNoteCount == 0U);

    std::cout << "[PASS] dispatched Note edges drive ADSR without 16 ms latency\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_manual_and_playback_share_one_resolved_destination_cache();
    test_dispatched_note_edges_bypass_frame_throttle_and_drive_adsr();
    std::cout << "\nAll MacroMidiCcRuntimeIntegration tests passed.\n";
    return 0;
}

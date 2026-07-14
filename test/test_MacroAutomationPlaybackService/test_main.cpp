#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/type/Result.hpp>

#include "../../src/handler/macro/MacroAutomationPlaybackService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"

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

void configureAutomation(core::state::CoreState& state) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
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

void configureModulation(core::state::CoreState& state, float depth) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    core::state::macro::MacroModulationShape shape;
    shape.durationBeats = 2.0f;
    assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, 0.25f));
    assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, -0.25f));
    assert(core::state::macro::macroAutomationAssignModulation(
        state.pages.automation,
        *slot,
        shape
    ));
    slot->modulationDepth = depth;
}

void test_playback_updates_runtime_and_sends_cc_when_value_changes() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    state.pages.activePageData().cc[0] = 74;
    state.pages.updateActiveConfigs();
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    playback.update(1000);
    assert(midiTransport.ccCount == 1);
    assert(midiTransport.lastChannel == 0);
    assert(midiTransport.lastCc == 74);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    playback.update(1008);
    assert(midiTransport.ccCount == 1);

    playback.update(1500);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);
    assert(state.macros[0].value.get() > 0.49f && state.macros[0].value.get() < 0.51f);
    assert(!state.hasPendingProjectMutationCoalescing());

    std::cout << "[PASS] test_playback_updates_runtime_and_sends_cc_when_value_changes\n";
}

void test_playback_stops_when_transport_is_stopped() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    state.statusBar.playing.set(false);
    playback.update(1000);
    playback.update(1500);
    assert(midiTransport.ccCount == 0);

    std::cout << "[PASS] test_playback_stops_when_transport_is_stopped\n";
}

void test_update_period_remains_bounded_across_millisecond_rollover() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    playback.update(0xFFFF'FFF0U);
    assert(midiTransport.ccCount == 1);

    playback.update(0xFFFF'FFF8U);
    assert(midiTransport.ccCount == 1);

    playback.update(0x0000'0000U);
    assert(midiTransport.ccCount == 2);

    std::cout << "[PASS] test_update_period_remains_bounded_across_millisecond_rollover\n";
}

void test_manual_override_suspends_playback_for_the_macro_slot() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        services,
        midi
    );

    playback.update(1000);
    assert(midiTransport.ccCount == 1);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    assert(services.takeManualControl(0, 0.42f));
    playback.update(1500);
    assert(midiTransport.ccCount == 1);
    assert(std::fabs(state.macros[0].value.get() - 0.42f) < 0.0001f);

    // At beat 2 the lane wraps to its initial value. Restoring automation
    // must still resend it because manual input superseded the prior output.
    assert(services.resumeComputedSources(0));
    playback.update(3000);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    std::cout << "[PASS] test_manual_override_suspends_playback_for_the_macro_slot\n";
}

void test_modulation_only_playback_and_depth_zero_remain_computed() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureModulation(state, 0.4f);
    state.pages.activePageData().values[0] = 0.5f;
    state.pages.activePageData().cc[0] = 74;
    state.pages.updateActiveConfigs();
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        services,
        midi
    );

    assert(!services.automationActiveFor(0));
    assert(services.computedSourcePlaybackActiveFor(0));
    playback.update(1000);
    assert(midiTransport.ccCount == 1);
    assert(midiTransport.lastValue >= 75 && midiTransport.lastValue <= 77);

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        address
    );
    assert(slot != nullptr);
    const uint16_t pointCount = slot->modulation.pointCount;
    slot->modulationDepth = 0.0f;
    playback.update(1500);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);
    assert(services.computedSourcePlaybackActiveFor(0));
    assert(slot->modulation.pointCount == pointCount);

    slot->modulationDepth = 0.4f;
    playback.update(2000);
    assert(midiTransport.ccCount == 3);
    assert(midiTransport.lastValue >= 50 && midiTransport.lastValue <= 52);
    assert(slot->modulation.pointCount == pointCount);

    std::cout << "[PASS] test_modulation_only_playback_and_depth_zero_remain_computed\n";
}

void test_recording_session_suspends_existing_lane_playback_for_the_macro_slot() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    playback.update(1000);
    assert(midiTransport.ccCount == 1);

    state.macros[0].value.set(0.42f);
    state.macroUi.automationRecording.active = true;
    state.macroUi.automationRecording.address = {
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };

    playback.update(1500);
    assert(midiTransport.ccCount == 1);
    assert(std::fabs(state.macros[0].value.get() - 0.42f) < 0.0001f);

    state.macroUi.automationRecording.active = false;
    playback.update(3000);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    std::cout << "[PASS] test_recording_session_suspends_existing_lane_playback_for_the_macro_slot\n";
}

void test_reactivating_slot_or_lane_resends_value_superseded_while_inactive() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    playback.update(1000);
    assert(midiTransport.ccCount == 1);
    assert(midiTransport.lastValue == 0);

    state.pages.setMacroSlotActive(0, false);
    state.macros[0].value.set(0.42f);
    playback.update(1500);
    assert(midiTransport.ccCount == 1);

    // At beat 2 the lane resolves to the same value that was sent before the
    // slot was disabled. Reactivation must still reclaim runtime and MIDI.
    state.pages.setMacroSlotActive(0, true);
    playback.update(3000);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    auto* slot =
        core::state::macro::macroAutomationFindMutableSlot(state.pages.automation, address);
    assert(slot != nullptr);
    slot->automation.active = false;
    state.macros[0].value.set(0.42f);
    playback.update(3500);
    assert(midiTransport.ccCount == 2);

    // The same ownership guarantee applies when only the automation lane is
    // disabled while the macro slot remains active.
    slot->automation.active = true;
    playback.update(5000);
    assert(midiTransport.ccCount == 3);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    std::cout << "[PASS] test_reactivating_slot_or_lane_resends_value_superseded_while_inactive\n";
}

}  // namespace

int main() {
    test_playback_updates_runtime_and_sends_cc_when_value_changes();
    test_playback_stops_when_transport_is_stopped();
    test_update_period_remains_bounded_across_millisecond_rollover();
    test_manual_override_suspends_playback_for_the_macro_slot();
    test_modulation_only_playback_and_depth_zero_remain_computed();
    test_recording_session_suspends_existing_lane_playback_for_the_macro_slot();
    test_reactivating_slot_or_lane_resends_value_superseded_while_inactive();

    std::cout << "\nAll MacroAutomationPlaybackService tests passed.\n";
    return 0;
}

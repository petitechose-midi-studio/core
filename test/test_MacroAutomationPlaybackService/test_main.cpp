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
#include "../support/ProjectControlTestUtils.hpp"

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
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
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

void configureModulationAt(core::state::CoreState& state,
                           uint8_t track,
                           uint8_t page,
                           uint8_t macro,
                           float depth) {
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = track,
        .page = page,
        .macro = macro,
    };
    core::state::macro::MacroModulationShape shape;
    shape.durationBeats = 2.0f;
    assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, 0.25f));
    assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, -0.25f));
    assert(test_support::project_control::assignModulation(
        state.pages.control,
        address,
        shape,
        depth
    ));
}

void configureModulation(core::state::CoreState& state, float depth) {
    configureModulationAt(
        state,
        state.pages.currentActiveTrack(),
        state.pages.currentActivePage(),
        0,
        depth
    );
}

void test_runtime_projection_publication_is_atomic() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    auto configurePage = [&](uint8_t track,
                             uint8_t page,
                             float first,
                             float second) {
        auto& target = state.pages.tracks[track].pages[page];
        target.values[0] = first;
        target.values[1] = second;
        target.setMacroActive(0, true);
        target.setMacroActive(1, true);
        state.pages.tracks[track].setPageEnabled(page, true);
        configureModulationAt(state, track, page, 0, 0.4f);
        configureModulationAt(state, track, page, 1, 0.3f);
    };
    configurePage(0, 0, 0.35f, 0.65f);
    configurePage(0, 1, 0.2f, 0.8f);
    configurePage(1, 0, 0.45f, 0.55f);
    state.pages.syncSharedTrackState(0x0003U, 0);
    state.pages.updateActiveConfigs();

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
            &state.macroRuntimeOwnerRevision,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    const uint32_t revisionBefore =
        state.macroUi.runtimeProjectionRevision.get();
    playback.update(1000);
    const uint32_t revisionAfter =
        state.macroUi.runtimeProjectionRevision.get();
    assert((revisionAfter >> 8U) == (revisionBefore >> 8U) + 1U);
    assert(core::state::macro::macroRuntimeProjectionRevisionTargetsAll(
        revisionAfter
    ));
    assert(state.macroUi.runtimeProjections[0].valid);
    assert(state.macroUi.runtimeProjections[1].valid);
    assert(state.macroUi.runtimeProjectionValidFor(0, 0, 0));
    assert(state.macroUi.runtimeProjectionValidFor(0, 0, 1));

    // A Project activation is a hard runtime boundary and must bypass the
    // 16 ms cadence guard rather than leaving a cleared/intermediate frame.
    state.requestMacroRuntimeOwnerActivation();
    playback.update(1001);
    assert(state.pages.control.runtime.lastEvaluationMs == 1001U);
    assert(state.macroUi.runtimeProjectionValidFor(0, 0, 0));
    assert(state.macroUi.runtimeProjectionValidFor(0, 0, 1));

    // Page and Track navigation inside the same cadence window also publish
    // their target tuple immediately; navigation is never a refresh gesture.
    state.pages.setActivePage(1);
    playback.update(1002);
    assert(state.macroUi.runtimeProjectionValidFor(0, 1, 0));
    assert(state.macroUi.runtimeProjectionValidFor(0, 1, 1));
    assert(!state.macroUi.runtimeProjectionValidFor(0, 0, 0));

    state.pages.syncSharedTrackState(0x0003U, 1);
    playback.update(1003);
    assert(state.macroUi.runtimeProjectionValidFor(1, 0, 0));
    assert(state.macroUi.runtimeProjectionValidFor(1, 0, 1));
    assert(!state.macroUi.runtimeProjectionValidFor(0, 1, 0));

    std::cout << "[PASS] runtime projection publishes complete frames only\n";
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

void test_stopped_transport_publishes_static_owner_without_playing_curve() {
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
    assert(midiTransport.ccCount == 1);
    assert(midiTransport.lastValue == 0);

    std::cout
        << "[PASS] stopped transport publishes Static owner without curve motion\n";
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

void test_manual_override_replaces_automation_base_until_resume() {
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
    assert(midiTransport.ccCount == 2);
    assert(std::fabs(state.macros[0].value.get() - 0.42f) < 0.0001f);

    // At beat 2 the lane wraps to its initial value. Restoring automation
    // must still resend it because manual input superseded the prior output.
    assert(services.resumeComputedSources(0));
    playback.update(3000);
    assert(midiTransport.ccCount == 3);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    std::cout << "[PASS] test_manual_override_replaces_automation_base_until_resume\n";
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
    auto slot = test_support::project_control::readSlot(
        state.pages.control,
        address
    );
    const uint16_t pointCount = slot.legacy.modulation.pointCount;
    assert(core::state::modulation::setProjectControlModulationAmount(
        state.pages.control,
        address,
        0.0f
    ));
    playback.update(1500);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);
    assert(services.computedSourcePlaybackActiveFor(0));
    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(slot.legacy.modulation.pointCount == pointCount);

    assert(core::state::modulation::setProjectControlModulationAmount(
        state.pages.control,
        address,
        0.4f
    ));
    playback.update(2000);
    assert(midiTransport.ccCount == 3);
    assert(midiTransport.lastValue >= 50 && midiTransport.lastValue <= 52);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(slot.legacy.modulation.pointCount == pointCount);

    std::cout << "[PASS] test_modulation_only_playback_and_depth_zero_remain_computed\n";
}

void test_recording_keeps_modulation_audible_and_active_after_commit() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    configureModulation(state, 1.0f);
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
    assert(midiTransport.lastValue >= 31 && midiTransport.lastValue <= 32);

    assert(services.beginAutomationRecording(0, 1100));
    assert(services.recordAutomationPoint(0, 1200, 0.4f));
    playback.update(1250);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue >= 66 && midiTransport.lastValue <= 67);
    const auto projection = state.macroUi.runtimeProjections[0];
    assert(projection.valid && projection.modulationActive);
    assert(std::fabs(projection.base - 0.4f) < 0.0001f);
    assert(std::fabs(projection.modulation - 0.125f) < 0.01f);

    assert(services.recordAutomationPoint(0, 1600, 0.6f));
    assert(services.commitAutomationRecording(2000));
    const auto slot = test_support::project_control::readSlot(
        state.pages.control,
        {state.pages.currentActiveTrack(), state.pages.currentActivePage(), 0}
    );
    assert(core::state::macro::macroCurvePlaybackActive(slot.legacy.modulation));
    assert(slot.legacy.modulation.playbackState ==
           core::state::macro::MacroCurvePlaybackState::ACTIVE);

    std::cout << "[PASS] recording captures raw Base while Modulation stays audible\n";
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
    assert(core::state::modulation::setProjectControlAutomationEnabled(
        state.pages.control,
        address,
        false
    ));
    state.macros[0].value.set(0.42f);
    playback.update(3500);
    assert(midiTransport.ccCount == 3);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);
    assert(state.macros[0].value.get() > 0.49f &&
           state.macros[0].value.get() < 0.51f);

    // Disabling Automation transfers ownership to the authored Static Base;
    // re-enabling it must explicitly reclaim ownership for the curve.
    assert(core::state::modulation::setProjectControlAutomationEnabled(
        state.pages.control,
        address,
        true
    ));
    playback.update(5000);
    assert(midiTransport.ccCount == 4);
    assert(midiTransport.lastValue == 0);
    assert(std::fabs(state.macros[0].value.get() - 0.0f) < 0.0001f);

    std::cout << "[PASS] test_reactivating_slot_or_lane_resends_value_superseded_while_inactive\n";
}

void test_runtime_owner_epoch_is_independent_from_navigation_and_transport() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureModulation(state, 1.0f);
    state.pages.activePageData().values[0] = 0.5f;
    state.pages.activePageData().cc[0] = 74;
    state.pages.updateActiveConfigs();
    // Keep Track 1 intentionally silent: this test isolates navigation from
    // Project-author topology changes, which are covered by the integration
    // frame tests.
    state.pages.tracks[1].pages[0].setMacroActive(0, false);
    state.pages.syncSharedTrackState(0x0003U, 0);
    state.statusBar.tempo.set(60.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
            &state.macroRuntimeOwnerRevision,
        },
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi
    );

    playback.update(1000);
    assert(midiTransport.lastValue >= 95 && midiTransport.lastValue <= 96);
    playback.update(1500);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);

    const uint32_t stableOwnerRevision = state.macroRuntimeOwnerRevision.get();
    state.macroUi.focusedMacroSlot.set(1);
    playback.update(1750);
    assert(midiTransport.lastValue >= 47 && midiTransport.lastValue <= 48);
    assert(state.macroRuntimeOwnerRevision.get() == stableOwnerRevision);

    const int beforePageNavigation = midiTransport.ccCount;
    state.pages.setActivePage(1);
    playback.update(2000);
    assert(midiTransport.ccCount == beforePageNavigation);
    state.pages.setActivePage(0);
    playback.update(2250);
    assert(midiTransport.lastValue >= 31 && midiTransport.lastValue <= 32);
    assert(state.macroRuntimeOwnerRevision.get() == stableOwnerRevision);

    const int beforeTrackNavigation = midiTransport.ccCount;
    state.pages.syncSharedTrackState(0x0003U, 1);
    playback.update(2500);
    assert(midiTransport.ccCount == beforeTrackNavigation);
    state.pages.syncSharedTrackState(0x0003U, 0);
    playback.update(2750);
    assert(midiTransport.lastValue >= 31 && midiTransport.lastValue <= 32);
    assert(state.macroRuntimeOwnerRevision.get() == stableOwnerRevision);

    state.statusBar.playing.set(false);
    const int beforeStop = midiTransport.ccCount;
    playback.update(3000);
    playback.update(3500);
    assert(midiTransport.ccCount == beforeStop);

    state.statusBar.playing.set(true);
    playback.update(4000);
    assert(midiTransport.ccCount == beforeStop + 1);
    assert(midiTransport.lastValue >= 31 && midiTransport.lastValue <= 32);
    playback.update(4750);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);
    assert(state.macroRuntimeOwnerRevision.get() == stableOwnerRevision);

    state.requestMacroRuntimeOwnerActivation();
    assert(state.macroRuntimeOwnerRevision.get() == stableOwnerRevision + 1U);
    playback.update(5000);
    assert(midiTransport.lastValue >= 95 && midiTransport.lastValue <= 96);
    playback.update(5500);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);

    state.requestMacroRuntimeOwnerActivation();
    playback.reset();
    playback.update(6000);
    assert(midiTransport.lastValue >= 95 && midiTransport.lastValue <= 96);
    playback.update(6500);
    assert(midiTransport.lastValue >= 63 && midiTransport.lastValue <= 64);

    std::cout << "[PASS] test_runtime_owner_epoch_is_independent_from_navigation_and_transport\n";
}

void test_runtime_owner_activation_preserves_manual_ownership() {
    test_support::CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    configureAutomation(state);
    configureModulation(state, 1.0f);
    state.statusBar.playing.set(true);

    MockMidiTransport midiTransport;
    oc::api::MidiAPI midi(midiTransport);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    core::handler::MacroAutomationPlaybackService playback(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            state.pages,
            state.macroUi,
            state.statusBar,
            &state.macroRuntimeOwnerRevision,
        },
        services,
        midi
    );

    assert(services.takeManualControl(0, 0.42f));
    state.requestMacroRuntimeOwnerActivation();
    playback.update(1000);

    float manualValue = 0.0f;
    assert(services.manualOverrideValueFor(0, manualValue));
    assert(std::fabs(manualValue - 0.42f) < 0.0001f);
    assert(std::fabs(state.macros[0].value.get() - 0.67f) < 0.01f);
    assert(midiTransport.ccCount == 1);

    std::cout << "[PASS] test_runtime_owner_activation_preserves_manual_ownership\n";
}

}  // namespace

int main() {
    test_runtime_projection_publication_is_atomic();
    test_playback_updates_runtime_and_sends_cc_when_value_changes();
    test_stopped_transport_publishes_static_owner_without_playing_curve();
    test_update_period_remains_bounded_across_millisecond_rollover();
    test_manual_override_replaces_automation_base_until_resume();
    test_modulation_only_playback_and_depth_zero_remain_computed();
    test_recording_keeps_modulation_audible_and_active_after_commit();
    test_reactivating_slot_or_lane_resends_value_superseded_while_inactive();
    test_runtime_owner_epoch_is_independent_from_navigation_and_transport();
    test_runtime_owner_activation_preserves_manual_ownership();

    std::cout << "\nAll MacroAutomationPlaybackService tests passed.\n";
    return 0;
}

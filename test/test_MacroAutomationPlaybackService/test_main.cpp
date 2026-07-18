#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Result.hpp>

#include "../../src/handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "../../src/handler/macro/MacroAutomationPlaybackService.hpp"
#include "../../src/handler/macro/MacroAutomationTiming.hpp"
#include "../../src/handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "../../src/sequencer/RealtimeMidiQueue.hpp"
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

uint32_t g_now_ms = 0U;

uint32_t mockTimeMs() {
    return g_now_ms;
}

/** Drives the playback service through the same clock/frame/queue path as Core. */
class PlaybackHarness {
public:
    PlaybackHarness(
        core::state::CoreState& state,
        core::handler::MacroPerformanceDomainServices services,
        oc::api::MidiAPI& midi,
        const oc::state::Signal<uint32_t>* runtimeOwnerRevision = nullptr
    )
        : state_(state)
        , midi_(midi)
        , coordinator_(queue_)
        , adapter_(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  state.pages,
              },
              services,
              coordinator_
          )
        , playback_(
              core::handler::MacroAutomationPlaybackService::StateRefs{
                  state.pages,
                  state.macroUi,
                  runtimeOwnerRevision,
              },
              services,
              adapter_
          ) {}

    void update(uint32_t nowMs) {
        const bool playing = state_.statusBar.playing.get();
        if (clock_initialized_) {
            if (playing && was_playing_) {
                elapsed_playing_ms_ += nowMs - last_now_ms_;
            }
        } else {
            clock_initialized_ = true;
        }
        last_now_ms_ = nowMs;
        was_playing_ = playing;
        g_now_ms = nowMs;

        const float tempo = std::max(1.0f, state_.statusBar.tempo.get());
        const uint32_t sequencerTick = static_cast<uint32_t>(
            static_cast<double>(elapsed_playing_ms_) * tempo * 24.0 /
            60000.0
        );
        const uint32_t tickPeriodUs = static_cast<uint32_t>(
            60000000.0 / (static_cast<double>(tempo) * 24.0) + 0.5
        );
        coordinator_.publishProjectControlClock(
            sequencerTick,
            playing,
            elapsed_playing_ms_ * 1000U,
            tickPeriodUs
        );
        playback_.update(nowMs);

        deadline_us_ += 1000U;
        assert(coordinator_.resolveLive(deadline_us_).ok());
        queue_.drainDue(midi_, deadline_us_, UINT32_MAX);
    }

    void reset() {
        playback_.reset();
        clock_initialized_ = false;
        was_playing_ = false;
        last_now_ms_ = 0U;
        elapsed_playing_ms_ = 0U;
    }

private:
    core::state::CoreState& state_;
    oc::api::MidiAPI& midi_;
    core::sequencer::RealtimeMidiQueue queue_{};
    core::handler::MidiCcGlobalFrameCoordinator coordinator_;
    core::handler::MacroMidiCcRuntimeAdapter adapter_;
    core::handler::MacroAutomationPlaybackService playback_;
    bool clock_initialized_ = false;
    bool was_playing_ = false;
    uint32_t last_now_ms_ = 0U;
    uint32_t elapsed_playing_ms_ = 0U;
    uint32_t deadline_us_ = 0U;
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
    PlaybackHarness playback(
        state,
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi,
        &state.macroRuntimeOwnerRevision
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

void test_project_control_cadence_tracks_motion_without_unbounded_work() {
    namespace modulation = core::state::modulation;
    namespace timing = core::handler::macro;

    modulation::ProjectModulationRuntimePlan plan{};
    modulation::ProjectCurveArena curves{};
    modulation::ProjectControlTimeTelemetry telemetry{};

    assert(timing::projectControlUpdatePeriodMilliseconds(
        plan,
        curves,
        telemetry,
        0U
    ) == timing::MACRO_AUTOMATION_UPDATE_PERIOD_MS);

    auto& source = plan.sources[0];
    plan.sourceCount = 1U;
    source.kind = modulation::ModulatorKind::LFO;
    source.flags = modulation::PROJECT_MODULATOR_FLAG_ENABLED;
    source.traits.lfo.timing = modulation::ModulatorTimingMode::FREE;
    source.parameters.lfo.freePeriodMs = 8U;
    assert(timing::projectControlUpdatePeriodMilliseconds(
        plan,
        curves,
        telemetry,
        1U
    ) == timing::MACRO_AUTOMATION_MIN_UPDATE_PERIOD_MS);

    source.traits.lfo.timing = modulation::ModulatorTimingMode::SYNC;
    source.parameters.lfo.periodTicks = 12U;  // 1/64.
    modulation::publishProjectControlTimeTelemetry(
        telemetry,
        {.musicalTick = 0U, .monotonicMs = 0U, .playing = true}
    );
    assert(timing::projectControlUpdatePeriodMilliseconds(
        plan,
        curves,
        telemetry,
        1U
    ) == 2U);  // Safe first-frame estimate at the supported 300 BPM ceiling.

    modulation::publishProjectControlTimeTelemetry(
        telemetry,
        {.musicalTick = 384U, .monotonicMs = 1000U, .playing = true}
    );
    assert(timing::projectControlUpdatePeriodMilliseconds(
        plan,
        curves,
        telemetry,
        1U
    ) == 4U);  // 1/64 at the observed 120 BPM clock.

    // Dense Projects trade visual/control oversampling for a strict bounded
    // evaluation budget; the analytic generator remains time-correct.
    assert(timing::projectControlUpdatePeriodMilliseconds(
        plan,
        curves,
        telemetry,
        128U
    ) == timing::MACRO_AUTOMATION_UPDATE_PERIOD_MS);

    source.kind = modulation::ModulatorKind::ADSR;
    source.parameters.adsr.attack = 0U;
    source.parameters.adsr.decay = 0U;
    source.parameters.adsr.release = 0U;
    assert(timing::projectControlUpdatePeriodMilliseconds(
        plan,
        curves,
        telemetry,
        1U
    ) == timing::MACRO_AUTOMATION_UPDATE_PERIOD_MS);

    std::cout << "[PASS] adaptive cadence follows motion and bounds workload\n";
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
    PlaybackHarness playback(
        state,
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
    PlaybackHarness playback(
        state,
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
    PlaybackHarness playback(
        state,
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
    PlaybackHarness playback(
        state,
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
    PlaybackHarness playback(
        state,
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
    const uint16_t pointCount = slot.compatibility.modulation.pointCount;
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
    assert(slot.compatibility.modulation.pointCount == pointCount);

    assert(core::state::modulation::setProjectControlModulationAmount(
        state.pages.control,
        address,
        0.4f
    ));
    playback.update(2000);
    assert(midiTransport.ccCount == 3);
    assert(midiTransport.lastValue >= 50 && midiTransport.lastValue <= 52);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(slot.compatibility.modulation.pointCount == pointCount);

    std::cout << "[PASS] test_modulation_only_playback_and_depth_zero_remain_computed\n";
}

void test_automation_take_keeps_modulation_audible_and_active_after_commit() {
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
    PlaybackHarness playback(
        state,
        services,
        midi
    );

    playback.update(1000);
    assert(midiTransport.ccCount == 1);
    assert(midiTransport.lastValue >= 31 && midiTransport.lastValue <= 32);

    (void)services.setAutomationTakeTiming(
        core::state::macro::MacroAutomationTakeTiming::HOLD
    );
    assert(services.armAutomationTake());
    assert(services.recordAutomationTakeValue(0, 1100, 0.4f));
    playback.update(1250);
    assert(midiTransport.ccCount == 2);
    assert(midiTransport.lastValue >= 66 && midiTransport.lastValue <= 67);
    const auto projection = state.macroUi.runtimeProjections[0];
    assert(projection.valid && projection.modulationActive);
    assert(std::fabs(projection.base - 0.4f) < 0.005f);
    assert(std::fabs(projection.modulation - 0.125f) < 0.01f);

    assert(services.recordAutomationTakeValue(0, 1600, 0.6f));
    assert(services.releaseAutomationTake(2000));
    const auto slot = test_support::project_control::readSlot(
        state.pages.control,
        {state.pages.currentActiveTrack(), state.pages.currentActivePage(), 0}
    );
    assert(core::state::macro::macroCurvePlaybackActive(slot.compatibility.modulation));
    assert(slot.compatibility.modulation.playbackState ==
           core::state::macro::MacroCurvePlaybackState::ACTIVE);

    std::cout << "[PASS] take captures raw Base while Modulation stays audible\n";
}

void test_shared_take_publishes_live_base_without_printing_modulation() {
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
    const auto services =
        core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    PlaybackHarness playback(state, services, midi);

    playback.update(1000U);
    assert(services.armAutomationTake());
    assert(services.recordAutomationTakeValue(0U, 1200U, 0.4f));
    playback.update(1250U);

    const auto projection = state.macroUi.runtimeProjections[0];
    assert(projection.valid && projection.modulationActive);
    assert(std::fabs(projection.base - 0.4f) < 0.01f);
    assert(std::fabs(projection.modulation - 0.125f) < 0.01f);
    assert(midiTransport.lastValue >= 66U && midiTransport.lastValue <= 67U);
    assert(services.cancelAutomationTake());

    std::cout
        << "[PASS] shared take publishes Base while Modulation stays live\n";
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
    PlaybackHarness playback(
        state,
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
    PlaybackHarness playback(
        state,
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        midi,
        &state.macroRuntimeOwnerRevision
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
    // The production coordinator retains physical output ownership across a
    // transport pause, so restarting on the same value must not resend MIDI.
    assert(midiTransport.ccCount == beforeStop);
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
    PlaybackHarness playback(
        state,
        services,
        midi,
        &state.macroRuntimeOwnerRevision
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
    oc::time::setProvider(mockTimeMs);
    test_runtime_projection_publication_is_atomic();
    test_project_control_cadence_tracks_motion_without_unbounded_work();
    test_playback_updates_runtime_and_sends_cc_when_value_changes();
    test_stopped_transport_publishes_static_owner_without_playing_curve();
    test_update_period_remains_bounded_across_millisecond_rollover();
    test_manual_override_replaces_automation_base_until_resume();
    test_modulation_only_playback_and_depth_zero_remain_computed();
    test_automation_take_keeps_modulation_audible_and_active_after_commit();
    test_shared_take_publishes_live_base_without_printing_modulation();
    test_reactivating_slot_or_lane_resends_value_superseded_while_inactive();
    test_runtime_owner_epoch_is_independent_from_navigation_and_transport();
    test_runtime_owner_activation_preserves_manual_ownership();

    std::cout << "\nAll MacroAutomationPlaybackService tests passed.\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <oc/api/MidiAPI.hpp>
#include <oc/impl/NullMidi.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/time/Time.hpp>

#include "../../src/handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/sequencer/SequencerCcLaneRuntime.hpp"
#include "../../src/sequencer/SequencerPlaybackService.hpp"
#include "../../src/sequencer/SequencerRuntimeGraphBank.hpp"
#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/StatusBarState.hpp"
#include "../../src/state/project/ProjectNavigationState.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;

uint32_t fakeMicros = 0;

void installTimeProvider() {
    oc::time::setMicrosProvider([]() { return fakeMicros; });
}

void drainDue(core::sequencer::RealtimeMidiQueue& queue,
              oc::api::MidiAPI& midi,
              uint32_t nowUs,
              uint32_t budgetUs) {
    fakeMicros = nowUs;
    queue.drainDue(midi, nowUs, budgetUs);
}

class MockMidiTransport : public oc::interface::IMidi {
public:
    struct Message {
        core::sequencer::RealtimeMidiEventType type;
        uint8_t channel;
        uint8_t note;
        uint8_t value;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t channel, uint8_t controller, uint8_t value) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::ControlChange,
            channel,
            controller,
            value,
        });
    }
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::NoteOn,
            channel,
            note,
            velocity,
        });
    }
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::NoteOff,
            channel,
            note,
            velocity,
        });
    }
    void sendSysEx(const uint8_t*, size_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendPitchBend(uint8_t, int16_t) override {}
    void sendChannelPressure(uint8_t, uint8_t) override {}
    void sendClock() override {}
    void sendStart() override {}
    void sendStop() override {}
    void sendContinue() override {}
    void setOnCC(CCCallback callback) override { onCC = std::move(callback); }
    void setOnNoteOn(NoteCallback callback) override { onNoteOn = std::move(callback); }
    void setOnNoteOff(NoteCallback callback) override { onNoteOff = std::move(callback); }
    void setOnSysEx(SysExCallback callback) override { onSysEx = std::move(callback); }
    void setOnClock(ClockCallback callback) override { onClock = std::move(callback); }
    void setOnStart(RealtimeCallback callback) override { onStart = std::move(callback); }
    void setOnStop(RealtimeCallback callback) override { onStop = std::move(callback); }
    void setOnContinue(RealtimeCallback callback) override { onContinue = std::move(callback); }

    bool has(core::sequencer::RealtimeMidiEventType type, uint8_t note) const {
        for (const auto& message : messages) {
            if (message.type == type && message.note == note) return true;
        }
        return false;
    }

    std::vector<Message> messages;
    CCCallback onCC;
    NoteCallback onNoteOn;
    NoteCallback onNoteOff;
    SysExCallback onSysEx;
    ClockCallback onClock;
    RealtimeCallback onStart;
    RealtimeCallback onStop;
    RealtimeCallback onContinue;
};

void enableStep(SequencerState& state, uint8_t step) {
    auto mask = state.pattern.enabledMask.get();
    mask.setBit(step, true);
    state.pattern.enabledMask.set(mask);
}

const core::sequencer::SequencerRuntimeSnapshotBank::Snapshot& refreshSnapshot(
    core::sequencer::SequencerRuntimeSnapshotBank& bank,
    core::sequencer::SequencerRuntimeGraphBank& graphBank,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::sequencer::SequencerTrackActivationQueue* activations = nullptr
) {
    const auto activationPublication = activations != nullptr
        ? activations->captureRuntimePublication()
        : core::state::sequencer::SequencerTrackActivationRuntimePublication{};
    assert(graphBank.prepare(sequencer, trackBank));
    const uint8_t index = bank.refresh();
    graphBank.publishPrepared([&bank, index, activations, &activationPublication]() {
        bank.commit(index);
        if (activations != nullptr) {
            activations->applyRuntimePublication(activationPublication);
        }
    });
    return bank.activeSnapshot();
}

void setRootStep(core::state::sequencer::SequencerPatternState& pattern,
                 uint8_t note,
                 uint8_t length) {
    pattern.length.set(length);
    pattern.stepsPerBeat.set(4);
    pattern.note[0] = note;
    pattern.velocity[0] = 100;
    pattern.gate[0] = 100;
    pattern.setEnabled(0, true);
    pattern.bumpStepDataRevision();
}

void test_graph_revision_change_resyncs_playback_service_graph() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer,
        bank,
        projectNavigation,
    };

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
    };

    const auto& snapshot = refreshSnapshot(snapshotBank, runtimeGraphBank, sequencer, bank);
    service.update(snapshot, 0, true, 0, 1000, false);
    service.update(snapshot, 12, true, 12000, 1000, false);
    assert(midiQueue.size() == 2);
    midiQueue.clear();
    for (uint8_t track = 0;
         track < core::sequencer::SequencerPlaybackService::TRACK_COUNT;
         ++track) {
        service.stopTrack(track);
        midiQueue.clear();
    }
    service.completeStop();
    midiQueue.clear();

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        sequencer.pattern,
        rootNode,
        2
    );
    assert(sequence.ok);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* child = graph->sequence(sequence.id);
    assert(child != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(
        sequencer.pattern,
        static_cast<uint16_t>(child->firstStepNode + 1U),
        7
    ));
    assert(graph->stepNodes[child->firstStepNode + 1U].has(STEP_NODE_NOTE_OFFSET));

    const auto& graphSnapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    service.update(graphSnapshot, 0, true, 0, 1000, false);
    service.update(graphSnapshot, 12, true, 12000, 1000, false);

    assert(midiQueue.size() == 4);
    oc::impl::NullMidi midiTransport;
    oc::api::MidiAPI midi{midiTransport};
    drainDue(midiQueue, midi, 12000, 10000);
    const auto projection = service.takeUiProjectionSnapshot();
    assert(projection.noteOutPulse);
    assert(projection.trackVelocity[0] == 96);

    std::cout << "[PASS] test_graph_revision_change_resyncs_playback_service_graph\n";
}

void test_muted_track_does_not_emit_note_events() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer,
        bank,
        projectNavigation,
    };

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);
    assert(bank.setTrackMuted(0, true));

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
    };

    const auto& snapshot = refreshSnapshot(snapshotBank, runtimeGraphBank, sequencer, bank);
    assert(snapshot.enabledMask == 0x0001);
    assert(snapshot.mutedMask == 0x0001);
    service.update(snapshot, 0, true, 0, 1000, false);

    assert(midiQueue.size() == 0);

    assert(bank.setTrackMuted(0, false));
    const auto& unmutedSnapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    service.update(unmutedSnapshot, 0, true, 0, 1000, false);
    assert(midiQueue.size() > 0);

    std::cout << "[PASS] test_muted_track_does_not_emit_note_events\n";
}

void test_audible_track_applies_at_local_loop_boundary_and_cancels_old_midi_plan() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::state::sequencer::SequencerTrackActivationQueue activations;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    setRootStep(sequencer.pattern, 60, 4);

    core::sequencer::SequencerPlaybackService service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 0, true, 0, 1000, false);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 0, UINT32_MAX);
    assert(transport.has(core::sequencer::RealtimeMidiEventType::NoteOn, 60));
    midiQueue.clear();

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0001, 0x0001, 0, true, paste));
    assert(activations.armPrepared(paste));
    setRootStep(sequencer.pattern, 72, 4);
    activations.publishPrepared(paste);
    const auto& staged = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );

    assert(midiQueue.push({
        .deadlineUs = 1000000,
        .type = core::sequencer::RealtimeMidiEventType::NoteOn,
        .channel = 0,
        .note = 99,
        .velocity = 100,
        .trackIndex = 0,
    }));
    service.update(staged, 12, true, 12000, 1000, false);
    drainDue(midiQueue, midi, 12000, UINT32_MAX);
    assert(activations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::QUEUED);
    assert(!transport.has(core::sequencer::RealtimeMidiEventType::NoteOn, 72));

    service.update(staged, 24, true, 24000, 1000, false);
    drainDue(midiQueue, midi, 24000, UINT32_MAX);
    assert(activations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::APPLIED);
    assert(transport.has(core::sequencer::RealtimeMidiEventType::NoteOff, 60));
    assert(transport.has(core::sequencer::RealtimeMidiEventType::NoteOn, 72));

    drainDue(midiQueue, midi, 2000000, UINT32_MAX);
    assert(!transport.has(core::sequencer::RealtimeMidiEventType::NoteOn, 99));

    std::cout
        << "[PASS] test_audible_track_applies_at_local_loop_boundary_and_cancels_old_midi_plan\n";
}

void test_transport_stop_applies_staged_track_on_first_scheduler_boundary() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::state::sequencer::SequencerTrackActivationQueue activations;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    setRootStep(sequencer.pattern, 60, 8);
    core::sequencer::SequencerPlaybackService service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 7, true, 7000, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0001, 0x0001, 0, true, paste));
    assert(activations.armPrepared(paste));
    setRootStep(sequencer.pattern, 73, 8);
    activations.publishPrepared(paste);
    const auto& staged = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(staged, 8, false, 8000, 1000, false);
    assert(activations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::APPLIED);

    std::cout
        << "[PASS] test_transport_stop_applies_staged_track_on_first_scheduler_boundary\n";
}

void test_inactive_target_applies_on_first_staged_update() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::state::sequencer::SequencerTrackActivationQueue activations;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerPlaybackService service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 5, true, 5000, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0002, 0x0001, 0, true, paste));
    assert(paste.localLoopBoundaryMask == 0);
    assert(activations.armPrepared(paste));
    setRootStep(bank.track(1), 74, 8);
    bank.syncSharedTrackState(0x0003, 0);
    activations.publishPrepared(paste);
    const auto& staged = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(staged, 7, true, 7000, 1000, false);
    assert(activations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::APPLIED);

    std::cout << "[PASS] test_inactive_target_applies_on_first_staged_update\n";
}

void test_multi_track_activation_uses_each_tracks_local_loop_boundary() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::state::sequencer::SequencerTrackActivationQueue activations;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    setRootStep(sequencer.pattern, 60, 4);
    setRootStep(bank.track(1), 61, 8);
    bank.syncSharedTrackState(0x0003, 0);
    core::sequencer::SequencerPlaybackService service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 1, true, 1000, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0003, 0x0003, 0, true, paste));
    assert(activations.armPrepared(paste));
    setRootStep(sequencer.pattern, 70, 4);
    setRootStep(bank.track(1), 71, 8);
    activations.publishPrepared(paste);
    const auto& staged = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );

    service.update(staged, 24, true, 24000, 1000, false);
    assert(activations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::APPLIED);
    assert(activations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::QUEUED);

    service.update(staged, 48, true, 48000, 1000, false);
    assert(activations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::APPLIED);

    std::cout
        << "[PASS] test_multi_track_activation_uses_each_tracks_local_loop_boundary\n";
}

void test_cc_lane_runtime_is_integrated_once_per_tick_before_note_on() {
    namespace seq = core::state::sequencer;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::handler::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    setRootStep(sequencer.pattern, 60, 4);
    sequencer.pattern.midiChannel.set(0);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 74,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 64,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 0, 91).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    assert(snapshotBank.lastRefreshSucceeded());
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);

    service.update(initial, 0, true, 1000, 1000, false, laneSnapshot);
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1);
    assert(midiQueue.size() >= 2);  // resolved CC plus note-engine plan
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 1000, UINT32_MAX);
    assert(transport.messages.size() == 2);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].channel == 0);
    assert(transport.messages[0].note == 74);
    assert(transport.messages[0].value == 91);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[1].channel == 0);
    assert(transport.messages[1].note == 60);
    const auto firstProjection = service.takeUiProjectionSnapshot();
    assert(firstProjection.ccOutPulse);

    midiQueue.clear();
    transport.messages.clear();
    service.update(initial, 0, true, 1100, 1000, false, laneSnapshot);
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1);
    assert(midiQueue.size() == 0);
    assert(!service.takeUiProjectionSnapshot().ccOutPulse);

    sequencer.pattern.midiChannel.set(4);
    const auto& migrated = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(migrated, 1, true, 2000, 1000, false, laneSnapshot);
    assert(coordinator.diagnostics().publishedLaneFrameCount == 2);
    drainDue(midiQueue, midi, 2000, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].channel == 4);
    assert(transport.messages[0].note == 74);
    assert(transport.messages[0].value == 91);

    midiQueue.clear();
    service.update(migrated, 1, false, 3000, 1000, false, laneSnapshot);
    // Stopping transport preserves the last lane hold in the coordinator.  It
    // must not publish an empty frame, which would temporarily expose a lower
    // priority persistent author such as a Macro.
    assert(coordinator.diagnostics().publishedLaneFrameCount == 2);
    assert(ccRuntime.hasHeldValue(0, 0));

    std::cout
        << "[PASS] test_cc_lane_runtime_is_integrated_once_per_tick_before_note_on\n";
}

void test_track_three_channel_five_cc74_precedes_note_on() {
    namespace seq = core::state::sequencer;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::handler::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    constexpr uint8_t trackThree = 2;
    constexpr uint8_t channelFive = 4;
    auto& pattern = bank.track(trackThree);
    setRootStep(pattern, 67, 4);
    pattern.midiChannel.set(channelFive);
    auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 74,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 0,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 0, 96).changed());
    pattern.ccLaneRevision.set(lanes->revision);
    bank.syncSharedTrackState(
        static_cast<uint16_t>((1U << 0U) | (1U << trackThree)),
        0
    );

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);
    service.update(snapshot, 0, true, 0, 1000, false, laneSnapshot);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 0, UINT32_MAX);
    if (transport.messages.size() != 2) {
        std::cerr << "track3/ch5 messages=" << transport.messages.size() << '\n';
        for (const auto& message : transport.messages) {
            std::cerr << "  type=" << static_cast<int>(message.type)
                      << " ch=" << static_cast<int>(message.channel)
                      << " data=" << static_cast<int>(message.note)
                      << " value=" << static_cast<int>(message.value) << '\n';
        }
    }
    assert(transport.messages.size() == 2);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].channel == channelFive);
    assert(transport.messages[0].note == 74);
    assert(transport.messages[0].value == 96);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[1].channel == channelFive);
    assert(transport.messages[1].note == 67);

    std::cout
        << "[PASS] test_track_three_channel_five_cc74_precedes_note_on\n";
}

void test_cc_lane_event_is_emitted_on_note_off_only_step() {
    namespace seq = core::state::sequencer;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::handler::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    // Step 0 owns the note. Step 1 is deliberately note-empty and only owns
    // a CC event, at the exact tick where step 0's 100% gate emits Note-Off.
    setRootStep(sequencer.pattern, 60, 4);
    sequencer.pattern.midiChannel.set(0);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 74,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 64,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 1, 91).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);
    const auto* runtimeLanes = laneSnapshot->lanesForTrack(0);
    assert(runtimeLanes != nullptr);
    assert(runtimeLanes->lanes[0].activeMask.test(1));
    assert(runtimeLanes->lanes[0].values[1] == 91);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    service.update(snapshot, 0, true, 0, 1000, false, laneSnapshot);
    drainDue(midiQueue, midi, 0, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    transport.messages.clear();

    constexpr uint32_t ticksPerStep = oc::note::clock::PPQN / 4U;
    constexpr uint32_t stepBoundaryUs = ticksPerStep * 1000U;
    service.update(
        snapshot,
        ticksPerStep,
        true,
        stepBoundaryUs,
        1000,
        false,
        laneSnapshot
    );
    if (!ccRuntime.hasHeldValue(0, 0)) {
        const auto& diagnostics = coordinator.diagnostics();
        std::cerr << "note-off/cc runtime missing hold; laneFrames="
                  << diagnostics.publishedLaneFrameCount
                  << " resolved=" << diagnostics.resolvedLiveFrameCount
                  << " rejected=" << diagnostics.queueRejectedFrameCount << '\n';
    }
    assert(ccRuntime.hasHeldValue(0, 0));
    assert(ccRuntime.heldValue(0, 0) == 91);
    drainDue(midiQueue, midi, stepBoundaryUs, UINT32_MAX);
    if (transport.messages.size() != 2) {
        std::cerr << "note-off/cc messages=" << transport.messages.size() << '\n';
        for (const auto& message : transport.messages) {
            std::cerr << "  type=" << static_cast<int>(message.type)
                      << " ch=" << static_cast<int>(message.channel)
                      << " data=" << static_cast<int>(message.note)
                      << " value=" << static_cast<int>(message.value) << '\n';
        }
    }
    assert(transport.messages.size() == 2);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[0].note == 60);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[1].note == 74);
    assert(transport.messages[1].value == 91);
    assert(service.takeUiProjectionSnapshot().ccOutPulse);

    std::cout
        << "[PASS] test_cc_lane_event_is_emitted_on_note_off_only_step\n";
}

void test_editing_future_cc_step_waits_for_playhead() {
    namespace seq = core::state::sequencer;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::handler::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.midiChannel.set(2);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 71,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 0,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);
    service.update(initial, 0, true, 0, 1000, false, laneSnapshot);
    assert(midiQueue.size() == 0);
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1);

    // Editing step 2 while the playhead is still on step 0 must update the
    // immutable project snapshot only. It must not author a live value early.
    lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 2, 99).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);
    const auto& edited = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);

    service.update(edited, 1, true, 1000, 1000, false, laneSnapshot);
    assert(midiQueue.size() == 0);
    assert(!ccRuntime.hasHeldValue(0, 0));
    constexpr uint32_t ticksPerStep = oc::note::clock::PPQN / 4U;
    service.update(
        edited,
        ticksPerStep,
        true,
        ticksPerStep * 1000U,
        1000,
        false,
        laneSnapshot
    );
    assert(midiQueue.size() == 0);
    assert(!ccRuntime.hasHeldValue(0, 0));

    const uint32_t targetTick = ticksPerStep * 2U;
    const uint32_t targetUs = targetTick * 1000U;
    service.update(edited, targetTick, true, targetUs, 1000, false, laneSnapshot);
    assert(ccRuntime.hasHeldValue(0, 0));
    assert(ccRuntime.heldValue(0, 0) == 99);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, targetUs, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].channel == 2);
    assert(transport.messages[0].note == 71);
    assert(transport.messages[0].value == 99);

    std::cout << "[PASS] test_editing_future_cc_step_waits_for_playhead\n";
}

void test_transport_stop_keeps_lane_winner_without_macro_fallback_or_reemit() {
    namespace seq = core::state::sequencer;
    namespace shared = core::state::shared;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::handler::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 74,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 64,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 0, 91).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);
    const shared::MidiCcCandidate macro{
        .destination = shared::MidiCcDestination{
            .identity = shared::MidiCcDestinationIdentity{
                .port = 0,
                .channel = 0,
                .controller = 74,
            },
            .routeValidity = shared::MidiCcRouteValidity::VALID,
        },
        .author = shared::MidiCcAuthor{
            .candidateClass = shared::MidiCcCandidateClass::MACRO_STATIC,
            .stableAddress = 0,
        },
        .localValue = 33,
    };
    assert(coordinator.publishPersistentAuthors(&macro, 1));

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(snapshot, 0, true, 1000, 1000, false, laneSnapshot);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 1000, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].value == 91);
    assert(service.takeUiProjectionSnapshot().ccOutPulse);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinationCount == 1);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               shared::MidiCcCandidateClass::SEQUENCER_CC_LANE);
        assert(telemetry->destinations[0].finalValue == 91);
    }

    transport.messages.clear();
    midiQueue.clear();
    service.markCcTransportStopped();
    service.update(snapshot, 0, false, 2000, 1000, false, laneSnapshot);
    drainDue(midiQueue, midi, 2000, UINT32_MAX);
    assert(transport.messages.empty());
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               shared::MidiCcCandidateClass::SEQUENCER_CC_LANE);
        assert(telemetry->destinations[0].finalValue == 91);
    }

    service.update(snapshot, 1, true, 3000, 1000, false, laneSnapshot);
    drainDue(midiQueue, midi, 3000, UINT32_MAX);
    assert(transport.messages.empty());
    assert(!service.takeUiProjectionSnapshot().ccOutPulse);

    service.resetCcProject();
    assert(!ccRuntime.hasHeldValue(0, 0));
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinationCount == 0);
    }
    assert(coordinator.diagnostics().publishedLaneFrameCount == 0);
    service.update(snapshot, 1, false, 4000, 1000, false, nullptr);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinationCount == 0);
    }
    assert(midiQueue.size() == 0);

    std::cout
        << "[PASS] test_transport_stop_keeps_lane_winner_without_macro_fallback_or_reemit\n";
}

void test_unassigned_inherited_route_replaces_valid_hold_without_stale_cc() {
    namespace seq = core::state::sequencer;
    namespace shared = core::state::shared;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::handler::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    sequencer.pattern.midiChannel.set(0);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 74,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 64,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 0, 91).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
    };
    auto& valid = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(valid, 0, true, 1000, 1000, false, laneSnapshot);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 1000, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].channel == 0);
    assert(transport.messages[0].value == 91);
    transport.messages.clear();

    // The Track route becomes unassigned while the lane is holding a value.
    // A canonical NO_ROUTE frame must replace the old channel-1 author.
    sequencer.pattern.midiChannel.set(
        shared::MidiCcDestinationIdentity::INVALID_CHANNEL
    );
    auto& unassigned = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(unassigned, 1, true, 2000, 1000, false, laneSnapshot);
    drainDue(midiQueue, midi, 2000, UINT32_MAX);
    assert(transport.messages.empty());
    assert(coordinator.diagnostics().publishedLaneFrameCount == 2);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinationCount == 1);
        assert(telemetry->noRouteCount == 1);
        assert(!telemetry->destinations[0].shouldEmit);
        assert(telemetry->destinations[0].destination.identity.channel ==
               shared::MidiCcDestinationIdentity::INVALID_CHANNEL);
    }

    // Explicit Pin is independent from the missing Track route.
    lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    lanes->lanes[0].destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::PINNED;
    lanes->lanes[0].destination.pinnedPort = 0;
    lanes->lanes[0].destination.pinnedChannel = 6;
    ++lanes->revision;
    sequencer.pattern.ccLaneRevision.set(lanes->revision);
    auto& pinned = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(pinned, 2, true, 3000, 1000, false, laneSnapshot);
    drainDue(midiQueue, midi, 3000, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].channel == 6);
    assert(transport.messages[0].note == 74);
    assert(transport.messages[0].value == 91);

    std::cout
        << "[PASS] unassigned inherited route suppresses stale CC; Pin stays valid\n";
}

}  // namespace

int main() {
    installTimeProvider();
    test_graph_revision_change_resyncs_playback_service_graph();
    test_muted_track_does_not_emit_note_events();
    test_audible_track_applies_at_local_loop_boundary_and_cancels_old_midi_plan();
    test_transport_stop_applies_staged_track_on_first_scheduler_boundary();
    test_inactive_target_applies_on_first_staged_update();
    test_multi_track_activation_uses_each_tracks_local_loop_boundary();
    test_cc_lane_runtime_is_integrated_once_per_tick_before_note_on();
    test_track_three_channel_five_cc74_precedes_note_on();
    test_cc_lane_event_is_emitted_on_note_off_only_step();
    test_editing_future_cc_step_waits_for_playhead();
    test_transport_stop_keeps_lane_winner_without_macro_fallback_or_reemit();
    test_unassigned_inherited_route_replaces_valid_hold_without_stale_cc();

    std::cout << "All SequencerPlaybackService tests passed\n";
    return 0;
}

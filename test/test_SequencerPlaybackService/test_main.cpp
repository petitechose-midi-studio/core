#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <oc/api/MidiAPI.hpp>
#include <oc/impl/NullMidi.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/time/Time.hpp>

#include "../../src/handler/common/SharedTrackDomainServices.hpp"
#include "../../src/sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "../../src/sequencer/SequencerCcLaneRuntime.hpp"
#include "../../src/sequencer/SequencerPlaybackService.hpp"
#include "../../src/sequencer/SequencerRuntimeGraphBank.hpp"
#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/StatusBarState.hpp"
#include "../../src/state/project/ProjectNavigationState.hpp"
#include "../../src/state/project/ProjectTrackDomainOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerPatternRegionOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "support/AdvancingMicrosClock.hpp"

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;

test_support::AdvancingMicrosClock testClock;

void installTimeProvider() {
    testClock.install();
}

void drainDue(core::sequencer::RealtimeMidiQueue& queue,
              oc::api::MidiAPI& midi,
              uint32_t nowUs,
              uint32_t budgetUs) {
    testClock.freezeAt(nowUs);
    queue.drainDue(midi, nowUs, budgetUs);
}

class MockMidiTransport : public oc::interface::IMidi {
public:
    using MidiOutputAcceptance = oc::interface::MidiOutputAcceptance;

    struct Message {
        core::sequencer::RealtimeMidiEventType type;
        uint8_t channel;
        uint8_t note;
        uint8_t value;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    MidiOutputAcceptance sendCC(uint8_t channel, uint8_t controller, uint8_t value) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::ControlChange,
            channel,
            controller,
            value,
        });
        return MidiOutputAcceptance::ACCEPTED;
    }
    MidiOutputAcceptance sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::NoteOn,
            channel,
            note,
            velocity,
        });
        return MidiOutputAcceptance::ACCEPTED;
    }
    MidiOutputAcceptance sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::NoteOff,
            channel,
            note,
            velocity,
        });
        return MidiOutputAcceptance::ACCEPTED;
    }
    MidiOutputAcceptance sendSysEx(const uint8_t*, size_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendProgramChange(uint8_t, uint8_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendPitchBend(uint8_t, int16_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendChannelPressure(uint8_t, uint8_t) override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendClock() override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendStart() override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendStop() override { return MidiOutputAcceptance::ACCEPTED; }
    MidiOutputAcceptance sendContinue() override { return MidiOutputAcceptance::ACCEPTED; }
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
    pattern.setContentLength(length);
    pattern.stepsPerBeat.set(4);
    pattern.note[0] = note;
    pattern.velocity[0] = 100;
    pattern.gate[0] = 100;
    pattern.setEnabled(0, true);
    pattern.bumpStepDataRevision();
}

core::sequencer::ProjectTrackRuntimeSnapshot makeProjectTracks(
    uint16_t enabledMask = 0x0001U
) {
    core::sequencer::ProjectTrackRuntimeSnapshot snapshot{};
    snapshot.revision = 1U;
    snapshot.enabledMask = enabledMask;
    snapshot.audibleMask = enabledMask;
    for (uint8_t track = 0U; track < snapshot.midiChannels.size(); ++track) {
        snapshot.midiChannels[track] = track;
    }
    return snapshot;
}

/**
 * Test adapter providing a deterministic Project-owned runtime snapshot when a
 * scenario does not need custom Track routing.
 */
class SequencerTrackFixturePlaybackAdapter final
    : public core::sequencer::SequencerPlaybackService {
public:
    using core::sequencer::SequencerPlaybackService::SequencerPlaybackService;

    void update(
        const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
        uint32_t tick,
        bool playing,
        uint32_t nowUs,
        uint32_t tickPeriodUs,
        bool publishRuntimeState = true,
        const core::sequencer::SequencerCcLaneRuntimeProjectSnapshot*
            ccLaneSnapshot = nullptr,
        const core::sequencer::ProjectTrackRuntimeSnapshot* projectTracks = nullptr,
        bool allowPredictiveLookahead = false
    ) {
        if (projectTracks != nullptr) {
            core::sequencer::SequencerPlaybackService::update(
                snapshot,
                tick,
                playing,
                nowUs,
                tickPeriodUs,
                *projectTracks,
                publishRuntimeState,
                ccLaneSnapshot,
                allowPredictiveLookahead
            );
            return;
        }

        core::sequencer::ProjectTrackRuntimeSnapshot projected{};
        projected.revision = 1U;
        projected.enabledMask = snapshot.enabledMask;
        projected.mutedMask = 0U;
        projected.soloMask = 0U;
        projected.audibleMask = snapshot.enabledMask;
        for (uint8_t track = 0U; track < projected.midiChannels.size(); ++track) {
            projected.midiChannels[track] = track;
        }

        core::sequencer::SequencerPlaybackService::update(
            snapshot,
            tick,
            playing,
            nowUs,
            tickPeriodUs,
            projected,
            publishRuntimeState,
            ccLaneSnapshot,
            allowPredictiveLookahead
        );
    }
};

void setProjectTrackMix(
    core::sequencer::ProjectTrackRuntimeSnapshot& snapshot,
    uint16_t mutedMask,
    uint16_t soloMask
) {
    snapshot.mutedMask = static_cast<uint16_t>(
        mutedMask & snapshot.enabledMask
    );
    snapshot.soloMask = static_cast<uint16_t>(
        soloMask & snapshot.enabledMask
    );
    const uint16_t selected = snapshot.soloMask != 0U
        ? snapshot.soloMask
        : snapshot.enabledMask;
    snapshot.audibleMask = static_cast<uint16_t>(
        snapshot.enabledMask &
        static_cast<uint16_t>(~snapshot.mutedMask) &
        selected
    );
    ++snapshot.revision;
}

void storeTrackClipboard(
    core::state::StructureClipboardState& clipboard,
    const core::state::sequencer::SequencerState& editor
) {
    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(editor.pattern, snapshot);
    assert(clipboard.storeSequencerTrack(
        snapshot,
        nullptr,
        0,
        core::state::sequencer::sequencerCcLaneView(editor.pattern)
    ));
}

void test_canonical_project_track_contract_is_the_only_routing_authority() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState navigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue queue;
    core::sequencer::SequencerRuntimeGraphBank graphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshots{
        sequencer, bank, navigation,
    };

    setRootStep(sequencer.pattern, 60U, 4U);
    setRootStep(bank.track(1U), 61U, 4U);
    setRootStep(bank.track(2U), 62U, 4U);
    // Every Sequencer Track is enabled and unmuted. The Project runtime
    // contract owns Channel, Delay, Mute and Solo below.
    bank.syncSharedTrackState(0x0007U, 0x0000U);
    const auto& snapshot = refreshSnapshot(
        snapshots, graphBank, sequencer, bank
    );
    assert(snapshot.enabledMask == 0x0007U);

    auto projectTracks = makeProjectTracks(0x0007U);
    projectTracks.midiChannels[1U] = 9U;
    projectTracks.delayMs[1U] = 5;
    // Track 0 is selected by Solo but removed by Mute; Track 2 is unmuted but
    // removed by Solo. Only Track 1 may emit.
    setProjectTrackMix(projectTracks, 0x0001U, 0x0003U);

    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, queue, graphBank,
    };
    service.update(
        snapshot,
        0U,
        true,
        0U,
        1000U,
        false,
        nullptr,
        &projectTracks
    );

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(queue, midi, 4999U, UINT32_MAX);
    assert(transport.messages.empty());
    drainDue(queue, midi, 5000U, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].channel == 9U);
    assert(transport.messages[0].note == 61U);

    std::cout
        << "[PASS] canonical Project Channel/Delay/Mute/Solo override SEQR mirrors\n";
}

void test_project_track_note_delay_positive_and_predictive_negative() {
    {
        SequencerState sequencer;
        core::state::sequencer::SequencerTrackBankState bank;
        core::state::project::ProjectNavigationState navigation;
        core::state::StatusBarState status;
        core::sequencer::RealtimeMidiQueue queue;
        core::sequencer::SequencerRuntimeGraphBank graphBank;
        core::sequencer::SequencerRuntimeSnapshotBank snapshots{
            sequencer, bank, navigation,
        };
        sequencer.pattern.setContentLength(2U);
        sequencer.pattern.stepsPerBeat.set(4U);
        sequencer.pattern.note[0] = 60U;
        sequencer.pattern.velocity[0] = 100U;
        sequencer.pattern.gate[0] = 100U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.pattern.bumpStepDataRevision();
        const auto& snapshot = refreshSnapshot(
            snapshots, graphBank, sequencer, bank
        );
        auto projectTracks = makeProjectTracks();
        projectTracks.delayMs[0] = 5;

        SequencerTrackFixturePlaybackAdapter service{
            sequencer, status, queue, graphBank,
        };
        MockMidiTransport transport;
        oc::api::MidiAPI midi{transport};

        service.update(
            snapshot, 0U, true, 0U, 1000U, false, nullptr,
            &projectTracks, true
        );
        drainDue(queue, midi, 4999U, UINT32_MAX);
        assert(transport.messages.empty());
        drainDue(queue, midi, 5000U, UINT32_MAX);
        assert(transport.messages.size() == 1U);
        assert(transport.messages[0].type ==
               core::sequencer::RealtimeMidiEventType::NoteOn);

        service.update(
            snapshot, 6U, true, 6000U, 1000U, false, nullptr,
            &projectTracks, true
        );
        drainDue(queue, midi, 10999U, UINT32_MAX);
        assert(transport.messages.size() == 1U);
        drainDue(queue, midi, 11000U, UINT32_MAX);
        assert(transport.messages.size() == 2U);
        assert(transport.messages[1].type ==
               core::sequencer::RealtimeMidiEventType::NoteOff);
    }

    auto runNegativeCase = [](bool predictive) {
        SequencerState sequencer;
        core::state::sequencer::SequencerTrackBankState bank;
        core::state::project::ProjectNavigationState navigation;
        core::state::StatusBarState status;
        core::sequencer::RealtimeMidiQueue queue;
        core::sequencer::SequencerRuntimeGraphBank graphBank;
        core::sequencer::SequencerRuntimeSnapshotBank snapshots{
            sequencer, bank, navigation,
        };
        sequencer.pattern.setContentLength(2U);
        sequencer.pattern.stepsPerBeat.set(4U);
        sequencer.pattern.note[1] = 61U;
        sequencer.pattern.velocity[1] = 100U;
        sequencer.pattern.gate[1] = 100U;
        sequencer.pattern.setEnabled(1U, true);
        sequencer.pattern.bumpStepDataRevision();
        const auto& snapshot = refreshSnapshot(
            snapshots, graphBank, sequencer, bank
        );
        auto projectTracks = makeProjectTracks();
        projectTracks.delayMs[0] = -2;

        SequencerTrackFixturePlaybackAdapter service{
            sequencer, status, queue, graphBank,
        };
        MockMidiTransport transport;
        oc::api::MidiAPI midi{transport};
        service.update(
            snapshot, 0U, true, 0U, 1000U, false, nullptr,
            &projectTracks, predictive
        );
        service.update(
            snapshot, 4U, true, 4000U, 1000U, false, nullptr,
            &projectTracks, predictive
        );
        drainDue(queue, midi, 4000U, UINT32_MAX);
        if (predictive) {
            assert(transport.messages.size() == 1U);
            assert(transport.messages[0].note == 61U);
            const auto telemetry = service.copyActiveRuntimeTelemetry();
            assert(telemetry.playheadStep == 0);
        } else {
            assert(transport.messages.empty());
            service.update(
                snapshot, 6U, true, 6000U, 1000U, false, nullptr,
                &projectTracks, false
            );
            drainDue(queue, midi, 6000U, UINT32_MAX);
            assert(transport.messages.size() == 1U);
            assert(transport.messages[0].note == 61U);
        }
    };

    runNegativeCase(true);
    runNegativeCase(false);
    std::cout
        << "[PASS] Project Track Note delay is signed and external-safe\n";
}

void test_negative_delay_tempo_change_rebuilds_future_plan_once() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState navigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue queue;
    core::sequencer::SequencerRuntimeGraphBank graphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshots{
        sequencer, bank, navigation,
    };

    sequencer.pattern.setContentLength(2U);
    sequencer.pattern.stepsPerBeat.set(4U);
    sequencer.pattern.note[1] = 61U;
    sequencer.pattern.velocity[1] = 100U;
    sequencer.pattern.gate[1] = 100U;
    sequencer.pattern.setEnabled(1U, true);
    sequencer.pattern.bumpStepDataRevision();
    const auto& snapshot = refreshSnapshot(
        snapshots, graphBank, sequencer, bank
    );
    auto projectTracks = makeProjectTracks();
    projectTracks.delayMs[0] = -2;

    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, queue, graphBank,
    };
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    service.update(
        snapshot, 0U, true, 0U, 1500U, false, nullptr,
        &projectTracks, true
    );
    service.update(
        snapshot, 4U, true, 6000U, 1500U, false, nullptr,
        &projectTracks, true
    );
    // Tick 6 has been prepared with an advanced physical deadline, but has
    // not reached the MIDI transport yet.
    assert(queue.size() > 0U);
    drainDue(queue, midi, 6999U, UINT32_MAX);
    assert(transport.messages.empty());

    // A live tempo change invalidates that future projection atomically. The
    // rebuilt 2 ms/tick plan must not retain the 1.5 ms/tick Note On/Off pair.
    service.update(
        snapshot, 4U, true, 6000U, 2000U, false, nullptr,
        &projectTracks, true
    );
    assert(queue.size() == 0U);

    service.update(
        snapshot, 5U, true, 8000U, 2000U, false, nullptr,
        &projectTracks, true
    );
    drainDue(queue, midi, 8000U, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].note == 61U);

    // Keep advancing through the gate boundary. Whether the engine planned
    // the release with the trigger or at the later horizon, only one release
    // may reach MIDI and no active note may remain stuck.
    service.update(
        snapshot, 6U, true, 10000U, 2000U, false, nullptr,
        &projectTracks, true
    );
    service.update(
        snapshot, 10U, true, 18000U, 2000U, false, nullptr,
        &projectTracks, true
    );
    service.update(
        snapshot, 11U, true, 20000U, 2000U, false, nullptr,
        &projectTracks, true
    );
    drainDue(queue, midi, 20000U, UINT32_MAX);
    assert(transport.messages.size() == 2U);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].note == 61U);
    assert(queue.size() == 0U);

    std::cout
        << "[PASS] negative Track delay rebuilds once across tempo change\n";
}

void test_delay_change_during_active_gate_panics_then_resyncs() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState navigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue queue;
    core::sequencer::SequencerRuntimeGraphBank graphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshots{
        sequencer, bank, navigation,
    };

    sequencer.pattern.setContentLength(4U);
    sequencer.pattern.stepsPerBeat.set(4U);
    sequencer.pattern.note[0] = 60U;
    sequencer.pattern.note[1] = 61U;
    sequencer.pattern.velocity[0] = 100U;
    sequencer.pattern.velocity[1] = 100U;
    sequencer.pattern.gate[0] = 100U;
    sequencer.pattern.gate[1] = 100U;
    sequencer.pattern.setEnabled(0U, true);
    sequencer.pattern.setEnabled(1U, true);
    sequencer.pattern.bumpStepDataRevision();
    const auto& snapshot = refreshSnapshot(
        snapshots, graphBank, sequencer, bank
    );
    auto projectTracks = makeProjectTracks();

    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, queue, graphBank,
    };
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    service.update(
        snapshot, 0U, true, 0U, 1000U, false, nullptr,
        &projectTracks, true
    );
    drainDue(queue, midi, 0U, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].note == 60U);

    projectTracks.delayMs[0] = 10;
    ++projectTracks.revision;
    service.update(
        snapshot, 1U, true, 1000U, 1000U, false, nullptr,
        &projectTracks, true
    );
    // Changing timing cancels the old natural release and emits an immediate
    // panic on the route that owns the active note; Track Delay never delays it.
    drainDue(queue, midi, 1000U, UINT32_MAX);
    assert(transport.messages.size() == 2U);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].channel == 0U);
    assert(transport.messages[1].note == 60U);

    drainDue(queue, midi, 6000U, UINT32_MAX);
    assert(transport.messages.size() == 2U);
    service.update(
        snapshot, 6U, true, 6000U, 1000U, false, nullptr,
        &projectTracks, true
    );
    drainDue(queue, midi, 15999U, UINT32_MAX);
    assert(transport.messages.size() == 2U);
    drainDue(queue, midi, 16000U, UINT32_MAX);
    assert(transport.messages.size() == 3U);
    assert(transport.messages[2].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[2].note == 61U);

    service.update(
        snapshot, 12U, true, 12000U, 1000U, false, nullptr,
        &projectTracks, true
    );
    drainDue(queue, midi, 21999U, UINT32_MAX);
    assert(transport.messages.size() == 3U);
    drainDue(queue, midi, 22000U, UINT32_MAX);
    assert(transport.messages.size() == 4U);
    assert(transport.messages[3].type ==
           core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[3].note == 61U);
    assert(queue.size() == 0U);

    std::cout
        << "[PASS] live Track delay change panics old gate then resyncs\n";
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

    sequencer.pattern.setContentLength(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);

    SequencerTrackFixturePlaybackAdapter service{
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

void test_playback_service_uses_one_shot_prelude_then_internal_loop() {
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

    sequencer.pattern.setContentLength(8);
    sequencer.pattern.stepsPerBeat.set(4);
    assert(core::state::sequencer::setPatternPlaybackRegion(
        sequencer.pattern,
        {8, 2, 4, 6}
    ));
    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank,
        runtimeGraphBank,
        sequencer,
        bank
    );

    const std::array<std::pair<uint32_t, int16_t>, 6> expected{{
        {0, 2},
        {6, 3},
        {12, 4},
        {18, 5},
        {24, 4},
        {30, 5},
    }};
    for (const auto& [tick, step] : expected) {
        service.update(snapshot, tick, true, tick * 1000U, 1000, false);
        const auto telemetry = service.copyActiveRuntimeTelemetry();
        assert(telemetry.playheadStep == step);
    }

    std::cout << "[PASS] playback uses Prelude once then the exact Loop region\n";
}

void test_staged_track_applies_at_first_region_loop_start_after_prelude() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::state::sequencer::SequencerTrackActivationQueue activations;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer,
        bank,
        projectNavigation,
    };
    setRootStep(sequencer.pattern, 60, 8);
    assert(core::state::sequencer::setPatternPlaybackRegion(
        sequencer.pattern,
        {8, 1, 3, 6}
    ));

    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank,
        runtimeGraphBank,
        sequencer,
        bank,
        &activations
    );
    service.update(initial, 0, true, 0, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0001, 0x0001, true, paste));
    assert(activations.armPrepared(paste));
    sequencer.pattern.note[0] = 72;
    sequencer.pattern.bumpStepDataRevision();
    activations.publishPrepared(paste);
    const auto& staged = refreshSnapshot(
        snapshotBank,
        runtimeGraphBank,
        sequencer,
        bank,
        &activations
    );

    service.update(staged, 6, true, 6000, 1000, false);
    assert(activations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::QUEUED);
    service.update(staged, 12, true, 12000, 1000, false);
    assert(activations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::APPLIED);

    std::cout << "[PASS] staged Track applies at Loop Start after Prelude\n";
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

    sequencer.pattern.setContentLength(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);
    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
    };

    const auto& snapshot = refreshSnapshot(snapshotBank, runtimeGraphBank, sequencer, bank);
    assert(snapshot.enabledMask == 0x0001);
    auto projectTracks = makeProjectTracks(snapshot.enabledMask);
    setProjectTrackMix(projectTracks, 0x0001U, 0U);
    service.update(
        snapshot,
        0,
        true,
        0,
        1000,
        false,
        nullptr,
        &projectTracks
    );

    assert(midiQueue.size() == 0);

    setProjectTrackMix(projectTracks, 0U, 0U);
    const auto& unmutedSnapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    service.update(
        unmutedSnapshot,
        0,
        true,
        0,
        1000,
        false,
        nullptr,
        &projectTracks
    );
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

    SequencerTrackFixturePlaybackAdapter service{
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
    assert(activations.prepare(0x0001, 0x0001, true, paste));
    assert(activations.armPrepared(paste));
    setRootStep(sequencer.pattern, 72, 4);
    activations.publishPrepared(paste);
    const auto& staged = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );

    core::sequencer::RealtimeMidiEvent pendingNote{};
    pendingNote.deadlineUs = 1000000;
    pendingNote.type = core::sequencer::RealtimeMidiEventType::NoteOn;
    pendingNote.trackIndex = 0;
    pendingNote.channel = 0;
    pendingNote.note = 99;
    pendingNote.velocity = 100;
    assert(midiQueue.push(pendingNote));
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
    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 7, true, 7000, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0001, 0x0001, true, paste));
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
    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 5, true, 5000, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0002, 0x0001, true, paste));
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
    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, midiQueue, runtimeGraphBank, &activations,
    };
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank, &activations
    );
    service.update(initial, 1, true, 1000, 1000, false);

    core::state::sequencer::SequencerTrackActivationBatch paste;
    assert(activations.prepare(0x0003, 0x0003, true, paste));
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
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    setRootStep(sequencer.pattern, 60, 4);
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

    SequencerTrackFixturePlaybackAdapter service{
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
    auto projectTracks = makeProjectTracks();
    projectTracks.midiChannels[0] = 0U;

    service.update(
        initial, 0, true, 1000, 1000, false, laneSnapshot, &projectTracks
    );
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
    service.update(
        initial, 0, true, 1100, 1000, false, laneSnapshot, &projectTracks
    );
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1);
    assert(midiQueue.size() == 0);
    assert(!service.takeUiProjectionSnapshot().ccOutPulse);

    // The Project-owned Track route is authoritative for both the held CC and
    // the active Note.
    projectTracks.midiChannels[0] = 4U;
    ++projectTracks.revision;
    const auto& refreshed = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(
        refreshed, 1, true, 2000, 1000, false, laneSnapshot, &projectTracks
    );
    assert(coordinator.diagnostics().publishedLaneFrameCount == 2);
    drainDue(midiQueue, midi, 2000, UINT32_MAX);
    assert(transport.messages.size() == 2);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[0].channel == 0);
    assert(transport.messages[0].note == 60);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[1].channel == 4);
    assert(transport.messages[1].note == 74);
    assert(transport.messages[1].value == 91);

    // Resynchronization resumes at the next real loop boundary. It must not
    // replay history at the route transaction tick, and every new edge belongs
    // to the canonical Channel rather than the stale SEQR mirror.
    midiQueue.clear();
    transport.messages.clear();
    service.update(
        refreshed, 24, true, 24000, 1000, false, laneSnapshot, &projectTracks
    );
    drainDue(midiQueue, midi, 24000, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].channel == 4);
    assert(transport.messages[0].note == 60);

    midiQueue.clear();
    const uint32_t laneFramesBeforeStop =
        coordinator.diagnostics().publishedLaneFrameCount;
    service.update(
        refreshed, 24, false, 25000, 1000, false, laneSnapshot, &projectTracks
    );
    // Stopping transport preserves the last lane hold in the coordinator.  It
    // must not publish an empty frame, which would temporarily expose a lower
    // priority persistent author such as a Macro.
    assert(coordinator.diagnostics().publishedLaneFrameCount ==
           laneFramesBeforeStop);
    assert(ccRuntime.hasHeldValue(0, 0));

    std::cout
        << "[PASS] test_cc_lane_runtime_is_integrated_once_per_tick_before_note_on\n";
}

void test_project_track_mute_and_solo_filter_note_and_cc_emission() {
    namespace seq = core::state::sequencer;
    SequencerState sequencer;
    seq::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };

    setRootStep(sequencer.pattern, 60, 4);
    auto* trackZeroLanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(trackZeroLanes != nullptr);
    seq::SequencerCcLaneDraft trackZeroLane{};
    trackZeroLane.destination.controller = 74;
    trackZeroLane.destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    assert(seq::createSequencerCcLane(
        *trackZeroLanes, 0, trackZeroLane
    ).changed());
    assert(seq::setSequencerCcLaneEvent(
        *trackZeroLanes, 0, 0, 81
    ).changed());
    sequencer.pattern.bumpCcLaneRevision();

    auto& trackOne = bank.track(1);
    setRootStep(trackOne, 67, 4);
    auto* trackOneLanes = seq::ensureSequencerCcLaneBank(trackOne);
    assert(trackOneLanes != nullptr);
    seq::SequencerCcLaneDraft trackOneLane{};
    trackOneLane.destination.controller = 71;
    trackOneLane.destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    assert(seq::createSequencerCcLane(
        *trackOneLanes, 0, trackOneLane
    ).changed());
    assert(seq::setSequencerCcLaneEvent(
        *trackOneLanes, 0, 0, 92
    ).changed());
    trackOne.bumpCcLaneRevision();
    bank.syncSharedTrackState(0x0003U, 0U);

    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(
        snapshotBank.activeIndex()
    );
    assert(laneSnapshot != nullptr);

    const auto runMixCase = [&](uint16_t mutedMask, uint16_t soloMask) {
        core::sequencer::RealtimeMidiQueue midiQueue;
        core::sequencer::SequencerCcLaneRuntime ccRuntime;
        core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};
        SequencerTrackFixturePlaybackAdapter service{
            sequencer,
            status,
            midiQueue,
            runtimeGraphBank,
            nullptr,
            &ccRuntime,
            &coordinator,
        };
        auto projectTracks = makeProjectTracks(0x0003U);
        projectTracks.midiChannels[0] = 4U;
        projectTracks.midiChannels[1] = 9U;
        setProjectTrackMix(projectTracks, mutedMask, soloMask);

        service.update(
            snapshot,
            0,
            true,
            0,
            1000,
            false,
            laneSnapshot,
            &projectTracks
        );
        MockMidiTransport transport;
        oc::api::MidiAPI midi{transport};
        drainDue(midiQueue, midi, 0, UINT32_MAX);

        assert(transport.messages.size() == 2U);
        assert(transport.messages[0].type ==
               core::sequencer::RealtimeMidiEventType::ControlChange);
        assert(transport.messages[0].channel == 9U);
        assert(transport.messages[0].note == 71U);
        assert(transport.messages[0].value == 92U);
        assert(transport.messages[1].type ==
               core::sequencer::RealtimeMidiEventType::NoteOn);
        assert(transport.messages[1].channel == 9U);
        assert(transport.messages[1].note == 67U);
    };

    // These cases fail independently if either Project mute or Project solo
    // is ignored by one of the Note/CC emission paths.
    runMixCase(0x0001U, 0x0000U);
    runMixCase(0x0000U, 0x0002U);

    std::cout
        << "[PASS] Project mute and solo filter Note and CC emissions\n";
}

void test_project_track_unmute_resumes_current_phase_without_history_replay() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };

    setRootStep(sequencer.pattern, 60, 4);
    sequencer.pattern.note[2] = 64;
    sequencer.pattern.velocity[2] = 100;
    sequencer.pattern.gate[2] = 100;
    sequencer.pattern.setEnabled(2, true);
    sequencer.pattern.bumpStepDataRevision();

    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, midiQueue, runtimeGraphBank,
    };
    auto projectTracks = makeProjectTracks();
    setProjectTrackMix(projectTracks, 0x0001U, 0x0000U);

    service.update(
        snapshot, 0, true, 0, 1000, false, nullptr, &projectTracks
    );
    service.update(
        snapshot, 6, true, 6000, 1000, false, nullptr, &projectTracks
    );
    assert(midiQueue.size() == 0U);

    setProjectTrackMix(projectTracks, 0x0000U, 0x0000U);
    service.update(
        snapshot, 12, true, 12000, 1000, false, nullptr, &projectTracks
    );
    const auto telemetry = service.copyActiveRuntimeTelemetry();
    assert(telemetry.playheadStep == 2);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 12000, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].channel == 0U);
    assert(transport.messages[0].note == 64U);
    assert(!transport.has(core::sequencer::RealtimeMidiEventType::NoteOn, 60U));

    std::cout
        << "[PASS] unmute resumes current phase without historical replay\n";
}

void test_active_track_switch_does_not_panic_or_reset_playing_track() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer, bank, projectNavigation,
    };

    setRootStep(sequencer.pattern, 60, 4);
    bank.syncSharedTrackState(0x0003U, 0U);
    const auto& initial = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    auto projectTracks = makeProjectTracks(0x0003U);
    SequencerTrackFixturePlaybackAdapter service{
        sequencer, status, midiQueue, runtimeGraphBank,
    };

    service.update(
        initial, 0, true, 0, 1000, false, nullptr, &projectTracks
    );
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 0, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].note == 60U);
    transport.messages.clear();
    assert(midiQueue.size() == 0U);

    auto switched = initial;
    switched.activeTrack = 1U;
    service.update(
        switched, 1, true, 1000, 1000, false, nullptr, &projectTracks
    );
    assert(midiQueue.size() == 0U);
    drainDue(midiQueue, midi, 1000, UINT32_MAX);
    assert(transport.messages.empty());

    // The original gate still owns its natural Note Off deadline. A view-only
    // active Track switch must neither panic early nor abandon that edge.
    constexpr uint32_t naturalGateOffUs = oc::note::clock::PPQN / 4U * 1000U;
    service.update(
        switched,
        oc::note::clock::PPQN / 4U,
        true,
        naturalGateOffUs,
        1000,
        false,
        nullptr,
        &projectTracks
    );
    drainDue(midiQueue, midi, naturalGateOffUs, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[0].channel == 0U);
    assert(transport.messages[0].note == 60U);

    std::cout
        << "[PASS] active Track switch is phase/audibility neutral\n";
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
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    constexpr uint8_t trackThree = 2;
    constexpr uint8_t channelFive = 4;
    auto& pattern = bank.track(trackThree);
    setRootStep(pattern, 67, 4);
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

    SequencerTrackFixturePlaybackAdapter service{
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
    auto projectTracks = makeProjectTracks(snapshot.enabledMask);
    projectTracks.midiChannels[trackThree] = channelFive;
    service.update(
        snapshot, 0, true, 0, 1000, false, laneSnapshot, &projectTracks
    );

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

void test_track_paste_emits_note_and_inherited_cc_on_destination_channel() {
    namespace seq = core::state::sequencer;
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings
    );

    constexpr uint8_t sourceChannel = 1;       // User-facing MIDI channel 2.
    constexpr uint8_t destinationChannel = 10; // User-facing MIDI channel 11.
    constexpr uint8_t pastedNote = 60;
    constexpr uint8_t inheritedController = 74;
    constexpr uint8_t inheritedValue = 93;

    setRootStep(state.sequencer.pattern, pastedNote, 1);
    assert(core::state::project::setProjectTrackMidiChannel(
        state.projectTracks,
        0,
        sourceChannel
    ).changed());
    auto* sourceLanes = seq::ensureSequencerCcLaneBank(state.sequencer.pattern);
    assert(sourceLanes != nullptr);
    seq::SequencerCcLaneDraft inheritedLane{};
    inheritedLane.destination.controller = inheritedController;
    inheritedLane.destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    assert(seq::createSequencerCcLane(*sourceLanes, 0, inheritedLane).changed());
    assert(seq::setSequencerCcLaneEvent(
        *sourceLanes, 0, 0, inheritedValue
    ).changed());
    state.sequencer.pattern.bumpCcLaneRevision();

    assert(core::state::project::setProjectTrackMidiChannel(
        state.projectTracks,
        1,
        destinationChannel
    ).changed());
    storeTrackClipboard(state.structureClipboard, state.sequencer);
    const auto paste = core::handler::executeSequencerTrackTransfer(
        state.sequencerTracks,
        state.projectTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        1,
        0,
        &state.sequencerTrackActivations,
        true
    );
    assert(paste.applied());
    assert(state.sequencerTracks.activeTrackIndex() == 1);
    assert(state.projectTracks.authored.midiChannels[1] == destinationChannel);

    // Keep the copied source in the Project but silence it so every emitted
    // message below is an unambiguous proof of the pasted destination route.
    assert(core::state::project::setProjectTrackMuted(
        state.projectTracks,
        0,
        true
    ).changed());

    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        state.sequencer,
        state.sequencerTracks,
        state.projectNavigation,
    };
    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};
    SequencerTrackFixturePlaybackAdapter service{
        state.sequencer,
        state.statusBar,
        midiQueue,
        runtimeGraphBank,
        &state.sequencerTrackActivations,
        &ccRuntime,
        &coordinator,
    };

    const auto& snapshot = refreshSnapshot(
        snapshotBank,
        runtimeGraphBank,
        state.sequencer,
        state.sequencerTracks,
        &state.sequencerTrackActivations
    );
    assert((snapshot.enabledMask & static_cast<uint16_t>(1U << 1U)) != 0);
    assert(snapshot.tracks[1].note[0] == pastedNote);
    assert(snapshot.tracks[1].enabledMask.test(0));
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);
    core::sequencer::ProjectTrackRuntimeSnapshot projectTracks{};
    core::sequencer::captureProjectTrackRuntimeSnapshot(
        state.projectTracks,
        snapshot.enabledMask,
        projectTracks
    );
    assert((projectTracks.mutedMask & static_cast<uint16_t>(1U << 1U)) == 0);

    // Track 2 did not exist before the paste, so its queued activation must be
    // accepted on the first scheduler boundary and become immediately audible.
    service.update(
        snapshot, 0, true, 0, 1000, false, laneSnapshot, &projectTracks
    );
    assert(state.sequencerTrackActivations.telemetry(1).status ==
           seq::SequencerTrackActivationStatus::APPLIED);

    // Applying the activation invalidates any plan authored before that exact
    // boundary. Publish the acknowledged runtime state, then let the pasted
    // Track author its first note plan on the next musical boundary.
    const auto& activeSnapshot = refreshSnapshot(
        snapshotBank,
        runtimeGraphBank,
        state.sequencer,
        state.sequencerTracks,
        &state.sequencerTrackActivations
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);
    service.update(
        activeSnapshot,
        24,
        true,
        24000,
        1000,
        false,
        laneSnapshot,
        &projectTracks
    );

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 24000, UINT32_MAX);

    bool foundInheritedCc = false;
    bool foundPastedNote = false;
    for (const auto& message : transport.messages) {
        if (message.type == core::sequencer::RealtimeMidiEventType::ControlChange &&
            message.note == inheritedController &&
            message.value == inheritedValue) {
            assert(message.channel == destinationChannel);
            foundInheritedCc = true;
        }
        if (message.type == core::sequencer::RealtimeMidiEventType::NoteOn &&
            message.note == pastedNote) {
            assert(message.channel == destinationChannel);
            foundPastedNote = true;
        }
    }
    if (!foundInheritedCc || !foundPastedNote) {
        std::cerr << "post-paste messages=" << transport.messages.size() << '\n';
        for (const auto& message : transport.messages) {
            std::cerr << "  type=" << static_cast<int>(message.type)
                      << " ch=" << static_cast<int>(message.channel)
                      << " data=" << static_cast<int>(message.note)
                      << " value=" << static_cast<int>(message.value) << '\n';
        }
    }
    assert(foundInheritedCc);
    assert(foundPastedNote);

    test_support::drainNotifications();
    std::cout
        << "[PASS] Track paste emits Note and inherited CC on destination channel\n";
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
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    // Step 0 owns the note. Step 1 is deliberately note-empty and only owns
    // a CC event, at the exact tick where step 0's 100% gate emits Note-Off.
    setRootStep(sequencer.pattern, 60, 4);
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

    SequencerTrackFixturePlaybackAdapter service{
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
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    sequencer.pattern.setContentLength(4);
    sequencer.pattern.stepsPerBeat.set(4);
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

    SequencerTrackFixturePlaybackAdapter service{
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
    auto projectTracks = makeProjectTracks();
    projectTracks.midiChannels[0] = 2U;
    service.update(
        initial, 0, true, 0, 1000, false, laneSnapshot, &projectTracks
    );
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

    service.update(
        edited, 1, true, 1000, 1000, false, laneSnapshot, &projectTracks
    );
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
        laneSnapshot,
        &projectTracks
    );
    assert(midiQueue.size() == 0);
    assert(!ccRuntime.hasHeldValue(0, 0));

    const uint32_t targetTick = ticksPerStep * 2U;
    const uint32_t targetUs = targetTick * 1000U;
    service.update(
        edited,
        targetTick,
        true,
        targetUs,
        1000,
        false,
        laneSnapshot,
        &projectTracks
    );
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

void test_negative_project_delay_predicts_cc_lane_without_advancing_live_hold() {
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
    core::sequencer::SequencerCcLaneRuntime predictiveRuntime;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    sequencer.pattern.setContentLength(4);
    sequencer.pattern.stepsPerBeat.set(4);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    const seq::SequencerCcLaneDraft laneDraft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = 74,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
        },
        .initialValue = 0,
    };
    assert(seq::createSequencerCcLane(*lanes, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0, 1, 99).changed());
    sequencer.pattern.ccLaneRevision.set(lanes->revision);

    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
        &predictiveRuntime,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    auto projectTracks = makeProjectTracks();
    projectTracks.delayMs[0] = -2;

    service.update(
        snapshot, 0U, true, 0U, 1000U, false,
        laneSnapshot, &projectTracks, true
    );
    assert(midiQueue.size() == 0U);
    service.update(
        snapshot, 3U, true, 3000U, 1000U, false,
        laneSnapshot, &projectTracks, true
    );
    assert(midiQueue.size() == 0U);
    service.update(
        snapshot, 4U, true, 4000U, 1000U, false,
        laneSnapshot, &projectTracks, true
    );
    assert(midiQueue.size() == 1U);
    assert(!ccRuntime.hasHeldValue(0U, 0U));

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 4000U, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].value == 99U);

    std::cout << "[PASS] negative Track delay predicts Lane without advancing live hold\n";
}

void test_negative_cc_lookahead_crosses_uint32_tick_wrap() {
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
    core::sequencer::SequencerCcLaneRuntime predictiveRuntime;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    constexpr uint32_t tick = UINT32_MAX - 49U;
    constexpr uint32_t tickPeriodUs = 1000U;
    constexpr uint32_t leadTicks = 100U;
    constexpr uint8_t ticksPerStep = oc::note::clock::PPQN / 4U;
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8U);
    oc::note::sequencer::StepSequencerPlaybackTickPosition current{};
    assert(oc::note::sequencer::tryResolvePlaybackTick(
        region, tick, ticksPerStep, current
    ));
    const uint32_t ordinalAdvance = static_cast<uint32_t>(
        (static_cast<uint64_t>(current.tickOffset) + leadTicks) /
        ticksPerStep
    );
    oc::note::sequencer::StepSequencerPlaybackPosition future{};
    assert(oc::note::sequencer::tryResolvePlaybackOrdinal(
        region,
        current.playback.ordinal + ordinalAdvance,
        future
    ));
    assert(future.stepIndex != current.playback.stepIndex);

    sequencer.pattern.setContentLength(8U);
    sequencer.pattern.stepsPerBeat.set(4U);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    seq::SequencerCcLaneDraft laneDraft{};
    laneDraft.destination.controller = 74U;
    laneDraft.destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    assert(seq::createSequencerCcLane(*lanes, 0U, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(
        *lanes, 0U, future.stepIndex, 103U
    ).changed());
    sequencer.pattern.bumpCcLaneRevision();

    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
        &predictiveRuntime,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    auto projectTracks = makeProjectTracks();
    projectTracks.delayMs[0] = -100;

    constexpr uint32_t nowUs = 7000U;
    // Simulate an already-planned Note edge from immediately before the
    // negative-horizon wrap window. The bounded Note fallback must preserve it
    // while the modular CC projection is added independently.
    core::sequencer::RealtimeMidiEvent pendingNote{};
    pendingNote.deadlineUs = nowUs + 500U;
    pendingNote.type = core::sequencer::RealtimeMidiEventType::NoteOn;
    pendingNote.trackIndex = 0U;
    pendingNote.channel = 0U;
    pendingNote.note = 60U;
    pendingNote.velocity = 100U;
    assert(midiQueue.push(pendingNote));
    service.update(
        snapshot, tick, true, nowUs, tickPeriodUs, false,
        laneSnapshot, &projectTracks, true
    );
    assert(midiQueue.size() == 2U);
    assert(!ccRuntime.hasHeldValue(0U, 0U));
    assert(predictiveRuntime.hasHeldValue(0U, 0U));
    assert(coordinator.diagnostics().pendingRemovalRetryCount == 0U);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, nowUs, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].value == 103U);
    assert(midiQueue.size() == 1U);
    drainDue(midiQueue, midi, nowUs + 500U, UINT32_MAX);
    assert(transport.messages.size() == 2U);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[1].note == 60U);
    assert(midiQueue.size() == 0U);
    drainDue(midiQueue, midi, nowUs + 1000U, UINT32_MAX);
    assert(transport.messages.size() == 2U);
    assert(coordinator.diagnostics().pendingRemovalRetryCount == 0U);

    std::cout
        << "[PASS] -100 ms CC projection crosses UINT32 transport wrap\n";
}

void test_failed_negative_cc_projection_falls_back_due_now() {
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
    core::sequencer::SequencerCcLaneRuntime predictiveRuntime;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

    sequencer.pattern.setContentLength(8U);
    sequencer.pattern.stepsPerBeat.set(4U);
    auto* lanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    seq::SequencerCcLaneDraft laneDraft{};
    laneDraft.destination.controller = 71U;
    laneDraft.destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::PINNED;
    laneDraft.destination.pinnedPort = 0U;
    laneDraft.destination.pinnedChannel = 0U;
    assert(seq::createSequencerCcLane(*lanes, 0U, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0U, 0U, 88U).changed());
    sequencer.pattern.bumpCcLaneRevision();

    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
        &predictiveRuntime,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    auto projectTracks = makeProjectTracks();
    // This deliberately exceeds the bounded 128-ordinal projection horizon.
    // With a 1100 us tick the old global negative-delay rule incorrectly
    // assigned the current fallback a 900 us predictive residual.
    projectTracks.delayMs[0] = -32767;
    projectTracks.midiChannels[0] =
        core::state::shared::MidiCcDestinationIdentity::INVALID_CHANNEL;

    constexpr uint32_t nowUs = 9000U;
    constexpr uint32_t tickPeriodUs = 1100U;
    service.update(
        snapshot, 0U, true, nowUs, tickPeriodUs, false,
        laneSnapshot, &projectTracks, true
    );
    assert(ccRuntime.hasHeldValue(0U, 0U));
    assert(!predictiveRuntime.hasHeldValue(0U, 0U));
    assert(midiQueue.size() == 1U);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, nowUs, UINT32_MAX);
    assert(transport.messages.size() == 1U);
    assert(transport.messages[0].value == 88U);

    std::cout
        << "[PASS] failed CC lookahead keeps current fallback deadline at now\n";
}

void test_sixteen_track_negative_cc_lookahead_uses_one_complete_pass() {
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

    for (uint8_t track = 0U; track < 16U; ++track) {
        auto& pattern = track == 0U ? sequencer.pattern : bank.track(track);
        pattern.setContentLength(8U);
        pattern.stepsPerBeat.set(4U);
        auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
        assert(lanes != nullptr);
        for (uint8_t lane = 0U; lane < 4U; ++lane) {
            seq::SequencerCcLaneDraft draft{};
            draft.destination.controller = static_cast<uint8_t>(20U + lane);
            draft.destination.routePolicy =
                seq::SequencerCcLaneRoutePolicy::PINNED;
            draft.destination.pinnedPort = 0U;
            draft.destination.pinnedChannel = track;
            assert(seq::createSequencerCcLane(*lanes, lane, draft).changed());
            assert(seq::setSequencerCcLaneEvent(
                *lanes,
                lane,
                2U,
                static_cast<uint8_t>(track * 4U + lane)
            ).changed());
        }
        pattern.bumpCcLaneRevision();
    }
    bank.syncSharedTrackState(0xFFFFU, 0U);

    core::sequencer::SequencerCcLaneRuntime ccRuntime;
    core::sequencer::SequencerCcLaneRuntime predictiveRuntime;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};
    SequencerTrackFixturePlaybackAdapter service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
        nullptr,
        &ccRuntime,
        &coordinator,
        &predictiveRuntime,
    };
    const auto& snapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    const auto* laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    assert(laneSnapshot != nullptr);
    auto projectTracks = makeProjectTracks(0xFFFFU);
    projectTracks.delayMs.fill(-100);
    // Keep the benchmark scoped to CC projection. Pinned Lane routes remain
    // valid while unassigned Track note routes prevent 16 note schedulers from
    // dominating the host timing sample.
    projectTracks.midiChannels.fill(
        core::state::shared::MidiCcDestinationIdentity::INVALID_CHANNEL
    );

    constexpr uint32_t tickPeriodUs =
        60000000U / (300U * oc::note::clock::PPQN);
    constexpr uint32_t nowUs = 12000U;
    constexpr uint32_t leadTicks =
        (100000U + tickPeriodUs - 1U) / tickPeriodUs;
    constexpr uint32_t residualUs = leadTicks * tickPeriodUs - 100000U;
    const auto singlePassStarted = std::chrono::steady_clock::now();
    service.update(
        snapshot, 0U, true, nowUs, tickPeriodUs, false,
        laneSnapshot, &projectTracks, true
    );
    const uint64_t singlePassUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - singlePassStarted
        ).count()
    );
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1U);
    assert(midiQueue.size() == 0U);
    for (uint8_t track = 0U; track < 16U; ++track) {
        for (uint8_t lane = 0U; lane < 4U; ++lane) {
            // The previous N-pass implementation reset the predictive scratch for
            // every Track, so only Track 16 remained here. All 64 holds prove
            // one complete seeded build produced the published frame.
            assert(predictiveRuntime.hasHeldValue(track, lane));
            assert(!ccRuntime.hasHeldValue(track, lane));
        }
    }

    service.update(
        snapshot, 0U, true, nowUs + residualUs - 1U, tickPeriodUs, false,
        laneSnapshot, &projectTracks, true
    );
    assert(midiQueue.size() == 0U);
    service.update(
        snapshot, 0U, true, nowUs + residualUs, tickPeriodUs, false,
        laneSnapshot, &projectTracks, true
    );
    assert(midiQueue.size() == 64U);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, nowUs + residualUs, UINT32_MAX);
    assert(transport.messages.size() == 64U);
    assert(midiQueue.size() == 0U);

    std::cout
        << "[BENCH] 16 Tracks x 4 Lanes, -100 ms @300 BPM: host single pass="
        << singlePassUs << " us (device p95/max via sequencer.playback)\n"
        << "[PASS] one predictive CC build/tick, 64 deadlines on time\n";
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
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

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

    SequencerTrackFixturePlaybackAdapter service{
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
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{midiQueue};

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

    SequencerTrackFixturePlaybackAdapter service{
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
    auto projectTracks = makeProjectTracks();
    service.update(
        valid, 0, true, 1000, 1000, false, laneSnapshot, &projectTracks
    );
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    drainDue(midiQueue, midi, 1000, UINT32_MAX);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].channel == 0);
    assert(transport.messages[0].value == 91);
    transport.messages.clear();

    // The Track route becomes unassigned while the lane is holding a value.
    // A canonical NO_ROUTE frame must replace the old channel-1 author.
    projectTracks.midiChannels[0] =
        shared::MidiCcDestinationIdentity::INVALID_CHANNEL;
    ++projectTracks.revision;
    auto& unassigned = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    laneSnapshot = snapshotBank.laneSnapshot(snapshotBank.activeIndex());
    service.update(
        unassigned, 1, true, 2000, 1000, false, laneSnapshot, &projectTracks
    );
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
    service.update(
        pinned, 2, true, 3000, 1000, false, laneSnapshot, &projectTracks
    );
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
    test_canonical_project_track_contract_is_the_only_routing_authority();
    test_project_track_note_delay_positive_and_predictive_negative();
    test_negative_delay_tempo_change_rebuilds_future_plan_once();
    test_delay_change_during_active_gate_panics_then_resyncs();
    test_graph_revision_change_resyncs_playback_service_graph();
    test_playback_service_uses_one_shot_prelude_then_internal_loop();
    test_staged_track_applies_at_first_region_loop_start_after_prelude();
    test_muted_track_does_not_emit_note_events();
    test_audible_track_applies_at_local_loop_boundary_and_cancels_old_midi_plan();
    test_transport_stop_applies_staged_track_on_first_scheduler_boundary();
    test_inactive_target_applies_on_first_staged_update();
    test_multi_track_activation_uses_each_tracks_local_loop_boundary();
    test_cc_lane_runtime_is_integrated_once_per_tick_before_note_on();
    test_project_track_mute_and_solo_filter_note_and_cc_emission();
    test_project_track_unmute_resumes_current_phase_without_history_replay();
    test_active_track_switch_does_not_panic_or_reset_playing_track();
    test_track_three_channel_five_cc74_precedes_note_on();
    test_negative_cc_lookahead_crosses_uint32_tick_wrap();
    test_failed_negative_cc_projection_falls_back_due_now();
    test_sixteen_track_negative_cc_lookahead_uses_one_complete_pass();
    test_track_paste_emits_note_and_inherited_cc_on_destination_channel();
    test_cc_lane_event_is_emitted_on_note_off_only_step();
    test_editing_future_cc_step_waits_for_playhead();
    test_negative_project_delay_predicts_cc_lane_without_advancing_live_hold();
    test_transport_stop_keeps_lane_winner_without_macro_fallback_or_reemit();
    test_unassigned_inherited_route_replaces_valid_hold_without_stale_cc();

    std::cout << "All SequencerPlaybackService tests passed\n";
    return 0;
}

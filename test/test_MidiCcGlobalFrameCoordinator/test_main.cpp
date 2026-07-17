#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/time/Time.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"

namespace {

using core::handler::MidiCcGlobalFrameCoordinator;
using core::handler::MidiCcGlobalFrameStatus;
using core::sequencer::RealtimeMidiEvent;
using core::sequencer::RealtimeMidiEventType;
using core::sequencer::RealtimeMidiQueue;
using core::sequencer::SequencerCcLaneRuntimeFrame;
using core::state::shared::MidiCcAuthor;
using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestination;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcResolutionMode;
using core::state::shared::MidiCcResolutionTelemetry;
using core::state::shared::MidiCcResolveStatus;
using core::state::shared::MidiCcRouteValidity;
namespace mod = core::state::modulation;

uint32_t fakeMicros = 0;

class MockMidiTransport final : public oc::interface::IMidi {
public:
    struct Message {
        RealtimeMidiEventType type = RealtimeMidiEventType::NoteOff;
        uint8_t channel = 0;
        uint8_t data1 = 0;
        uint8_t data2 = 0;
    };

    oc::type::Result<void> init() override {
        return oc::type::Result<void>::ok();
    }
    void update() override {}
    void sendCC(uint8_t channel, uint8_t controller, uint8_t value) override {
        messages.push_back({
            RealtimeMidiEventType::ControlChange,
            channel,
            controller,
            value,
        });
    }
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({
            RealtimeMidiEventType::NoteOn,
            channel,
            note,
            velocity,
        });
    }
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({
            RealtimeMidiEventType::NoteOff,
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

    void setOnCC(CCCallback callback) override { onCc = std::move(callback); }
    void setOnNoteOn(NoteCallback callback) override {
        onNoteOn = std::move(callback);
    }
    void setOnNoteOff(NoteCallback callback) override {
        onNoteOff = std::move(callback);
    }
    void setOnSysEx(SysExCallback callback) override {
        onSysEx = std::move(callback);
    }
    void setOnClock(ClockCallback callback) override {
        onClock = std::move(callback);
    }
    void setOnStart(RealtimeCallback callback) override {
        onStart = std::move(callback);
    }
    void setOnStop(RealtimeCallback callback) override {
        onStop = std::move(callback);
    }
    void setOnContinue(RealtimeCallback callback) override {
        onContinue = std::move(callback);
    }

    std::vector<Message> messages;
    CCCallback onCc;
    NoteCallback onNoteOn;
    NoteCallback onNoteOff;
    SysExCallback onSysEx;
    ClockCallback onClock;
    RealtimeCallback onStart;
    RealtimeCallback onStop;
    RealtimeCallback onContinue;
};

MidiCcCandidate candidate(
    MidiCcCandidateClass candidateClass,
    uint16_t stableAddress,
    uint8_t value,
    uint8_t channel = 1,
    uint8_t controller = 74,
    MidiCcRouteValidity validity = MidiCcRouteValidity::VALID,
    uint8_t port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT
) {
    return MidiCcCandidate{
        .destination = MidiCcDestination{
            .identity = MidiCcDestinationIdentity{
                .port = port,
                .channel = channel,
                .controller = controller,
            },
            .routeValidity = validity,
        },
        .author = MidiCcAuthor{
            .candidateClass = candidateClass,
            .stableAddress = stableAddress,
        },
        .localValue = value,
    };
}

SequencerCcLaneRuntimeFrame laneFrame(
    const MidiCcCandidate* candidates,
    uint8_t count
) {
    SequencerCcLaneRuntimeFrame frame{};
    frame.candidateCount = count;
    for (uint8_t i = 0; i < count; ++i) frame.candidates[i] = candidates[i];
    return frame;
}

RealtimeMidiEvent noteOff(uint16_t ordinal, uint32_t deadlineUs = 1000) {
    return RealtimeMidiEvent{
        .deadlineUs = deadlineUs,
        .type = RealtimeMidiEventType::NoteOff,
        .channel = static_cast<uint8_t>((ordinal / 128U) % 16U),
        .note = static_cast<uint8_t>(ordinal % 128U),
        .velocity = 0,
        .trackIndex = 15,
    };
}

void addNoteOffs(RealtimeMidiQueue& queue, uint16_t count) {
    for (uint16_t i = 0; i < count; ++i) assert(queue.push(noteOff(i)));
}

void drain(
    RealtimeMidiQueue& queue,
    oc::api::MidiAPI& midi,
    uint32_t deadlineUs
) {
    fakeMicros = deadlineUs;
    queue.drainDue(midi, fakeMicros, 100000);
}

void test_one_global_frame_uses_manual_lane_macro_priority() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    std::array<MidiCcCandidate, 2> persistent{
        candidate(MidiCcCandidateClass::MACRO_COMPUTED, 2, 20),
        candidate(MidiCcCandidateClass::LIVE_MANUAL, 4, 90),
    };
    MidiCcCandidate lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        3,
        70
    );
    auto lanes = laneFrame(&lane, 1);
    assert(coordinator.publishPersistentAuthors(
        persistent.data(),
        persistent.size()
    ));
    assert(coordinator.publishSequencerLanes(lanes));

    auto result = coordinator.resolveLive(1000);
    assert(result.status == MidiCcGlobalFrameStatus::OK);
    assert(result.candidateCount == 3);
    assert(result.destinationCount == 1);
    assert(result.conflictCount == 1);
    assert(result.queuedEmissionCount == 1);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               MidiCcCandidateClass::LIVE_MANUAL);
        assert(telemetry->destinations[0].finalValue == 90);
        assert(telemetry->destinations[0].loserCount == 2);
        assert(telemetry->losers[0].author.candidateClass ==
               MidiCcCandidateClass::SEQUENCER_CC_LANE);
        assert(telemetry->losers[1].author.candidateClass ==
               MidiCcCandidateClass::MACRO_COMPUTED);
    }
    drain(queue, midi, 1000);
    assert(transport.messages.back().data2 == 90);

    // Manual remains durable while another source changes. Resume is modeled
    // only by publishing a complete persistent frame without that candidate.
    lane.localValue = 71;
    lanes = laneFrame(&lane, 1);
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(2000);
    assert(result.queuedEmissionCount == 0);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].finalValue == 90);
    }
    assert(coordinator.resolveLive(2000).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);

    assert(coordinator.publishPersistentAuthors(&persistent[0], 1));
    result = coordinator.resolveLive(3000);
    assert(result.queuedEmissionCount == 1);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               MidiCcCandidateClass::SEQUENCER_CC_LANE);
        assert(telemetry->destinations[0].finalValue == 71);
    }
    drain(queue, midi, 3000);

    lanes = {};
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(4000);
    assert(result.queuedEmissionCount == 1);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               MidiCcCandidateClass::MACRO_COMPUTED);
        assert(telemetry->destinations[0].finalValue == 20);
    }

    std::cout << "[PASS] one global Manual/Lane/Macro frame\n";
}

void test_physical_dispatch_cache_and_removed_pending_retry() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    MidiCcCandidate lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        12,  // Track 3, Lane 0.
        64
    );
    auto lanes = laneFrame(&lane, 1);
    assert(coordinator.publishSequencerLanes(lanes));
    auto result = coordinator.resolveLive(5000);
    assert(result.queuedEmissionCount == 1);

    // Fill every remaining place, then one critical NoteOff must displace the
    // still-pending CC. It was accepted but never reached MidiAPI.
    addNoteOffs(
        queue,
        static_cast<uint16_t>(queue.capacity() - 1U)
    );
    const auto finalNoteOff = noteOff(
        static_cast<uint16_t>(queue.capacity() - 1U)
    );
    const auto displacement = queue.pushBatch(&finalNoteOff, 1);
    assert(displacement.ok());
    assert(displacement.displacedControlChangeCount == 1);
    assert(coordinator.needsLiveResolution());
    assert(coordinator.diagnostics().pendingRemovalRetryCount == 1);

    queue.clear();
    result = coordinator.resolveLive(6000);
    assert(result.queuedEmissionCount == 1);
    drain(queue, midi, 6000);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type == RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].data2 == 64);

    // A genuinely dispatched, unchanged value is suppressed on the next
    // source revision; changing the value emits once.
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(7000);
    assert(result.eligibleEmissionCount == 1);
    assert(result.queuedEmissionCount == 0);
    assert(queue.size() == 0);

    lane.localValue = 65;
    lanes = laneFrame(&lane, 1);
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(8000);
    assert(result.queuedEmissionCount == 1);
    assert(queue.cancelPendingEvents(3) == 1);
    assert(coordinator.needsLiveResolution());
    result = coordinator.resolveLive(8100);
    assert(result.queuedEmissionCount == 1);

    queue.clear();
    assert(coordinator.needsLiveResolution());
    result = coordinator.resolveLive(8200);
    assert(result.queuedEmissionCount == 1);

    coordinator.resetProject();
    assert(queue.size() == 0);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->candidateCount == 0);
    }
    assert(!coordinator.needsLiveResolution());
    assert(coordinator.resolveLive(9000).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);

    std::cout << "[PASS] pending removal retries and dispatch cache\n";
}

void test_project_trigger_bus_observes_only_physical_note_edges() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    mod::ProjectModulationTriggerFrame frame{};

    const RealtimeMidiEvent firstOn{
        .deadlineUs = 1000U,
        .type = RealtimeMidiEventType::NoteOn,
        .channel = 4U,
        .note = 60U,
        .velocity = 99U,
        .trackIndex = 3U,
    };
    assert(queue.push(firstOn));
    assert(!coordinator.hasPendingProjectModulationTriggers());
    assert(coordinator.diagnostics().capturedProjectTriggerEventCount == 0U);
    drain(queue, midi, 1000U);
    assert(coordinator.hasPendingProjectModulationTriggers());
    assert(coordinator.drainProjectModulationTriggers(frame) == 1U);
    assert(frame.count == 1U);
    assert(frame.events[0].trigger.kind == mod::ModulationTriggerKind::TRACK_NOTE);
    assert(frame.events[0].trigger.track == 3U);
    assert(frame.events[0].trigger.channel == 4U);
    assert(frame.events[0].trigger.data == 60U);
    assert(frame.events[0].edge == mod::ProjectModulationTriggerEdge::GATE_ON);
    assert(frame.events[0].velocity == 99U);
    assert(!coordinator.hasPendingProjectModulationTriggers());

    const std::array<RealtimeMidiEvent, 2> offEdges{{
        RealtimeMidiEvent{
            .deadlineUs = 1001U,
            .type = RealtimeMidiEventType::NoteOff,
            .channel = 4U,
            .note = 60U,
            .velocity = 17U,
            .trackIndex = 3U,
        },
        RealtimeMidiEvent{
            .deadlineUs = 1002U,
            .type = RealtimeMidiEventType::NoteOn,
            .channel = 5U,
            .note = 61U,
            .velocity = 0U,
            .trackIndex = 2U,
        },
    }};
    assert(queue.pushBatch(offEdges.data(), offEdges.size()).ok());
    drain(queue, midi, 1002U);
    assert(coordinator.drainProjectModulationTriggers(frame) == 2U);
    assert(frame.events[0].edge == mod::ProjectModulationTriggerEdge::GATE_OFF);
    assert(frame.events[0].velocity == 17U);
    assert(frame.events[1].edge == mod::ProjectModulationTriggerEdge::GATE_OFF);
    assert(frame.events[1].trigger.track == 2U);

    for (uint16_t index = 0U;
         index < mod::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY + 1U;
         ++index) {
        const RealtimeMidiEvent event{
            .deadlineUs = 2000U,
            .type = RealtimeMidiEventType::NoteOn,
            .channel = static_cast<uint8_t>(index % 16U),
            .note = static_cast<uint8_t>(index % 128U),
            .velocity = 100U,
            .trackIndex = static_cast<uint8_t>((index / 16U) % 16U),
        };
        assert(queue.push(event));
    }
    drain(queue, midi, 2000U);
    assert(coordinator.diagnostics().projectTriggerEventOverflowCount == 1U);
    assert(coordinator.diagnostics().capturedProjectTriggerEventCount == 259U);
    assert(coordinator.drainProjectModulationTriggers(frame) ==
           mod::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY);
    assert(!coordinator.hasPendingProjectModulationTriggers());

    assert(queue.push(firstOn));
    drain(queue, midi, 2000U);
    assert(coordinator.hasPendingProjectModulationTriggers());
    coordinator.resetProject();
    assert(!coordinator.hasPendingProjectModulationTriggers());
    assert(coordinator.diagnostics().projectTriggerEventOverflowCount == 0U);
    assert(coordinator.diagnostics().capturedProjectTriggerEventCount == 0U);

    std::cout << "[PASS] physical Project trigger bus and bounded overflow\n";
}

void test_queue_rejection_keeps_old_generation_and_telemetry_atomic() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};

    MidiCcCandidate initial = candidate(
        MidiCcCandidateClass::MACRO_STATIC,
        0,
        10,
        0,
        10
    );
    assert(coordinator.publishPersistentAuthors(&initial, 1));
    auto result = coordinator.resolveLive(10000);
    assert(result.queuedEmissionCount == 1);
    addNoteOffs(
        queue,
        static_cast<uint16_t>(queue.capacity() - 1U)
    );
    assert(queue.size() == queue.capacity());

    const std::array<MidiCcCandidate, 2> replacement{
        candidate(MidiCcCandidateClass::MACRO_STATIC, 0, 11, 0, 10),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 1, 12, 0, 11),
    };
    assert(coordinator.publishPersistentAuthors(
        replacement.data(),
        replacement.size()
    ));
    result = coordinator.resolveLive(11000);
    assert(result.status == MidiCcGlobalFrameStatus::QUEUE_REJECTED);
    assert(result.queueStatus ==
           core::sequencer::RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED);
    assert(queue.size() == queue.capacity());
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->candidateCount == 1);
        assert(telemetry->destinationCount == 1);
        assert(telemetry->destinations[0].finalValue == 10);
    }
    assert(coordinator.needsLiveResolution());

    queue.clear();
    result = coordinator.resolveLive(12000);
    assert(result.status == MidiCcGlobalFrameStatus::OK);
    assert(result.queuedEmissionCount == 2);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->candidateCount == 2);
        assert(telemetry->destinationCount == 2);
    }
    assert(coordinator.diagnostics().queueRejectedFrameCount == 1);

    std::cout << "[PASS] rejected queue frame is fully atomic\n";
}

void test_strict_source_validation() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};

    const MidiCcCandidate macro = candidate(
        MidiCcCandidateClass::MACRO_COMPUTED,
        0,
        42
    );
    assert(coordinator.publishPersistentAuthors(&macro, 1));
    const auto live = coordinator.resolveLive(13000);
    assert(live.status == MidiCcGlobalFrameStatus::OK);
    assert(live.destinationCount == 1);

    MidiCcCandidate invalid = macro;
    invalid.author.candidateClass = MidiCcCandidateClass::SEQUENCER_CC_LANE;
    assert(!coordinator.publishPersistentAuthors(&invalid, 1));
    invalid = macro;
    invalid.destination.identity.port = 7;
    assert(!coordinator.publishPersistentAuthors(&invalid, 1));
    invalid = macro;
    invalid.localValue = 255;
    assert(!coordinator.publishPersistentAuthors(&invalid, 1));
    assert(!coordinator.publishPersistentAuthors(
        nullptr,
        core::handler::MidiCcPersistentAuthorFrame::MAX_CANDIDATES + 1U
    ));

    auto badLaneFrame = laneFrame(&macro, 1);
    assert(!coordinator.publishSequencerLanes(badLaneFrame));
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->mode == MidiCcResolutionMode::LIVE);
        assert(telemetry->candidateCount == 1);
    }

    std::cout << "[PASS] source validation is strict\n";
}

void test_telemetry_view_is_stable_across_realtime_publications() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};

    MidiCcCandidate macro = candidate(
        MidiCcCandidateClass::MACRO_COMPUTED,
        0,
        10
    );
    assert(coordinator.publishPersistentAuthors(&macro, 1));
    assert(coordinator.resolveLive(14000).ok());

    auto held = coordinator.readTelemetry();
    assert(held);
    assert(held->destinations[0].finalValue == 10);
    MidiCcResolutionTelemetry snapshot = *held;

    // A second UI consumer must fail closed without stealing the protected
    // frame. The realtime owner can still publish indefinitely through the
    // other two frames; the held view remains byte-stable.
    assert(!coordinator.readTelemetry());
    for (uint8_t value : {20, 30, 40}) {
        macro.localValue = value;
        assert(coordinator.publishPersistentAuthors(&macro, 1));
        assert(coordinator.resolveLive(14000U + value).ok());
        assert(std::memcmp(
            held.get(),
            &snapshot,
            sizeof(snapshot)
        ) == 0);
    }
    assert(!coordinator.readTelemetry());

    held = {};
    auto latest = coordinator.readTelemetry();
    assert(latest);
    assert(latest->destinations[0].finalValue == 40);

    std::cout << "[PASS] telemetry read view is stable and non-blocking\n";
}

void test_exact_320_candidate_envelope_and_measurements() {
    static std::array<
        MidiCcCandidate,
        core::handler::MidiCcPersistentAuthorFrame::MAX_CANDIDATES
    > persistent{};
    static std::array<MidiCcCandidate, 64> laneCandidates{};
    static std::array<MidiCcCandidate, MidiCcResolutionTelemetry::MAX_CANDIDATES>
        combined{};

    for (uint16_t i = 0; i < persistent.size(); ++i) {
        persistent[i] = candidate(
            MidiCcCandidateClass::MACRO_STATIC,
            i,
            static_cast<uint8_t>(i % 128U),
            static_cast<uint8_t>(i / 128U),
            static_cast<uint8_t>(i % 128U)
        );
        combined[i] = persistent[i];
    }
    for (uint8_t i = 0; i < laneCandidates.size(); ++i) {
        laneCandidates[i] = candidate(
            MidiCcCandidateClass::SEQUENCER_CC_LANE,
            i,
            i,
            2,
            i
        );
        combined[persistent.size() + i] = laneCandidates[i];
    }

    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    const auto lanes = laneFrame(
        laneCandidates.data(),
        static_cast<uint8_t>(laneCandidates.size())
    );
    assert(coordinator.publishPersistentAuthors(
        persistent.data(),
        persistent.size()
    ));
    assert(coordinator.publishSequencerLanes(lanes));
    const auto result = coordinator.resolveLive(1000);
    assert(result.status == MidiCcGlobalFrameStatus::OK);
    assert(result.candidateCount == 320);
    assert(result.destinationCount == 320);
    assert(result.queuedEmissionCount == 320);
    assert(queue.size() == combined.size());

    MidiCcResolutionTelemetry telemetry{};
    constexpr uint16_t iterationCount = 200;
    const auto started = std::chrono::steady_clock::now();
    for (uint16_t i = 0; i < iterationCount; ++i) {
        assert(core::state::shared::resolveMidiCcDestinations(
            combined.data(),
            combined.size(),
            MidiCcResolutionMode::LIVE,
            telemetry
        ) == MidiCcResolveStatus::OK);
    }
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started
    ).count();

    constexpr size_t queueEventStorageBytes =
        RealtimeMidiQueue::MAX_QUEUE_DEPTH * sizeof(RealtimeMidiEvent);
    static_assert(queueEventStorageBytes == 3936U);
    static_assert(
        oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES == 8U
    );
    std::cout << "[MEASURE] sizeof RealtimeMidiEvent="
              << sizeof(RealtimeMidiEvent)
              << ", queue event storage=" << queueEventStorageBytes
              << ", sizeof RealtimeMidiQueue=" << sizeof(RealtimeMidiQueue)
              << "\n";
    std::cout << "[MEASURE] sizeof MidiCcGlobalFrameCoordinator="
              << sizeof(MidiCcGlobalFrameCoordinator)
              << ", MidiCcResolutionTelemetry="
              << sizeof(MidiCcResolutionTelemetry)
              << ", SequencerCcLaneRuntime="
              << sizeof(core::sequencer::SequencerCcLaneRuntime)
              << ", SequencerCcLaneBank="
              << sizeof(core::state::sequencer::SequencerCcLaneBank)
              << "\n";
    std::cout << "[MEASURE] resolver 320 candidates average="
              << static_cast<double>(elapsedUs) / iterationCount
              << " us (host -O2, informational)\n";
    std::cout << "[PASS] exact 128 Off + 64 CC + 128 On envelope\n";
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    oc::time::setMicrosProvider([]() { return fakeMicros; });

    test_one_global_frame_uses_manual_lane_macro_priority();
    test_physical_dispatch_cache_and_removed_pending_retry();
    test_project_trigger_bus_observes_only_physical_note_edges();
    test_queue_rejection_keeps_old_generation_and_telemetry_atomic();
    test_strict_source_validation();
    test_telemetry_view_is_stable_across_realtime_publications();
    test_exact_320_candidate_envelope_and_measurements();

    std::cout << "All MidiCcGlobalFrameCoordinator tests passed\n";
    return 0;
}

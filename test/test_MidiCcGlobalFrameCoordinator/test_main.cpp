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
#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "state/macro/MacroConstants.hpp"
#include "support/ProjectTrackRuntimeSnapshotTestFixture.hpp"

namespace {

using core::handler::MidiCcGlobalFrameCoordinator;
using core::handler::MidiCcGlobalFrameStatus;
using core::sequencer::RealtimeMidiEvent;
using core::sequencer::RealtimeMidiEventType;
using core::sequencer::RealtimeMidiQueue;
using core::sequencer::SequencerCcLaneRuntimeFrame;
using core::sequencer::ProjectTrackRuntimeSnapshot;
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
        .trackIndex = 15,
        .channel = static_cast<uint8_t>((ordinal / 128U) % 16U),
        .note = static_cast<uint8_t>(ordinal % 128U),
        .velocity = 0,
    };
}

void addNoteOffs(RealtimeMidiQueue& queue, uint16_t count) {
    for (uint16_t i = 0; i < count; ++i) assert(queue.push(noteOff(i)));
}

ProjectTrackRuntimeSnapshot projectTracks() {
    return test_support::makeAllAudibleProjectTrackRuntimeSnapshot();
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

    auto result = coordinator.resolveLive(1000, projectTracks());
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
    result = coordinator.resolveLive(2000, projectTracks());
    assert(result.queuedEmissionCount == 0);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->destinations[0].finalValue == 90);
    }
    assert(coordinator.resolveLive(2000, projectTracks()).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);

    assert(coordinator.publishPersistentAuthors(&persistent[0], 1));
    result = coordinator.resolveLive(3000, projectTracks());
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
    result = coordinator.resolveLive(4000, projectTracks());
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
    auto result = coordinator.resolveLive(5000, projectTracks());
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
    assert(coordinator.needsLiveResolution(5000U));
    assert(coordinator.diagnostics().pendingRemovalRetryCount == 1);

    queue.clear();
    result = coordinator.resolveLive(6000, projectTracks());
    assert(result.queuedEmissionCount == 1);
    drain(queue, midi, 6000);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type == RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].data2 == 64);

    // An identical Lane frame is publication-deduplicated entirely; changing
    // the value below publishes and emits exactly once.
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(7000, projectTracks());
    assert(result.status == MidiCcGlobalFrameStatus::NO_CHANGE);
    assert(result.queuedEmissionCount == 0);
    assert(queue.size() == 0);

    lane.localValue = 65;
    lanes = laneFrame(&lane, 1);
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(8000, projectTracks());
    assert(result.queuedEmissionCount == 1);
    assert(queue.cancelPendingEvents(3) == 1);
    assert(coordinator.needsLiveResolution(8000U));
    result = coordinator.resolveLive(8100, projectTracks());
    assert(result.queuedEmissionCount == 1);

    queue.clear();
    assert(coordinator.needsLiveResolution(8100U));
    result = coordinator.resolveLive(8200, projectTracks());
    assert(result.queuedEmissionCount == 1);

    coordinator.resetProject();
    assert(queue.size() == 0);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry);
        assert(telemetry->candidateCount == 0);
    }
    assert(!coordinator.needsLiveResolution(9000U));
    assert(coordinator.resolveLive(9000, projectTracks()).status ==
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
        .trackIndex = 3U,
        .channel = 4U,
        .note = 60U,
        .velocity = 99U,
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
            .trackIndex = 3U,
            .channel = 4U,
            .note = 60U,
            .velocity = 17U,
        },
        RealtimeMidiEvent{
            .deadlineUs = 1002U,
            .type = RealtimeMidiEventType::NoteOn,
            .trackIndex = 2U,
            .channel = 5U,
            .note = 61U,
            .velocity = 0U,
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
            .trackIndex = static_cast<uint8_t>((index / 16U) % 16U),
            .channel = static_cast<uint8_t>(index % 16U),
            .note = static_cast<uint8_t>(index % 128U),
            .velocity = 100U,
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
    auto result = coordinator.resolveLive(10000, projectTracks());
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
    result = coordinator.resolveLive(11000, projectTracks());
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
    assert(coordinator.needsLiveResolution(11000U));

    queue.clear();
    result = coordinator.resolveLive(12000, projectTracks());
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
    const auto live = coordinator.resolveLive(13000, projectTracks());
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
    const std::array duplicateBase{
        candidate(MidiCcCandidateClass::MACRO_COMPUTED, 1U, 10U),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 1U, 11U),
    };
    assert(!coordinator.publishPersistentAuthors(
        duplicateBase.data(),
        duplicateBase.size()
    ));
    const std::array manualAndBase{
        candidate(MidiCcCandidateClass::LIVE_MANUAL, 1U, 10U),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 1U, 11U),
    };
    assert(coordinator.publishPersistentAuthors(
        manualAndBase.data(),
        manualAndBase.size()
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
    assert(coordinator.resolveLive(14000, projectTracks()).ok());

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
        assert(coordinator.resolveLive(14000U + value, projectTracks()).ok());
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

void test_reset_preserves_held_telemetry_view_until_exact_raii_release() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};

    auto macro = candidate(
        MidiCcCandidateClass::MACRO_COMPUTED,
        0U,
        37U
    );
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    assert(coordinator.resolveLive(15000U, projectTracks()).ok());

    auto held = coordinator.readTelemetry();
    assert(held && held->destinations[0].finalValue == 37U);
    const MidiCcResolutionTelemetry snapshot = *held;

    coordinator.resetProject();
    assert(std::memcmp(held.get(), &snapshot, sizeof(snapshot)) == 0);
    // Reset published a different zero frame but must not free the old reader
    // slot. A future acquisition stays blocked until this exact RAII lease ends.
    assert(!coordinator.readTelemetry());

    held = {};
    {
        auto zero = coordinator.readTelemetry();
        assert(zero);
        assert(zero->candidateCount == 0U);
        assert(zero->destinationCount == 0U);
    }

    macro.localValue = 51U;
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    assert(coordinator.resolveLive(16000U, projectTracks()).ok());
    auto latest = coordinator.readTelemetry();
    assert(latest && latest->destinations[0].finalValue == 51U);

    std::cout << "[PASS] reset preserves held telemetry until RAII release\n";
}

void test_identical_lane_frame_is_not_republished_or_reinvalidated() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    auto lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        0U,
        63U,
        0U
    );
    auto lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 7U;
    lanes.predictiveAuthorMask = UINT64_C(1);
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1U);
    assert(coordinator.resolveLive(17000U, projectTracks()).ok());
    const auto resolved = coordinator.diagnostics().resolvedLiveFrameCount;
    const auto invalidated =
        coordinator.diagnostics().laneGenerationInvalidationCount;

    // Contributions are UI/runtime diagnostics and are deliberately not part
    // of the coordinator's source semantics.
    lanes.contributions[0].heldValue = 99U;
    lanes.contributions[0].authoredEventThisTick = true;
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.diagnostics().publishedLaneFrameCount == 1U);
    assert(coordinator.resolveLive(17001U, projectTracks()).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);
    assert(coordinator.diagnostics().resolvedLiveFrameCount == resolved);
    assert(coordinator.diagnostics().laneGenerationInvalidationCount ==
           invalidated);

    std::cout << "[PASS] identical Lane frame keeps revision and lifecycle stable\n";
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
    const auto result = coordinator.resolveLive(1000, projectTracks());
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
    static_assert(queueEventStorageBytes == 4608U);
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
    std::cout << "[PASS] full 320-destination CC resolver envelope\n";
}

void test_temporal_authors_are_arbitrated_only_at_physical_deadline() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();
    tracks.delayMs[0] = 50;

    const std::array persistent{
        candidate(MidiCcCandidateClass::MACRO_COMPUTED, 0U, 20U, 0U),
        candidate(MidiCcCandidateClass::LIVE_MANUAL, 0U, 90U, 0U),
    };
    assert(coordinator.publishPersistentAuthors(
        persistent.data(),
        persistent.size()
    ));
    auto result = coordinator.resolveLive(1000U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 0U);
    assert(queue.size() == 0U);
    result = coordinator.resolveLive(50999U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 0U);
    result = coordinator.resolveLive(51000U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 1U);
    {
        auto telemetry = coordinator.readTelemetry();
        assert(telemetry && telemetry->candidateCount == 2U);
        assert(telemetry->destinations[0].winner.author.candidateClass ==
               MidiCcCandidateClass::LIVE_MANUAL);
        assert(telemetry->destinations[0].finalValue == 90U);
    }
    drain(queue, midi, 51000U);
    assert(transport.messages.back().data2 == 90U);

    std::cout << "[PASS] positive delay temporalizes authors before arbitration\n";
}

void test_negative_delay_is_lane_predictive_only() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    auto tracks = projectTracks();
    tracks.delayMs[0] = -50;

    const auto macro = candidate(
        MidiCcCandidateClass::MACRO_STATIC,
        0U,
        10U,
        0U
    );
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    auto result = coordinator.resolveLive(1000U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 1U);  // Macro clamps to zero delay.
    queue.clear();
    coordinator.resetProject();

    const auto lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        0U,
        77U,
        0U
    );
    auto lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 1U;
    lanes.predictiveAuthorMask = UINT64_C(1);
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(2000U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 0U);
    // ceil(50/20)=3 ticks: future event is scheduled at +60-50=+10ms.
    result = coordinator.resolveLive(11999U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 0U);
    result = coordinator.resolveLive(12000U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 1U);

    queue.clear();
    coordinator.resetProject();
    lanes.predictiveAuthorMask = 0U;
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(13000U, tracks, 20000U, false);
    assert(result.queuedEmissionCount == 1U);  // External clock clamps to now.

    queue.clear();
    coordinator.resetProject();
    assert(coordinator.publishSequencerLanes(lanes));
    result = coordinator.resolveLive(14000U, tracks, 20000U, true);
    assert(result.queuedEmissionCount == 1U);  // Current fallback has no residual.

    std::cout
        << "[PASS] negative residual requires explicit projected-Lane metadata\n";
}

void test_lane_lifecycle_generation_cancels_old_future_mutation() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    auto tracks = projectTracks();
    tracks.delayMs[0] = 100;

    auto lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        0U,
        40U,
        0U
    );
    auto lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 1U;
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(1000U, tracks).queuedEmissionCount == 0U);

    // Paste/Undo/restore publishes a new lifecycle before generation 1 is due.
    lane.localValue = 80U;
    lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 2U;
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(51000U, tracks).queuedEmissionCount == 0U);
    assert(coordinator.resolveLive(101000U, tracks).queuedEmissionCount == 0U);
    const auto result = coordinator.resolveLive(151000U, tracks);
    assert(result.queuedEmissionCount == 1U);
    assert(queue.size() == 1U);
    assert(coordinator.diagnostics().laneGenerationInvalidationCount == 1U);

    std::cout << "[PASS] Lane lifecycle cancels stale delayed generation\n";
}

void test_track_invalidation_removes_audibility_immediately() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();
    const auto macro = candidate(
        MidiCcCandidateClass::MACRO_STATIC,
        0U,
        55U,
        0U
    );
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    assert(coordinator.resolveLive(1000U, tracks).queuedEmissionCount == 1U);
    drain(queue, midi, 1000U);

    tracks.audibleMask = static_cast<uint16_t>(tracks.audibleMask & ~1U);
    coordinator.invalidateTrack(0U);
    const auto result = coordinator.resolveLive(1001U, tracks);
    assert(result.ok());
    auto telemetry = coordinator.readTelemetry();
    assert(telemetry && telemetry->candidateCount == 0U);
    assert(queue.size() == 0U);

    std::cout << "[PASS] Track audibility invalidation is immediate\n";
}

void test_route_and_delay_change_cancel_old_plan_and_rebuild() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();
    tracks.midiChannels[0] = 2U;
    auto macro = candidate(
        MidiCcCandidateClass::MACRO_STATIC,
        0U,
        61U,
        2U
    );
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    assert(coordinator.resolveLive(1000U, tracks).queuedEmissionCount == 1U);
    drain(queue, midi, 1000U);
    assert(transport.messages.back().data1 == 74U);
    assert(transport.messages.back().type == RealtimeMidiEventType::ControlChange);
    assert(transport.messages.back().data2 == 61U);

    tracks.midiChannels[0] = 3U;
    tracks.delayMs[0] = 50;
    macro.destination.identity.channel = 3U;
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    coordinator.invalidateTrack(0U);
    auto result = coordinator.resolveLive(2000U, tracks);
    assert(result.queuedEmissionCount == 0U);
    assert(coordinator.resolveLive(51999U, tracks).queuedEmissionCount == 0U);
    result = coordinator.resolveLive(52000U, tracks);
    assert(result.queuedEmissionCount == 1U);
    drain(queue, midi, 52000U);
    assert(transport.messages.back().channel == 3U);
    assert(transport.messages.back().data2 == 61U);

    std::cout << "[PASS] route/Delay invalidation cancels and rebuilds generation\n";
}

void test_pinned_lane_channel_survives_project_track_snapshot_and_delay() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();
    tracks.midiChannels[0] = 2U;
    auto pinned = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        0U,
        64U,
        6U,
        74U
    );
    auto lanes = laneFrame(&pinned, 1U);
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(1000U, tracks).queuedEmissionCount == 1U);
    drain(queue, midi, 1000U);
    assert(transport.messages.back().channel == 6U);

    tracks.midiChannels[0] = 3U;
    tracks.delayMs[0] = 50;
    coordinator.invalidateTrack(0U);
    assert(coordinator.resolveLive(2000U, tracks).queuedEmissionCount == 0U);
    assert(coordinator.resolveLive(52000U, tracks).queuedEmissionCount == 1U);
    drain(queue, midi, 52000U);
    assert(transport.messages.back().channel == 6U);

    std::cout << "[PASS] pinned Lane route survives Track snapshot/invalidation\n";
}

void test_transport_stop_cancels_future_lane_without_fallback_reemit() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();
    tracks.delayMs[0] = 100;
    const auto macro = candidate(
        MidiCcCandidateClass::MACRO_STATIC,
        0U,
        20U,
        0U
    );
    auto lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        0U,
        90U,
        0U
    );
    auto lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 1U;
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    assert(coordinator.publishSequencerLanes(lanes));
    // Both authors inherit +100 ms, then Lane wins at physical time.
    assert(coordinator.resolveLive(1000U, tracks).queuedEmissionCount == 0U);
    assert(coordinator.resolveLive(101000U, tracks).queuedEmissionCount == 1U);
    drain(queue, midi, 101000U);
    assert(transport.messages.back().data2 == 90U);

    // Schedule a future Lane value without replacing the effective hold, then
    // stop before its physical deadline.
    lane.localValue = 91U;
    lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 1U;
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(102000U, tracks).queuedEmissionCount == 0U);
    coordinator.discardPendingRetryForTransportStop();
    coordinator.publishProjectControlClock(0U, false, 102001U, 1000U);
    assert(coordinator.resolveLive(202000U, tracks).queuedEmissionCount == 0U);
    assert(transport.messages.size() == 1U);

    // The resume boundary re-stages the semantically identical source without
    // forcing a fake publication revision, then establishes a new +100 ms plan.
    coordinator.publishProjectControlClock(1U, true, 203000U, 1000U);
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(203000U, tracks).queuedEmissionCount == 0U);
    assert(coordinator.resolveLive(303000U, tracks).queuedEmissionCount == 1U);

    std::cout << "[PASS] transport stop cancels Lane future without fallback\n";
}

void test_transport_resume_retries_cleared_due_cc_exactly_once() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();

    auto lane = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        0U,
        90U,
        0U,
        74U
    );
    auto lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 1U;
    coordinator.publishProjectControlClock(0U, true, 1000U, 1000U);
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(1000U, tracks).queuedEmissionCount == 1U);
    drain(queue, midi, 1000U);
    assert(transport.messages.size() == 1U);
    assert(transport.messages.back().data1 == 74U);
    assert(transport.messages.back().data2 == 90U);

    // The replacement is accepted by the realtime queue but Stop wins before
    // it reaches the physical MIDI transport. queue.clear() invalidates the
    // planned cache; the explicit Stop boundary must retain value 91 as a
    // deferred intent rather than overwrite it with the dispatched value 90.
    lane.localValue = 91U;
    lanes = laneFrame(&lane, 1U);
    lanes.lifecycleGenerations[0] = 1U;
    assert(coordinator.publishSequencerLanes(lanes));
    assert(coordinator.resolveLive(2000U, tracks).queuedEmissionCount == 1U);
    assert(queue.size() == 1U);
    queue.clear();
    assert(queue.size() == 0U);
    coordinator.discardPendingRetryForTransportStop();
    coordinator.publishProjectControlClock(1U, false, 3000U, 1000U);
    assert(coordinator.resolveLive(3000U, tracks).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);
    assert(queue.size() == 0U);
    assert(transport.messages.size() == 1U);

    // Persistent Live Macro input remains usable while stopped. It must emit
    // only its own changed destination; the deferred Lane hold stays silent.
    const std::array<MidiCcCandidate, 2> manualMacro{
        candidate(MidiCcCandidateClass::MACRO_STATIC, 1U, 20U, 0U, 75U),
        candidate(MidiCcCandidateClass::LIVE_MANUAL, 1U, 77U, 0U, 75U),
    };
    assert(coordinator.publishPersistentAuthors(
        manualMacro.data(),
        manualMacro.size()
    ));
    assert(coordinator.resolveLive(4000U, tracks).queuedEmissionCount == 1U);
    assert(queue.size() == 1U);
    drain(queue, midi, 4000U);
    assert(transport.messages.size() == 2U);
    assert(transport.messages.back().data1 == 75U);
    assert(transport.messages.back().data2 == 77U);

    // Resume reconciles the preserved Lane intent against physical dispatch.
    // It is emitted once, then an identical frame remains silent.
    coordinator.publishProjectControlClock(2U, true, 5000U, 1000U);
    assert(coordinator.resolveLive(5000U, tracks).queuedEmissionCount == 1U);
    assert(queue.size() == 1U);
    drain(queue, midi, 5000U);
    assert(transport.messages.size() == 3U);
    assert(transport.messages.back().data1 == 74U);
    assert(transport.messages.back().data2 == 91U);
    assert(coordinator.resolveLive(6000U, tracks).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);
    assert(queue.size() == 0U);
    assert(transport.messages.size() == 3U);

    std::cout << "[PASS] resume retries cleared due CC exactly once\n";
}

void test_nine_due_deadline_groups_drain_across_bounded_resolve_calls() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    auto tracks = projectTracks();
    constexpr uint8_t kGroupCount = 9U;
    constexpr uint16_t kMacroAddressesPerTrack =
        core::state::macro::PAGE_COUNT * core::state::macro::MACRO_COUNT;
    std::array<MidiCcCandidate, kGroupCount> authors{};
    for (uint8_t track = 0U; track < kGroupCount; ++track) {
        tracks.delayMs[track] = static_cast<int16_t>(track + 1U);
        authors[track] = candidate(
            MidiCcCandidateClass::MACRO_STATIC,
            static_cast<uint16_t>(track * kMacroAddressesPerTrack),
            static_cast<uint8_t>(40U + track),
            track,
            static_cast<uint8_t>(20U + track)
        );
    }
    assert(coordinator.publishPersistentAuthors(
        authors.data(),
        authors.size()
    ));
    auto result = coordinator.resolveLive(0U, tracks);
    assert(result.ok());
    assert(result.queuedEmissionCount == 0U);

    // A distinct complete-frame revision arrives while all nine accepted
    // deadline groups are due. Reordering is semantically neutral to author
    // state but proves the source revision remains unconsumed until the bounded
    // drain has made room for it.
    std::reverse(authors.begin(), authors.end());
    assert(coordinator.publishPersistentAuthors(
        authors.data(),
        authors.size()
    ));
    assert(coordinator.diagnostics().publishedPersistentFrameCount == 2U);

    result = coordinator.resolveLive(10000U, tracks);
    assert(result.ok());
    assert(result.queuedEmissionCount == 8U);
    assert(queue.size() == 8U);
    assert(coordinator.diagnostics().committedDeadlineGroupCount == 8U);
    assert(coordinator.needsLiveResolution(10000U));

    result = coordinator.resolveLive(10000U, tracks);
    assert(result.ok());
    assert(result.queuedEmissionCount == 1U);
    assert(queue.size() == 9U);
    assert(coordinator.diagnostics().committedDeadlineGroupCount == 9U);
    assert(!coordinator.needsLiveResolution(10000U));
    assert(coordinator.resolveLive(10000U, tracks).status ==
           MidiCcGlobalFrameStatus::NO_CHANGE);

    drain(queue, midi, 10000U);
    assert(transport.messages.size() == kGroupCount);
    std::array<uint8_t, kGroupCount> seen{};
    for (const auto& message : transport.messages) {
        assert(message.type == RealtimeMidiEventType::ControlChange);
        assert(message.data1 >= 20U && message.data1 < 20U + kGroupCount);
        ++seen[message.data1 - 20U];
    }
    for (const auto count : seen) assert(count == 1U);

    std::cout << "[PASS] nine due groups drain 8+1 without source loss\n";
}

void test_full_temporal_spool_drains_due_before_retrying_source_revision() {
    RealtimeMidiQueue queue;
    MidiCcGlobalFrameCoordinator coordinator{queue};
    auto tracks = projectTracks();
    tracks.delayMs[0] = 100;

    auto macro = candidate(
        MidiCcCandidateClass::MACRO_STATIC,
        0U,
        0U,
        0U
    );
    for (uint32_t nowUs = 0U;
         nowUs < core::sequencer::TemporalMidiCcAuthorSpool::CAPACITY;
         ++nowUs) {
        macro.localValue = static_cast<uint8_t>(nowUs & 0x7FU);
        assert(coordinator.publishPersistentAuthors(&macro, 1U));
        const auto result = coordinator.resolveLive(nowUs, tracks);
        assert(result.ok());
        assert(result.queuedEmissionCount == 0U);
    }

    // The complete next source revision cannot be reserved while the spool is
    // full. It must remain unconsumed so a later pass can retry it atomically.
    macro.localValue = 1U;
    assert(coordinator.publishPersistentAuthors(&macro, 1U));
    auto result = coordinator.resolveLive(8192U, tracks);
    assert(result.status == MidiCcGlobalFrameStatus::TEMPORAL_REJECTED);
    assert(coordinator.diagnostics().temporalRejectedFrameCount == 1U);

    // At the first physical deadline, the accepted generation is drained
    // before the pending source revision reserves its replacement node. This
    // is the liveness guarantee that prevents permanent capacity deadlock.
    result = coordinator.resolveLive(100000U, tracks);
    assert(result.ok());
    assert(result.queuedEmissionCount == 1U);
    assert(!coordinator.needsLiveResolution(100000U));
    assert(coordinator.needsLiveResolution(200000U));

    std::cout << "[PASS] full temporal spool drains before source retry\n";
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
    test_reset_preserves_held_telemetry_view_until_exact_raii_release();
    test_identical_lane_frame_is_not_republished_or_reinvalidated();
    test_exact_320_candidate_envelope_and_measurements();
    test_temporal_authors_are_arbitrated_only_at_physical_deadline();
    test_negative_delay_is_lane_predictive_only();
    test_lane_lifecycle_generation_cancels_old_future_mutation();
    test_track_invalidation_removes_audibility_immediately();
    test_route_and_delay_change_cancel_old_plan_and_rebuild();
    test_pinned_lane_channel_survives_project_track_snapshot_and_delay();
    test_transport_stop_cancels_future_lane_without_fallback_reemit();
    test_transport_resume_retries_cleared_due_cc_exactly_once();
    test_nine_due_deadline_groups_drain_across_bounded_resolve_calls();
    test_full_temporal_spool_drains_due_before_retrying_source_revision();

    std::cout << "All MidiCcGlobalFrameCoordinator tests passed\n";
    return 0;
}

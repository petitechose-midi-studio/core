#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <oc/note/sequencer/StepSequencerExpander.hpp>

#include "sequencer/RealtimeMidiQueue.hpp"

namespace {

namespace note = oc::note::sequencer;
using core::sequencer::RealtimeMidiEvent;
using core::sequencer::RealtimeMidiEventType;
using core::sequencer::RealtimeMidiQueue;
using core::sequencer::RealtimeMidiQueueBatchStatus;

constexpr size_t TRACK_COUNT = 16U;
constexpr size_t NOTES_PER_TRACK = 16U;
constexpr size_t NOTE_EVENTS_PER_PHASE = TRACK_COUNT * NOTES_PER_TRACK;
constexpr size_t CC_EVENTS_PER_FRAME =
    RealtimeMidiQueue::MAX_RESOLVED_CC_EVENTS_PER_FRAME;
constexpr size_t PRODUCER_ENVELOPE_COUNT =
    2U * NOTE_EVENTS_PER_PHASE + CC_EVENTS_PER_FRAME;
constexpr uint32_t DEADLINE_US = 1'000'000U;

static_assert(NOTE_EVENTS_PER_PHASE == 256U);
static_assert(CC_EVENTS_PER_FRAME == 320U);
static_assert(PRODUCER_ENVELOPE_COUNT == 832U);
static_assert(RealtimeMidiQueue::NOTE_EVENT_PHASE_CAPACITY == 128U);
static_assert(RealtimeMidiQueue::MAX_QUEUE_DEPTH == 576U);

note::StepSequencerRuntimeState baseState() {
    note::StepSequencerRuntimeState state{};
    state.length = note::StepSequencerRuntimeState::MAX_STEPS;
    state.stepsPerBeat = 24U;
    state.enabledMask.setBit(0U);
    state.note[0] = 60U;
    state.velocity[0] = 100U;
    state.gate[0] = 100U;
    state.probability[0] = 100U;
    return state;
}

note::StepSequencerGraph twoChordGraph() {
    note::StepSequencerGraph graph{};
    graph.enabled = true;
    graph.rootSequenceId = 0U;
    graph.sequenceCount = 2U;
    graph.stepNodeCount = 130U;
    graph.sequences[0].kind = note::StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0U;
    graph.sequences[0].length = note::StepSequencerRuntimeState::MAX_STEPS;
    graph.sequences[1].kind = note::StepSequencerSequenceKind::MicroSequence;
    graph.sequences[1].firstStepNode = 128U;
    graph.sequences[1].length = 2U;
    graph.stepNodes[0].flags = note::STEP_NODE_CHILD_SEQUENCE;
    graph.stepNodes[0].childSequenceId = 1U;

    for (uint16_t nodeIndex : {uint16_t{128U}, uint16_t{129U}}) {
        auto& node = graph.stepNodes[nodeIndex];
        node.flags = note::STEP_NODE_CHORD_MODE | note::STEP_NODE_CHORD_LOCAL;
        node.chordMode = note::StepSequencerChordMode::Local;
        node.chordSpec = note::StepSequencerChordSpec::semantic(
            note::StepSequencerChordHarmony::Major,
            note::StepSequencerChordSpec::MAX_VOICES
        );
    }
    return graph;
}

RealtimeMidiEvent noteEvent(
    RealtimeMidiEventType type,
    uint8_t track,
    uint8_t channel,
    uint8_t midiNote,
    uint8_t velocity
) {
    RealtimeMidiEvent event{};
    event.deadlineUs = DEADLINE_US;
    event.type = type;
    event.trackIndex = track;
    event.channel = channel;
    event.note = midiNote;
    event.velocity = velocity;
    return event;
}

RealtimeMidiEvent ccEvent(size_t index) {
    RealtimeMidiEvent event{};
    event.deadlineUs = DEADLINE_US;
    event.type = RealtimeMidiEventType::ControlChange;
    event.trackIndex = static_cast<uint8_t>(index % TRACK_COUNT);
    event.channel = static_cast<uint8_t>(index % 16U);
    event.controller = static_cast<uint8_t>(index % 128U);
    event.value = static_cast<uint8_t>((index / 128U) + 1U);
    return event;
}

using ProducerEnvelope = std::array<RealtimeMidiEvent, PRODUCER_ENVELOPE_COUNT>;

ProducerEnvelope buildProducerEnvelope() {
    const auto state = baseState();
    const auto graph = twoChordGraph();
    ProducerEnvelope envelope{};

    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const auto expansion = note::StepSequencerExpander::expandRootStep(
            state,
            graph,
            0U,
            0U,
            1U,
            0x12345678U,
            true
        );
        assert(expansion.count == NOTES_PER_TRACK);
        assert(expansion.requestedNoteCount == NOTES_PER_TRACK);
        assert(!expansion.noteBudgetExceeded);

        for (uint8_t index = 0U; index < expansion.count; ++index) {
            const auto& expanded = expansion.notes[index];
            assert(expanded.localTick == 0U);
            const size_t phaseIndex =
                static_cast<size_t>(track) * NOTES_PER_TRACK + index;
            const uint8_t channel = static_cast<uint8_t>(track % 16U);
            envelope[phaseIndex] = noteEvent(
                RealtimeMidiEventType::NoteOff,
                track,
                channel,
                expanded.variation.resolved.note,
                0U
            );
            envelope[NOTE_EVENTS_PER_PHASE + CC_EVENTS_PER_FRAME + phaseIndex] =
                noteEvent(
                    RealtimeMidiEventType::NoteOn,
                    track,
                    channel,
                    expanded.variation.resolved.note,
                    expanded.variation.resolved.velocity
                );
        }
    }

    for (size_t index = 0U; index < CC_EVENTS_PER_FRAME; ++index) {
        envelope[NOTE_EVENTS_PER_PHASE + index] = ccEvent(index);
    }
    return envelope;
}

void assertExactClassesAndTracks(const ProducerEnvelope& envelope) {
    size_t noteOffCount = 0U;
    size_t controlChangeCount = 0U;
    size_t noteOnCount = 0U;
    std::array<size_t, TRACK_COUNT> noteOffsByTrack{};
    std::array<size_t, TRACK_COUNT> noteOnsByTrack{};

    for (const auto& event : envelope) {
        assert(event.deadlineUs == DEADLINE_US);
        switch (event.type) {
            case RealtimeMidiEventType::NoteOff:
                ++noteOffCount;
                ++noteOffsByTrack[event.trackIndex];
                break;
            case RealtimeMidiEventType::ControlChange:
                ++controlChangeCount;
                break;
            case RealtimeMidiEventType::NoteOn:
                ++noteOnCount;
                ++noteOnsByTrack[event.trackIndex];
                break;
        }
    }

    for (size_t index = 0U; index < NOTE_EVENTS_PER_PHASE; ++index) {
        assert(envelope[index].type == RealtimeMidiEventType::NoteOff);
    }
    const size_t controlChangeEnd = NOTE_EVENTS_PER_PHASE + CC_EVENTS_PER_FRAME;
    for (size_t index = NOTE_EVENTS_PER_PHASE;
         index < controlChangeEnd;
         ++index) {
        assert(envelope[index].type == RealtimeMidiEventType::ControlChange);
    }
    for (size_t index = controlChangeEnd; index < envelope.size(); ++index) {
        assert(envelope[index].type == RealtimeMidiEventType::NoteOn);
    }

    assert(noteOffCount == NOTE_EVENTS_PER_PHASE);
    assert(controlChangeCount == CC_EVENTS_PER_FRAME);
    assert(noteOnCount == NOTE_EVENTS_PER_PHASE);
    for (size_t track = 0U; track < TRACK_COUNT; ++track) {
        assert(noteOffsByTrack[track] == NOTES_PER_TRACK);
        assert(noteOnsByTrack[track] == NOTES_PER_TRACK);
    }
}

void assertAcceptedBoundary(const ProducerEnvelope& envelope, size_t count) {
    RealtimeMidiQueue queue;
    const auto result = queue.pushBatch(envelope.data(), count);
    assert(result.status == RealtimeMidiQueueBatchStatus::OK);
    assert(result.requestedCount == count);
    assert(queue.size() == count);
    assert(queue.diagnostics().highWaterMark == count);
    assert(queue.diagnostics().rejectedBatchCount == 0U);
}

void assertRejectedAtomically(const ProducerEnvelope& envelope, size_t count) {
    RealtimeMidiQueue queue;
    assert(queue.push(envelope.front()));
    const size_t sizeBefore = queue.size();
    const auto result = queue.pushBatch(envelope.data(), count);
    assert(result.status == RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED);
    assert(result.requestedCount == count);
    assert(queue.size() == sizeBefore);
    assert(queue.diagnostics().rejectedBatchCount == 1U);
    assert(queue.diagnostics().rejectedEventCount == count);
}

}  // namespace

int main() {
    const auto envelope = buildProducerEnvelope();
    assertExactClassesAndTracks(envelope);
    assertAcceptedBoundary(envelope, 575U);
    assertAcceptedBoundary(envelope, 576U);
    assertRejectedAtomically(envelope, 577U);
    assertRejectedAtomically(envelope, PRODUCER_ENVELOPE_COUNT);

    std::cout
        << "[PASS] 16 notes/Track => 256 Off + 320 CC + 256 On = 832; "
           "575/576 accepted, 577/832 rejected atomically\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/time/Time.hpp>

#include "../../src/sequencer/SequencerMidiEventSink.hpp"

namespace {

using core::sequencer::RealtimeMidiEventType;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;

uint32_t fakeMicros = 0;

class MockMidiTransport : public oc::interface::IMidi {
public:
    struct Message {
        RealtimeMidiEventType type;
        uint8_t channel;
        uint8_t note;
        uint8_t velocity;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({RealtimeMidiEventType::NoteOn, channel, note, velocity});
    }
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({RealtimeMidiEventType::NoteOff, channel, note, velocity});
    }
    void sendSysEx(const uint8_t*, size_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendPitchBend(uint8_t, int16_t) override {}
    void sendChannelPressure(uint8_t, uint8_t) override {}
    void sendClock() override {}
    void sendStart() override {}
    void sendStop() override {}
    void sendContinue() override {}

    void setOnCC(CCCallback cb) override { on_cc = std::move(cb); }
    void setOnNoteOn(NoteCallback cb) override { on_note_on = std::move(cb); }
    void setOnNoteOff(NoteCallback cb) override { on_note_off = std::move(cb); }
    void setOnSysEx(SysExCallback cb) override { on_sysex = std::move(cb); }
    void setOnClock(ClockCallback cb) override { on_clock = std::move(cb); }
    void setOnStart(RealtimeCallback cb) override { on_start = std::move(cb); }
    void setOnStop(RealtimeCallback cb) override { on_stop = std::move(cb); }
    void setOnContinue(RealtimeCallback cb) override { on_continue = std::move(cb); }

    std::vector<Message> messages;
    CCCallback on_cc;
    NoteCallback on_note_on;
    NoteCallback on_note_off;
    SysExCallback on_sysex;
    ClockCallback on_clock;
    RealtimeCallback on_start;
    RealtimeCallback on_stop;
    RealtimeCallback on_continue;
};

class Observer final : public core::sequencer::SequencerMidiEventSinkObserver {
public:
    void onNoteOn(uint8_t trackIndex, uint8_t velocity) override {
        noteOnCount += 1;
        lastTrack = trackIndex;
        lastVelocity = velocity;
    }

    uint32_t noteOnCount = 0;
    uint8_t lastTrack = 0;
    uint8_t lastVelocity = 0;
};

SequencerEvent noteEvent(SequencerEventType type,
                         uint32_t tick,
                         uint8_t channel,
                         uint8_t note,
                         uint8_t velocity) {
    return {
        .tick = tick,
        .type = type,
        .channel = channel,
        .note = note,
        .velocity = velocity,
    };
}

void installTimeProvider() {
    oc::time::setMicrosProvider([]() { return fakeMicros; });
}

void drainDue(core::sequencer::RealtimeMidiQueue& queue,
              oc::api::MidiAPI& midi,
              uint32_t nowUs) {
    fakeMicros = nowUs;
    queue.drainDue(midi, nowUs, 10000);
}

void test_note_on_uses_timeline_deadline_and_observer() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 2, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(10, 1000, 50);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 12, 3, 64, 90)));

    drainDue(queue, midi, 1099);
    assert(transport.messages.empty());

    drainDue(queue, midi, 1100);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type == RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].channel == 3);
    assert(transport.messages[0].note == 64);
    assert(transport.messages[0].velocity == 90);
    assert(observer.noteOnCount == 1);
    assert(observer.lastTrack == 2);
    assert(observer.lastVelocity == 90);

    std::cout << "[PASS] test_note_on_uses_timeline_deadline_and_observer\n";
}

void test_note_off_marks_inactive_and_counts_observer() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 1, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(20, 2000, 100);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 20, 0, 60, 100)));
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOff, 21, 0, 60, 0)));

    drainDue(queue, midi, 2100);
    assert(transport.messages.size() == 2);
    assert(transport.messages[0].type == RealtimeMidiEventType::NoteOn);
    assert(transport.messages[1].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].note == 60);
    assert(observer.noteOnCount == 1);

    assert(sink.emitSequencerEvent({.tick = 21, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 2100);
    assert(transport.messages.size() == 2);

    std::cout << "[PASS] test_note_off_marks_inactive_and_counts_observer\n";
}

void test_all_notes_off_cancels_notes_that_were_never_dispatched() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 2, 60, 90)));
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 5, 2, 61, 91)));
    assert(queue.size() == 2);
    assert(sink.emitSequencerEvent({.tick = 4, .type = SequencerEventType::AllNotesOff}));
    assert(queue.size() == 0);

    drainDue(queue, midi, 3999);
    assert(transport.messages.empty());

    drainDue(queue, midi, 4000);
    assert(transport.messages.empty());
    assert(queue.size() == 0);
    assert(observer.noteOnCount == 0);

    std::cout << "[PASS] test_all_notes_off_cancels_notes_that_were_never_dispatched\n";
}

void test_all_notes_off_flushes_active_chord_voices() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    for (uint8_t i = 0; i < 8; ++i) {
        assert(sink.emitSequencerEvent(
            noteEvent(SequencerEventType::NoteOn, 4, 2, static_cast<uint8_t>(60 + i), 90)));
    }

    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 8);
    for (uint8_t i = 0; i < 8; ++i) {
        assert(transport.messages[i].type == RealtimeMidiEventType::NoteOn);
        assert(transport.messages[i].channel == 2);
        assert(transport.messages[i].note == static_cast<uint8_t>(60 + i));
    }

    assert(sink.emitSequencerEvent({.tick = 4, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 4000);

    assert(transport.messages.size() == 16);
    for (uint8_t i = 0; i < 8; ++i) {
        const auto& message = transport.messages[8 + i];
        assert(message.type == RealtimeMidiEventType::NoteOff);
        assert(message.channel == 2);
        assert(message.note == static_cast<uint8_t>(60 + i));
    }
    assert(observer.noteOnCount == 8);

    std::cout << "[PASS] test_all_notes_off_flushes_active_chord_voices\n";
}

void test_all_notes_off_tracks_more_than_thirty_two_active_notes() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    for (uint8_t note = 24; note < 64; ++note) {
        assert(sink.emitSequencerEvent(
            noteEvent(SequencerEventType::NoteOn, 4, 2, note, 90)
        ));
    }
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 40);

    assert(sink.emitSequencerEvent({.tick = 4, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 80);

    std::cout << "[PASS] test_all_notes_off_tracks_more_than_thirty_two_active_notes\n";
}

void test_all_notes_off_atomically_handles_128_active_notes() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    for (uint8_t note = 0; note < 128U; ++note) {
        assert(sink.emitSequencerEvent(
            noteEvent(SequencerEventType::NoteOn, 4, 0, note, 90)
        ));
    }
    assert(queue.size() == 128);
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 128);

    assert(sink.emitSequencerEvent({
        .tick = 4,
        .type = SequencerEventType::AllNotesOff,
    }));
    assert(queue.size() == 128);
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 256);
    for (size_t i = 128; i < 256; ++i) {
        assert(transport.messages[i].type == RealtimeMidiEventType::NoteOff);
    }
}

void test_all_notes_off_rejects_one_over_queue_capacity_without_partial_panic() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    const size_t activeNoteCount = queue.capacity();
    assert(activeNoteCount <=
           static_cast<size_t>(core::sequencer::SequencerMidiEventSink::MIDI_CHANNEL_COUNT) *
               128U);
    for (size_t index = 0; index < activeNoteCount; ++index) {
        const auto channel = static_cast<uint8_t>(index / 128U);
        const auto note = static_cast<uint8_t>(index % 128U);
        assert(sink.emitSequencerEvent(
            noteEvent(SequencerEventType::NoteOn, 4, channel, note, 90)
        ));
        // Bound the setup independently from the production queue capacity;
        // the panic preflight below is what this test intentionally saturates.
        if (note == 127U) drainDue(queue, midi, 4000);
    }
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == activeNoteCount);

    // An unrelated queued event proves failed preflight mutates no queue data.
    assert(queue.push(core::sequencer::RealtimeMidiEvent{
        .deadlineUs = 5000,
        .type = RealtimeMidiEventType::NoteOff,
        .channel = 2,
        .note = 10,
        .velocity = 0,
        .trackIndex = 1,
    }));
    assert(!sink.emitSequencerEvent({
        .tick = 4,
        .type = SequencerEventType::AllNotesOff,
    }));
    assert(queue.size() == 1);
    assert(queue.diagnostics().criticalNoteOffOverflowCount == 1);
    assert(queue.cancelPendingEvents(1) == 1);
}

void test_all_notes_off_tracks_same_pitch_per_channel() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 2, 60, 90)));
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 3, 60, 91)));
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 2);

    assert(sink.emitSequencerEvent({.tick = 4, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 4000);

    assert(transport.messages.size() == 4);
    assert(transport.messages[2].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[2].channel == 2);
    assert(transport.messages[2].note == 60);
    assert(transport.messages[3].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[3].channel == 3);
    assert(transport.messages[3].note == 60);

    std::cout << "[PASS] test_all_notes_off_tracks_same_pitch_per_channel\n";
}

void test_retrigger_same_pitch_keeps_latest_note_active() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 2, 60, 90)));
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 1);

    sink.setTimeline(5, 4025, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOff, 5, 2, 60, 0)));
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 5, 2, 60, 91)));
    drainDue(queue, midi, 4025);

    assert(transport.messages.size() == 3);
    assert(transport.messages[1].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].note == 60);
    assert(transport.messages[2].type == RealtimeMidiEventType::NoteOn);
    assert(transport.messages[2].note == 60);
    assert(transport.messages[2].velocity == 91);

    assert(sink.emitSequencerEvent({.tick = 5, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 4025);

    assert(transport.messages.size() == 4);
    assert(transport.messages[3].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[3].note == 60);

    std::cout << "[PASS] test_retrigger_same_pitch_keeps_latest_note_active\n";
}

void test_all_notes_off_releases_note_before_long_gate_note_off_arrives() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 2, 72, 96)));
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type == RealtimeMidiEventType::NoteOn);

    sink.setTimeline(8, 4100, 25);
    assert(sink.emitSequencerEvent({.tick = 8, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 4100);

    assert(transport.messages.size() == 2);
    assert(transport.messages[1].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].channel == 2);
    assert(transport.messages[1].note == 72);

    std::cout << "[PASS] test_all_notes_off_releases_note_before_long_gate_note_off_arrives\n";
}

void test_all_notes_off_replaces_a_future_note_off_with_an_immediate_release() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer;
    core::sequencer::SequencerMidiEventSink sink(queue, 0, &observer);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(4, 4000, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 2, 72, 96)));
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 1);

    sink.setTimeline(5, 4025, 25);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOff, 8, 2, 72, 0)));
    assert(queue.size() == 1);

    assert(sink.emitSequencerEvent({.tick = 5, .type = SequencerEventType::AllNotesOff}));
    assert(queue.size() == 1);
    drainDue(queue, midi, 4025);

    assert(transport.messages.size() == 2);
    assert(transport.messages[1].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].note == 72);

    std::cout << "[PASS] "
                 "test_all_notes_off_replaces_a_future_note_off_with_an_immediate_release\n";
}

void test_all_notes_off_cancels_only_own_track() {
    core::sequencer::RealtimeMidiQueue queue;
    Observer observer0;
    Observer observer1;
    core::sequencer::SequencerMidiEventSink sink0(queue, 0, &observer0);
    core::sequencer::SequencerMidiEventSink sink1(queue, 1, &observer1);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink0.setTimeline(4, 4000, 25);
    sink1.setTimeline(4, 4000, 25);
    assert(sink0.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 2, 60, 90)));
    assert(sink1.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 4, 3, 61, 91)));
    assert(queue.size() == 2);
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 2);

    assert(sink0.emitSequencerEvent({.tick = 4, .type = SequencerEventType::AllNotesOff}));
    assert(queue.size() == 1);

    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 3);
    assert(transport.messages[2].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[2].channel == 2);
    assert(transport.messages[2].note == 60);
    assert(queue.size() == 0);

    assert(sink1.emitSequencerEvent({.tick = 4, .type = SequencerEventType::AllNotesOff}));
    drainDue(queue, midi, 4000);
    assert(transport.messages.size() == 4);
    assert(transport.messages[3].type == RealtimeMidiEventType::NoteOff);
    assert(transport.messages[3].channel == 3);
    assert(transport.messages[3].note == 61);

    std::cout << "[PASS] test_all_notes_off_cancels_only_own_track\n";
}

void test_past_tick_deadline_is_due_immediately() {
    core::sequencer::RealtimeMidiQueue queue;
    core::sequencer::SequencerMidiEventSink sink(queue, 0);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    sink.setTimeline(10, 1000, 50);
    assert(sink.emitSequencerEvent(noteEvent(SequencerEventType::NoteOn, 8, 1, 62, 80)));

    drainDue(queue, midi, 999);
    assert(transport.messages.size() == 1);
    assert(transport.messages[0].note == 62);

    std::cout << "[PASS] test_past_tick_deadline_is_due_immediately\n";
}

}  // namespace

int main() {
    installTimeProvider();
    test_note_on_uses_timeline_deadline_and_observer();
    test_note_off_marks_inactive_and_counts_observer();
    test_all_notes_off_cancels_notes_that_were_never_dispatched();
    test_all_notes_off_flushes_active_chord_voices();
    test_all_notes_off_tracks_more_than_thirty_two_active_notes();
    test_all_notes_off_atomically_handles_128_active_notes();
    test_all_notes_off_rejects_one_over_queue_capacity_without_partial_panic();
    test_all_notes_off_tracks_same_pitch_per_channel();
    test_retrigger_same_pitch_keeps_latest_note_active();
    test_all_notes_off_releases_note_before_long_gate_note_off_arrives();
    test_all_notes_off_replaces_a_future_note_off_with_an_immediate_release();
    test_all_notes_off_cancels_only_own_track();
    test_past_tick_deadline_is_due_immediately();

    std::cout << "All SequencerMidiEventSink tests passed\n";
    return 0;
}

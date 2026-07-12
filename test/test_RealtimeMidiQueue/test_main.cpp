#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/time/Time.hpp>

#include "../../src/sequencer/RealtimeMidiQueue.hpp"

namespace {

uint32_t fakeMicros = 0;

class MockMidiTransport : public oc::interface::IMidi {
public:
    struct Message {
        core::sequencer::RealtimeMidiEventType type;
        uint8_t channel;
        uint8_t note;
        uint8_t velocity;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({core::sequencer::RealtimeMidiEventType::NoteOn, channel, note, velocity});
    }
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
        messages.push_back({core::sequencer::RealtimeMidiEventType::NoteOff, channel, note, velocity});
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

core::sequencer::RealtimeMidiEvent event(core::sequencer::RealtimeMidiEventType type,
                                         uint32_t deadlineUs,
                                         uint8_t note) {
    return {
        .deadlineUs = deadlineUs,
        .type = type,
        .channel = 1,
        .note = note,
        .velocity = 100,
        .trackIndex = 0,
    };
}

core::sequencer::RealtimeMidiEvent event(core::sequencer::RealtimeMidiEventType type,
                                         uint32_t deadlineUs,
                                         uint8_t note,
                                         uint8_t trackIndex) {
    auto midiEvent = event(type, deadlineUs, note);
    midiEvent.trackIndex = trackIndex;
    return midiEvent;
}

void installTimeProvider() {
    oc::time::setMicrosProvider([]() { return fakeMicros; });
}

void test_drains_in_deadline_order_with_note_off_priority() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = 1000;

    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 1000, 60)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOff, 1000, 60)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 900, 61)));

    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.size() == 3);
    assert(transport.messages[0].type == core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].note == 61);
    assert(transport.messages[1].type == core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].note == 60);
    assert(transport.messages[2].type == core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[2].note == 60);
    assert(queue.size() == 0);

    std::cout << "[PASS] test_drains_in_deadline_order_with_note_off_priority\n";
}

void test_future_event_stays_queued() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = 1000;

    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 2000, 60)));
    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.empty());
    assert(queue.size() == 1);

    std::cout << "[PASS] test_future_event_stays_queued\n";
}

void test_late_note_on_is_sent() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = 5000;

    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 1000, 60)));
    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.size() == 1);
    assert(queue.size() == 0);

    std::cout << "[PASS] test_late_note_on_is_sent\n";
}

void test_large_late_note_on_drops_but_note_off_sends() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = core::sequencer::RealtimeMidiQueue::DROP_THRESHOLD_US + 1000;

    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 0, 60)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOff, 0, 60)));
    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type == core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(queue.size() == 0);

    std::cout << "[PASS] test_large_late_note_on_drops_but_note_off_sends\n";
}

void test_note_on_is_rejected_when_full() {
    core::sequencer::RealtimeMidiQueue queue;

    for (size_t i = 0; i < queue.capacity(); ++i) {
        assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn,
                                static_cast<uint32_t>(i),
                                60)));
    }

    assert(!queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 9999, 61)));
    assert(queue.size() == queue.capacity());

    std::cout << "[PASS] test_note_on_is_rejected_when_full\n";
}

void test_note_off_replaces_note_on_when_full() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    for (size_t i = 0; i < queue.capacity(); ++i) {
        assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn,
                                static_cast<uint32_t>(1000 + i),
                                static_cast<uint8_t>(i % 64))));
    }

    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOff, 1000, 60)));
    fakeMicros = 1000;
    queue.drainDue(midi, fakeMicros, 10000);

    assert(!transport.messages.empty());
    assert(transport.messages[0].type == core::sequencer::RealtimeMidiEventType::NoteOff);

    std::cout << "[PASS] test_note_off_replaces_note_on_when_full\n";
}

void test_cancel_pending_events_for_track_keeps_other_tracks() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};

    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 1000, 60, 1)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOff, 1000, 60, 1)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 1000, 61, 2)));

    assert(queue.cancelPendingEvents(1) == 2);
    assert(queue.size() == 1);

    fakeMicros = 1000;
    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type == core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].note == 61);

    std::cout << "[PASS] test_cancel_pending_events_for_track_keeps_other_tracks\n";
}

}  // namespace

int main() {
    installTimeProvider();
    test_drains_in_deadline_order_with_note_off_priority();
    test_future_event_stays_queued();
    test_late_note_on_is_sent();
    test_large_late_note_on_drops_but_note_off_sends();
    test_note_on_is_rejected_when_full();
    test_note_off_replaces_note_on_when_full();
    test_cancel_pending_events_for_track_keeps_other_tracks();
    std::cout << "All RealtimeMidiQueue tests passed\n";
    return 0;
}

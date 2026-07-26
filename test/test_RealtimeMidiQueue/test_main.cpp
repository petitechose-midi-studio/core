#include <array>
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
    void sendCC(uint8_t channel, uint8_t controller, uint8_t value) override {
        messages.push_back({
            core::sequencer::RealtimeMidiEventType::ControlChange,
            channel,
            controller,
            value,
        });
    }
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
    core::sequencer::RealtimeMidiEvent midiEvent{};
    midiEvent.deadlineUs = deadlineUs;
    midiEvent.type = type;
    midiEvent.trackIndex = 0U;
    midiEvent.channel = 1U;
    midiEvent.note = note;
    midiEvent.velocity = 100U;
    return midiEvent;
}

core::sequencer::RealtimeMidiEvent ccEvent(uint32_t deadlineUs,
                                           uint8_t controller,
                                           uint8_t value = 64,
                                           uint8_t trackIndex = 0) {
    core::sequencer::RealtimeMidiEvent midiEvent{};
    midiEvent.deadlineUs = deadlineUs;
    midiEvent.type = core::sequencer::RealtimeMidiEventType::ControlChange;
    midiEvent.trackIndex = trackIndex;
    midiEvent.channel = 1U;
    midiEvent.controller = controller;
    midiEvent.value = value;
    return midiEvent;
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
    assert(queue.push(ccEvent(1000, 74, 96)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 900, 61)));

    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.size() == 4);
    assert(transport.messages[0].type == core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[0].note == 61);
    assert(transport.messages[1].type == core::sequencer::RealtimeMidiEventType::NoteOff);
    assert(transport.messages[1].note == 60);
    assert(transport.messages[2].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[2].note == 74);
    assert(transport.messages[2].velocity == 96);
    assert(transport.messages[3].type == core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[3].note == 60);
    assert(queue.size() == 0);

    std::cout << "[PASS] test_drains_in_deadline_order_with_note_off_priority\n";
}

void test_late_cc_is_never_dropped() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = core::sequencer::RealtimeMidiQueue::DROP_THRESHOLD_US + 5000;

    assert(queue.push(ccEvent(0, 74, 99)));
    assert(queue.push(event(core::sequencer::RealtimeMidiEventType::NoteOn, 0, 60)));
    queue.drainDue(midi, fakeMicros, 10000);

    assert(transport.messages.size() == 1);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].note == 74);
    assert(queue.diagnostics().lateSendCount == 1);
    assert(queue.diagnostics().droppedLateNoteOnCount == 1);
}

void test_wrap_aware_deadline_keeps_cc_before_note_on() {
    core::sequencer::RealtimeMidiQueue queue;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    const uint32_t deadline = UINT32_MAX - 4U;
    assert(queue.push(event(
        core::sequencer::RealtimeMidiEventType::NoteOn,
        deadline,
        60
    )));
    assert(queue.push(ccEvent(deadline, 1, 7)));
    fakeMicros = 3;
    queue.drainDue(midi, fakeMicros, 10000);
    assert(transport.messages.size() == 2);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
}

void test_full_resolver_cc_envelope_is_atomic_between_note_phases() {
    core::sequencer::RealtimeMidiQueue queue;
    static_assert(core::sequencer::RealtimeMidiQueue::MAX_QUEUE_DEPTH == 576U);
    std::array<
        core::sequencer::RealtimeMidiEvent,
        core::sequencer::RealtimeMidiQueue::MAX_RESOLVED_CC_EVENTS_PER_FRAME
    > ccBatch{};
    for (uint8_t i = 0; i < 128; ++i) {
        assert(queue.push(event(
            core::sequencer::RealtimeMidiEventType::NoteOff,
            1000,
            i
        )));
        assert(queue.push(event(
            core::sequencer::RealtimeMidiEventType::NoteOn,
            1000,
            i
        )));
    }
    for (size_t i = 0; i < ccBatch.size(); ++i) {
        ccBatch[i] = ccEvent(
            1000,
            static_cast<uint8_t>(i % 128U),
            static_cast<uint8_t>(i % 128U),
            static_cast<uint8_t>(i % 16U)
        );
    }
    const auto accepted = queue.pushBatch(ccBatch.data(), ccBatch.size());
    assert(accepted.ok());
    assert(queue.size() == queue.capacity());
    assert(queue.diagnostics().highWaterMark == queue.capacity());

    const auto rejected = queue.pushBatch(ccBatch.data(), ccBatch.size());
    assert(rejected.status ==
           core::sequencer::RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED);
    assert(queue.size() == queue.capacity());
    assert(queue.diagnostics().rejectedControlChangeBatchCount == 1);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = 1000;
    queue.drainDue(midi, fakeMicros, UINT32_MAX);
    assert(transport.messages.size() == queue.capacity());
    for (size_t i = 0; i < 128; ++i) {
        assert(transport.messages[i].type ==
               core::sequencer::RealtimeMidiEventType::NoteOff);
    }
    constexpr size_t ccEnd = 128U +
        core::sequencer::RealtimeMidiQueue::MAX_RESOLVED_CC_EVENTS_PER_FRAME;
    for (size_t i = 128; i < ccEnd; ++i) {
        assert(transport.messages[i].type ==
               core::sequencer::RealtimeMidiEventType::ControlChange);
    }
    for (size_t i = ccEnd; i < queue.capacity(); ++i) {
        assert(transport.messages[i].type ==
               core::sequencer::RealtimeMidiEventType::NoteOn);
    }

    std::cout << "[PASS] exact 128 Off + 320 CC + 128 On envelope\n";
}

void test_note_off_batch_displaces_note_on_then_cc_never_note_off() {
    core::sequencer::RealtimeMidiQueue queue;
    for (uint8_t i = 0; i < 128; ++i) {
        assert(queue.push(event(
            core::sequencer::RealtimeMidiEventType::NoteOff,
            5000,
            i
        )));
    }
    constexpr size_t lowerPriorityCount = 224U;
    for (size_t i = 0; i < lowerPriorityCount; ++i) {
        assert(queue.push(ccEvent(
            5000,
            static_cast<uint8_t>(i % 128U),
            64U,
            static_cast<uint8_t>(i % 16U)
        )));
        assert(queue.push(event(
            core::sequencer::RealtimeMidiEventType::NoteOn,
            5000,
            static_cast<uint8_t>(i % 128U)
        )));
    }
    assert(queue.size() == queue.capacity());

    const std::array panic{
        event(core::sequencer::RealtimeMidiEventType::NoteOff, 4000, 100),
        event(core::sequencer::RealtimeMidiEventType::NoteOff, 4000, 101),
    };
    const auto first = queue.pushBatch(panic.data(), panic.size());
    assert(first.ok());
    assert(first.displacedNoteOnCount == 2);
    assert(first.displacedControlChangeCount == 0);

    std::array<core::sequencer::RealtimeMidiEvent, 222> morePanic{};
    for (size_t i = 0; i < morePanic.size(); ++i) {
        morePanic[i] = event(
            core::sequencer::RealtimeMidiEventType::NoteOff,
            4000,
            static_cast<uint8_t>((70U + i) % 128U)
        );
    }
    const auto second = queue.pushBatch(morePanic.data(), morePanic.size());
    assert(second.ok());
    assert(second.displacedNoteOnCount == morePanic.size());

    const auto third = queue.pushBatch(panic.data(), panic.size());
    assert(third.ok());
    assert(third.displacedNoteOnCount == 0);
    assert(third.displacedControlChangeCount == 2);

    // Fill the remaining lower-priority slots with NoteOffs. Once only
    // NoteOffs remain, another panic cannot sacrifice any of them.
    std::array<core::sequencer::RealtimeMidiEvent, 222> finalPanic{};
    for (size_t i = 0; i < finalPanic.size(); ++i) {
        finalPanic[i] = event(
            core::sequencer::RealtimeMidiEventType::NoteOff,
            4000,
            static_cast<uint8_t>(i % 128U)
        );
    }
    const auto fourth = queue.pushBatch(finalPanic.data(), finalPanic.size());
    assert(fourth.ok());
    assert(fourth.displacedControlChangeCount == finalPanic.size());
    const auto rejected = queue.pushBatch(panic.data(), panic.size());
    assert(!rejected.ok());
    assert(queue.size() == queue.capacity());
    assert(queue.diagnostics().criticalNoteOffOverflowCount == 1);
}

void test_note_off_replacement_is_atomic_on_failure() {
    core::sequencer::RealtimeMidiQueue queue;
    for (size_t i = 0; i < queue.capacity(); ++i) {
        assert(queue.push(event(
            core::sequencer::RealtimeMidiEventType::NoteOff,
            1000,
            static_cast<uint8_t>(i % 128U),
            i == 0 ? 3 : 2
        )));
    }
    std::array<oc::note::sequencer::StepBitMask128, 2> activeNotesByChannel{};
    activeNotesByChannel[1].setBit(10);
    activeNotesByChannel[1].setBit(11);
    const auto rejected = queue.replaceTrackEventsWithNoteOffBatch(
        3,
        900,
        activeNotesByChannel.data(),
        activeNotesByChannel.size()
    );
    assert(!rejected.ok());
    assert(queue.size() == queue.capacity());
    // The pre-existing Track 4 event was not cancelled on failed preflight.
    assert(queue.cancelPendingEvents(3) == 1);
}

class LifecycleObserver final
    : public core::sequencer::RealtimeMidiQueueLifecycleObserver {
public:
    struct Removal {
        core::sequencer::RealtimeMidiEvent event{};
        core::sequencer::RealtimeMidiQueueLifecycleReason reason =
            core::sequencer::RealtimeMidiQueueLifecycleReason::QUEUE_CLEARED;
    };

    void onRealtimeMidiEventEnqueued(
        const core::sequencer::RealtimeMidiEvent& event
    ) override {
        enqueued.push_back(event);
    }
    void onRealtimeMidiEventRemoved(
        const core::sequencer::RealtimeMidiEvent& event,
        core::sequencer::RealtimeMidiQueueLifecycleReason reason
    ) override {
        removed.push_back({event, reason});
    }
    void onRealtimeMidiEventDispatched(
        const core::sequencer::RealtimeMidiEvent& event
    ) override {
        dispatched.push_back(event);
    }

    std::vector<core::sequencer::RealtimeMidiEvent> enqueued;
    std::vector<Removal> removed;
    std::vector<core::sequencer::RealtimeMidiEvent> dispatched;
};

void test_lifecycle_observer_reports_cc_dispatch_and_every_pending_removal() {
    core::sequencer::RealtimeMidiQueue queue;
    LifecycleObserver observer;
    queue.attachLifecycleObserver(observer);

    assert(queue.push(ccEvent(5000, 74, 99, 3)));
    for (size_t i = 1; i < queue.capacity(); ++i) {
        assert(queue.push(event(
            core::sequencer::RealtimeMidiEventType::NoteOff,
            6000,
            static_cast<uint8_t>(i % 128U),
            2
        )));
    }
    assert(queue.push(event(
        core::sequencer::RealtimeMidiEventType::NoteOff,
        4000,
        10,
        2
    )));
    assert(!observer.removed.empty());
    assert(observer.removed.back().event.type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(observer.removed.back().reason ==
           core::sequencer::RealtimeMidiQueueLifecycleReason::DISPLACED_BY_NOTE_OFF);

    queue.clear();
    observer.removed.clear();
    assert(queue.push(ccEvent(5000, 71, 80, 4)));
    assert(queue.cancelPendingEvents(4) == 1);
    assert(observer.removed.back().reason ==
           core::sequencer::RealtimeMidiQueueLifecycleReason::TRACK_CANCELLED);

    observer.removed.clear();
    assert(queue.push(ccEvent(5000, 72, 81, 5)));
    queue.clear();
    assert(observer.removed.back().reason ==
           core::sequencer::RealtimeMidiQueueLifecycleReason::QUEUE_CLEARED);

    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    assert(queue.push(ccEvent(1000, 73, 82, 6)));
    fakeMicros = 1000;
    queue.drainDue(midi, fakeMicros, 10000);
    assert(observer.dispatched.size() == 1);
    assert(observer.dispatched[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    queue.detachLifecycleObserver(observer);
}

void test_saturating_diagnostic_counter() {
    using core::sequencer::realtimeMidiSaturatingAdd;
    assert(realtimeMidiSaturatingAdd(UINT32_MAX - 2U, 1U) == UINT32_MAX - 1U);
    assert(realtimeMidiSaturatingAdd(UINT32_MAX - 2U, 3U) == UINT32_MAX);
    assert(realtimeMidiSaturatingAdd(UINT32_MAX, 1U) == UINT32_MAX);
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

void test_cancel_pending_note_events_preserves_same_track_cc() {
    core::sequencer::RealtimeMidiQueue queue;
    assert(queue.push(event(
        core::sequencer::RealtimeMidiEventType::NoteOn, 1000U, 60U, 3U
    )));
    assert(queue.push(event(
        core::sequencer::RealtimeMidiEventType::NoteOff, 1000U, 60U, 3U
    )));
    assert(queue.push(ccEvent(1000U, 74U, 91U, 3U)));
    assert(queue.push(event(
        core::sequencer::RealtimeMidiEventType::NoteOn, 1000U, 61U, 4U
    )));

    assert(queue.cancelPendingNoteEvents(3U) == 2U);
    assert(queue.size() == 2U);
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    fakeMicros = 1000U;
    queue.drainDue(midi, fakeMicros, 10000U);
    assert(transport.messages.size() == 2U);
    assert(transport.messages[0].type ==
           core::sequencer::RealtimeMidiEventType::ControlChange);
    assert(transport.messages[0].note == 74U);
    assert(transport.messages[0].velocity == 91U);
    assert(transport.messages[1].type ==
           core::sequencer::RealtimeMidiEventType::NoteOn);
    assert(transport.messages[1].note == 61U);

    std::cout
        << "[PASS] Note-plan cancellation preserves same-Track predictive CC\n";
}

void test_packed_event_preserves_invalid_metadata_for_validation() {
    static_assert(sizeof(core::sequencer::RealtimeMidiEvent) == 8U);
    core::sequencer::RealtimeMidiQueue queue;
    auto invalidTrack = event(
        core::sequencer::RealtimeMidiEventType::NoteOn,
        1000U,
        60U
    );
    invalidTrack.trackIndex = 16U;
    assert(invalidTrack.trackIndex == 16U);
    assert(!queue.push(invalidTrack));
    assert(queue.size() == 0U);

    auto invalidType = invalidTrack;
    invalidType.trackIndex = 15U;
    invalidType.type = static_cast<core::sequencer::RealtimeMidiEventType>(7U);
    assert(static_cast<uint8_t>(invalidType.type) == 7U);
    assert(!queue.push(invalidType));
    assert(queue.size() == 0U);

    std::cout << "[PASS] packed metadata keeps invalid values rejectable\n";
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
    test_cancel_pending_note_events_preserves_same_track_cc();
    test_packed_event_preserves_invalid_metadata_for_validation();
    test_late_cc_is_never_dropped();
    test_wrap_aware_deadline_keeps_cc_before_note_on();
    test_full_resolver_cc_envelope_is_atomic_between_note_phases();
    test_note_off_batch_displaces_note_on_then_cc_never_note_off();
    test_note_off_replacement_is_atomic_on_failure();
    test_lifecycle_observer_reports_cc_dispatch_and_every_pending_removal();
    test_saturating_diagnostic_counter();
    std::cout << "All RealtimeMidiQueue tests passed\n";
    return 0;
}

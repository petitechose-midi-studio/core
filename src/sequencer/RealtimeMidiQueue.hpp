#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/sequencer/StepBitMask128.hpp>

#include "sequencer/RealtimeMidiEvent.hpp"

namespace core::sequencer {

class RealtimeMidiQueueDispatchObserver {
public:
    virtual ~RealtimeMidiQueueDispatchObserver() = default;
    virtual void onRealtimeMidiEventDispatched(const RealtimeMidiEvent& event) = 0;
};

enum class RealtimeMidiQueueLifecycleReason : uint8_t {
    DISPLACED_BY_NOTE_OFF = 0,
    TRACK_CANCELLED,
    SOURCE_REPLACED,
    QUEUE_CLEARED,
    DROPPED_LATE,
};

/**
 * Global queue lifecycle observer, independent from the per-Track note
 * observer. The shared CC coordinator uses it to distinguish queued values
 * from values that physically reached MidiAPI.
 */
class RealtimeMidiQueueLifecycleObserver {
public:
    virtual ~RealtimeMidiQueueLifecycleObserver() = default;
    virtual void onRealtimeMidiEventEnqueued(const RealtimeMidiEvent& event) = 0;
    virtual void onRealtimeMidiEventRemoved(
        const RealtimeMidiEvent& event,
        RealtimeMidiQueueLifecycleReason reason
    ) = 0;
    virtual void onRealtimeMidiEventDispatched(const RealtimeMidiEvent& event) = 0;
};

enum class RealtimeMidiQueueBatchStatus : uint8_t {
    OK = 0,
    INVALID_INPUT,
    CAPACITY_EXCEEDED,
};

struct RealtimeMidiQueueBatchResult {
    RealtimeMidiQueueBatchStatus status =
        RealtimeMidiQueueBatchStatus::INVALID_INPUT;
    uint16_t requestedCount = 0;
    uint16_t cancelledCount = 0;
    uint16_t displacedNoteOnCount = 0;
    uint16_t displacedControlChangeCount = 0;

    [[nodiscard]] bool ok() const {
        return status == RealtimeMidiQueueBatchStatus::OK;
    }
};

struct RealtimeMidiQueueDiagnostics {
    uint32_t rejectedEventCount = 0;
    uint32_t rejectedBatchCount = 0;
    uint32_t rejectedControlChangeBatchCount = 0;
    uint32_t criticalNoteOffOverflowCount = 0;
    uint32_t displacedNoteOnCount = 0;
    uint32_t displacedControlChangeCount = 0;
    uint32_t lateSendCount = 0;
    uint32_t droppedLateNoteOnCount = 0;
    uint16_t highWaterMark = 0;
};

[[nodiscard]] constexpr uint32_t realtimeMidiSaturatingAdd(
    uint32_t value,
    uint32_t increment
) {
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

/**
 * Bounded, deadline-ordered queue for sequencer note and resolved CC events.
 *
 * Playback/event sinks enqueue note events here; runtime lanes drain due events
 * through `MidiAPI`. Note-offs have priority over note-ons at the same deadline
 * and may displace note-ons when the queue is full, so panic/stop paths can
 * silence active notes instead of being blocked by musical data.
 */
class RealtimeMidiQueue {
public:
    // Current transitional queue composition, not the producer maximum. Note
    // can emit 16 same-deadline notes per Track, hence 256 events in either
    // note phase across 16 Tracks. The executable producer-envelope fixture
    // records that 832-event raw frame while capacity policy remains separate.
    static constexpr size_t NOTE_EVENT_PHASE_CAPACITY = 128U;
    static constexpr size_t MAX_RESOLVED_CC_EVENTS_PER_FRAME = 320U;
    static constexpr size_t MAX_QUEUE_DEPTH =
        2U * NOTE_EVENT_PHASE_CAPACITY +
        MAX_RESOLVED_CC_EVENTS_PER_FRAME;
    static constexpr uint32_t LATE_SEND_THRESHOLD_US = 2000;
    static constexpr uint32_t DROP_THRESHOLD_US = 20000;
    static constexpr uint32_t MAX_DRAIN_BUDGET_US = 500;

    bool push(const RealtimeMidiEvent& event);
    /**
     * Transactional batch insertion. Every event is validated and capacity is
     * preflighted before mutation. A batch never evicts one of its own events;
     * new NoteOffs may displace existing NoteOns, then existing CCs. CC and
     * NoteOn batches never displace NoteOffs.
     */
    RealtimeMidiQueueBatchResult pushBatch(
        const RealtimeMidiEvent* events,
        size_t count
    );
    /**
     * Allocation-free panic batch used by SequencerMidiEventSink. The complete
     * active-note set is counted and capacity-preflighted before cancellation
     * or insertion, so AllNotesOff cannot become a partial per-note panic.
     */
    RealtimeMidiQueueBatchResult replaceTrackEventsWithNoteOffBatch(
        uint8_t trackIndex,
        uint32_t deadlineUs,
        const oc::note::sequencer::StepBitMask128* activeNotesByChannel,
        size_t channelCount
    );
    uint32_t cancelPendingEvents(uint8_t trackIndex);
    /** Remove only pending Note On/Off edges for one Track; preserve CC. */
    uint32_t cancelPendingNoteEvents(uint8_t trackIndex);
    /** Cold reset boundary; removes CC without publishing a replacement set. */
    uint32_t cancelControlChangeEvents();
    void clear();
    void attachTrackObserver(uint8_t trackIndex, RealtimeMidiQueueDispatchObserver& observer);
    void detachTrackObserver(uint8_t trackIndex, RealtimeMidiQueueDispatchObserver& observer);
    void attachLifecycleObserver(RealtimeMidiQueueLifecycleObserver& observer);
    void detachLifecycleObserver(RealtimeMidiQueueLifecycleObserver& observer);
    size_t size() const { return count_; }
    size_t capacity() const { return events_.size(); }
    const RealtimeMidiQueueDiagnostics& diagnostics() const {
        return diagnostics_;
    }

    void drainDue(oc::api::MidiAPI& midi,
                  uint32_t nowUs,
                  uint32_t budgetUs = MAX_DRAIN_BUDGET_US);

private:
    static bool due_(const RealtimeMidiEvent& event, uint32_t nowUs);
    static bool comesBefore_(const RealtimeMidiEvent& lhs, const RealtimeMidiEvent& rhs);
    static uint8_t priority_(RealtimeMidiEventType type);
    static bool validEvent_(const RealtimeMidiEvent& event);

    RealtimeMidiQueueBatchResult pushBatchImpl_(
        const RealtimeMidiEvent* events,
        size_t count
    );
    void insertNoFail_(const RealtimeMidiEvent& event);
    void erase_(size_t index);
    void remove_(size_t index, RealtimeMidiQueueLifecycleReason reason);
    void send_(oc::api::MidiAPI& midi, const RealtimeMidiEvent& event);
    void recordRejectedBatch_(
        const RealtimeMidiEvent* events,
        size_t count,
        bool capacityExceeded
    );
    void updateHighWaterMark_();

    std::array<RealtimeMidiEvent, MAX_QUEUE_DEPTH> events_{};
    std::array<RealtimeMidiQueueDispatchObserver*, 16> track_observers_{};
    RealtimeMidiQueueLifecycleObserver* lifecycle_observer_ = nullptr;
    size_t count_ = 0;
    RealtimeMidiQueueDiagnostics diagnostics_{};
};

}  // namespace core::sequencer

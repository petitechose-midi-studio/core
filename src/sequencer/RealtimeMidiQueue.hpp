#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>

#include "sequencer/RealtimeMidiEvent.hpp"

namespace core::sequencer {

class RealtimeMidiQueueDispatchObserver {
public:
    virtual ~RealtimeMidiQueueDispatchObserver() = default;
    virtual void onRealtimeMidiEventDispatched(const RealtimeMidiEvent& event) = 0;
};

/**
 * Bounded, deadline-ordered queue for sequencer note events.
 *
 * Playback/event sinks enqueue note events here; runtime lanes drain due events
 * through `MidiAPI`. Note-offs have priority over note-ons at the same deadline
 * and may displace note-ons when the queue is full, so panic/stop paths can
 * silence active notes instead of being blocked by musical note-ons.
 */
class RealtimeMidiQueue {
public:
    static constexpr size_t MAX_QUEUE_DEPTH = 128;
    static constexpr uint32_t LATE_SEND_THRESHOLD_US = 2000;
    static constexpr uint32_t DROP_THRESHOLD_US = 20000;
    static constexpr uint32_t MAX_DRAIN_BUDGET_US = 500;

    bool push(const RealtimeMidiEvent& event);
    uint32_t cancelPendingEvents(uint8_t trackIndex);
    void clear();
    void attachTrackObserver(uint8_t trackIndex, RealtimeMidiQueueDispatchObserver& observer);
    void detachTrackObserver(uint8_t trackIndex, RealtimeMidiQueueDispatchObserver& observer);
    size_t size() const { return count_; }
    size_t capacity() const { return events_.size(); }

    void drainDue(oc::api::MidiAPI& midi,
                  uint32_t nowUs,
                  uint32_t budgetUs = MAX_DRAIN_BUDGET_US);

private:
    static bool due_(const RealtimeMidiEvent& event, uint32_t nowUs);
    static bool comesBefore_(const RealtimeMidiEvent& lhs, const RealtimeMidiEvent& rhs);
    static uint8_t priority_(RealtimeMidiEventType type);

    bool makeRoomFor_(const RealtimeMidiEvent& event);
    void erase_(size_t index);
    void send_(oc::api::MidiAPI& midi, const RealtimeMidiEvent& event);

    std::array<RealtimeMidiEvent, MAX_QUEUE_DEPTH> events_{};
    std::array<RealtimeMidiQueueDispatchObserver*, 16> track_observers_{};
    size_t count_ = 0;
};

}  // namespace core::sequencer

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/SequencerEvent.hpp>

#include "sequencer/RealtimeMidiQueue.hpp"

namespace core::sequencer {

/**
 * Observer for MIDI activity produced by sequencer event sinks.
 *
 * The observer is telemetry/projection only. It should not send MIDI or mutate
 * sequencer state; the sink's authoritative side effect is enqueueing
 * `RealtimeMidiEvent` instances.
 */
struct SequencerMidiEventSinkObserver {
    virtual ~SequencerMidiEventSinkObserver() = default;

    virtual void onNoteOn(uint8_t trackIndex, uint8_t velocity) = 0;
    virtual void onNoteOff() = 0;
    virtual void onPanicNoteOffs(uint32_t count) = 0;
};

/**
 * Adapts note sequencer engine events into the realtime MIDI queue.
 *
 * The sink translates sequencer ticks into microsecond deadlines using the
 * current timeline, tracks active notes per track, and handles all-notes-off by
 * cancelling that track's pending note-ons before enqueueing immediate note-offs.
 */
class SequencerMidiEventSink final : public oc::note::sequencer::ISequencerEventSink {
public:
    static constexpr size_t MAX_ACTIVE_NOTES = 32;

    explicit SequencerMidiEventSink(RealtimeMidiQueue& queue,
                                    uint8_t trackIndex,
                                    SequencerMidiEventSinkObserver* observer = nullptr);

    void setTimeline(uint32_t currentTick, uint32_t nowUs, uint32_t tickPeriodUs);
    bool emitSequencerEvent(const oc::note::sequencer::SequencerEvent& event) override;

private:
    struct ActiveNote {
        uint8_t channel = 0;
        uint8_t note = 0;
        bool active = false;
    };

    bool enqueueNoteOn_(const oc::note::sequencer::SequencerEvent& event);
    bool enqueueNoteOff_(const oc::note::sequencer::SequencerEvent& event);
    bool enqueueAllNotesOff_();
    uint32_t deadlineForTick_(uint32_t tick) const;
    void markNoteActive_(uint8_t channel, uint8_t note);
    void markNoteInactive_(uint8_t channel, uint8_t note);

    RealtimeMidiQueue& queue_;
    SequencerMidiEventSinkObserver* observer_ = nullptr;
    uint8_t track_index_ = 0;
    uint32_t current_tick_ = 0;
    uint32_t current_time_us_ = 0;
    uint32_t tick_period_us_ = 0;
    std::array<ActiveNote, MAX_ACTIVE_NOTES> active_notes_{};
};

}  // namespace core::sequencer

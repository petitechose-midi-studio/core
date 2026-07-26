#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/note/sequencer/StepBitMask128.hpp>

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
};

/**
 * Adapts note sequencer engine events into the realtime MIDI queue.
 *
 * The sink translates sequencer ticks into microsecond deadlines using the
 * current timeline, tracks active notes per track, and handles all-notes-off by
 * cancelling that track's pending note-ons before enqueueing immediate note-offs.
 */
class SequencerMidiEventSink final : public oc::note::sequencer::ISequencerEventSink,
                                    private RealtimeMidiQueueDispatchObserver {
public:
    static constexpr uint8_t MIDI_CHANNEL_COUNT = 16;

    explicit SequencerMidiEventSink(RealtimeMidiQueue& queue,
                                    uint8_t trackIndex,
                                    SequencerMidiEventSinkObserver* observer = nullptr);
    ~SequencerMidiEventSink() override;

    /**
     * Sets the musical-to-physical deadline projection for subsequently
     * emitted scheduled events. `deadlineOffsetUs` is signed: positive values
     * defer, negative values are valid only when the caller has already
     * advanced the engine through a causal look-ahead horizon.
     *
     * Panic/AllNotesOff deliberately ignores this offset and remains immediate.
     */
    void setTimeline(
        uint32_t currentTick,
        uint32_t nowUs,
        uint32_t tickPeriodUs,
        int32_t deadlineOffsetUs = 0
    );
    bool emitSequencerEvent(const oc::note::sequencer::SequencerEvent& event) override;

private:
    bool enqueueNoteOn_(const oc::note::sequencer::SequencerEvent& event);
    bool enqueueNoteOff_(const oc::note::sequencer::SequencerEvent& event);
    bool enqueueAllNotesOff_();
    bool projectedDeadlineForTick_(uint32_t tick, uint32_t& out) const;
    void markNoteActive_(uint8_t channel, uint8_t note);
    void markNoteInactive_(uint8_t channel, uint8_t note);
    void onRealtimeMidiEventDispatched(const RealtimeMidiEvent& event) override;

    RealtimeMidiQueue& queue_;
    SequencerMidiEventSinkObserver* observer_ = nullptr;
    uint8_t track_index_ = 0;
    uint32_t current_tick_ = 0;
    uint32_t current_time_us_ = 0;
    uint32_t tick_period_us_ = 0;
    int32_t deadline_offset_us_ = 0;
    std::array<oc::note::sequencer::StepBitMask128, MIDI_CHANNEL_COUNT>
        active_notes_by_channel_{};
};

}  // namespace core::sequencer

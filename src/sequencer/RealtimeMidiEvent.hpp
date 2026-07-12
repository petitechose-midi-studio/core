#pragma once

#include <cstdint>

namespace core::sequencer {

/**
 * Event type understood by `RealtimeMidiQueue`.
 *
 * Keep this intentionally small: sequencer playback queues note timing here,
 * while clock/start/stop messages are owned by clock/runtime services.
 */
enum class RealtimeMidiEventType : uint8_t {
    NoteOn,
    NoteOff,
};

/**
 * Deadline-based MIDI note event produced by sequencer playback.
 *
 * `deadlineUs` is compared with wrap-aware time helpers by the queue. The
 * `trackIndex` exists so panic/all-notes-off paths can cancel only one track's
 * pending generation without disturbing other tracks.
 */
struct RealtimeMidiEvent {
    uint32_t deadlineUs = 0;
    RealtimeMidiEventType type = RealtimeMidiEventType::NoteOff;
    uint8_t channel = 0;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t trackIndex = 0;
};

}  // namespace core::sequencer

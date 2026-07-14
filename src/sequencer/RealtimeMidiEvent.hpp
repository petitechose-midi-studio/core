#pragma once

#include <cstdint>

namespace core::sequencer {

/**
 * Event type understood by `RealtimeMidiQueue`.
 *
 * Clock/start/stop messages remain owned by clock/runtime services. Classic CC
 * shares this queue so a resolved lane value can be ordered before Note On at
 * the exact same deadline without a second physical MIDI path.
 */
enum class RealtimeMidiEventType : uint8_t {
    NoteOn,
    NoteOff,
    ControlChange,
};

/**
 * Deadline-based MIDI event produced by sequencer/resolver playback.
 *
 * `deadlineUs` is compared with wrap-aware time helpers by the queue. The
 * `trackIndex` exists so panic/all-notes-off paths can cancel only one track's
 * pending generation without disturbing other tracks.
 */
struct RealtimeMidiEvent {
    uint32_t deadlineUs = 0;
    RealtimeMidiEventType type = RealtimeMidiEventType::NoteOff;
    uint8_t channel = 0;
    union {
        uint8_t note = 0;
        uint8_t controller;
    };
    union {
        uint8_t velocity = 0;
        uint8_t value;
    };
    uint8_t trackIndex = 0;
};

}  // namespace core::sequencer

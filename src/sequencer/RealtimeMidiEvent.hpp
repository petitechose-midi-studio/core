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
    NoteOff = 0,
    NoteOn = 1,
    ControlChange = 2,
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
    // Three type bits deliberately preserve invalid values 3..7 until queue
    // validation, while the fixed Track domain needs four valid bits. Sharing
    // one byte removes the former three-byte tail padding from every queue slot
    // while keeping direct field access on the hot path.
    RealtimeMidiEventType type : 3;
    // Five bits deliberately preserve invalid values 16..31 until queue
    // validation; a four-bit field would silently alias Track 16 to Track 0.
    uint8_t trackIndex : 5;
    uint8_t channel = 0;
    union {
        uint8_t note = 0;
        uint8_t controller;
    };
    union {
        uint8_t velocity = 0;
        uint8_t value;
    };
};

static_assert(sizeof(RealtimeMidiEvent) == 8U);

}  // namespace core::sequencer

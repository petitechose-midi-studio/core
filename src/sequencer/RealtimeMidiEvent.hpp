#pragma once

#include <cstdint>

namespace core::sequencer {

enum class RealtimeMidiEventType : uint8_t {
    NoteOn,
    NoteOff,
};

struct RealtimeMidiEvent {
    uint32_t deadlineUs = 0;
    RealtimeMidiEventType type = RealtimeMidiEventType::NoteOff;
    uint8_t channel = 0;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t trackIndex = 0;
};

}  // namespace core::sequencer

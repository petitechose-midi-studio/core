#pragma once

#include <cstddef>
#include <cstdint>
#include <oc/type/TextFormat.hpp>

namespace core::midi {

static constexpr float CC_MAX = 127.0f;

// Convert normalized [0.0, 1.0] to MIDI CC [0, 127] with rounding.
inline uint8_t toCC(float normalized) {
    if (normalized <= 0.0f) return 0;
    if (normalized >= 1.0f) return 127;
    return static_cast<uint8_t>(normalized * CC_MAX + 0.5f);
}

// Convert MIDI CC [0, 127] to normalized [0.0, 1.0].
inline float fromCC(uint8_t cc) {
    return static_cast<float>(cc) / CC_MAX;
}

inline void formatNoteName(char* buffer, size_t bufferSize, uint8_t midiNote) {
    if (!buffer || bufferSize == 0) return;

    static const char* NOTE_NAMES[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const uint8_t chroma = static_cast<uint8_t>(midiNote % 12);
    const int octave = static_cast<int>(midiNote) / 12 - 1;
    size_t pos = oc::type::text::appendString(buffer, bufferSize, 0, NOTE_NAMES[chroma]);
    pos = oc::type::text::appendSigned(buffer, bufferSize, pos, octave);
    oc::type::text::terminate(buffer, bufferSize, pos);
}

}  // namespace core::midi

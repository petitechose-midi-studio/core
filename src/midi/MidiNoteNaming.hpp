#pragma once

#include <array>
#include <cstdint>

namespace core::midi {

enum class NoteOctaveConvention : uint8_t {
    C3 = 0,
    C4 = 1,
    C5 = 2,
};

inline constexpr NoteOctaveConvention DEFAULT_NOTE_OCTAVE_CONVENTION =
    NoteOctaveConvention::C4;

inline constexpr std::array<NoteOctaveConvention, 3> NOTE_OCTAVE_CONVENTIONS = {
    NoteOctaveConvention::C3,
    NoteOctaveConvention::C4,
    NoteOctaveConvention::C5,
};

constexpr bool validNoteOctaveConvention(NoteOctaveConvention convention) {
    return convention >= NoteOctaveConvention::C3 &&
           convention <= NoteOctaveConvention::C5;
}

constexpr const char* noteOctaveConventionLabel(
    NoteOctaveConvention convention
) {
    switch (convention) {
        case NoteOctaveConvention::C3: return "C3";
        case NoteOctaveConvention::C5: return "C5";
        case NoteOctaveConvention::C4:
        default: return "C4";
    }
}

constexpr int midiNoteOctave(
    uint8_t midiNote,
    NoteOctaveConvention convention
) {
    const int middleCOctave =
        3 + static_cast<int>(convention);
    return static_cast<int>(midiNote) / 12 + middleCOctave - 5;
}

namespace detail {
inline NoteOctaveConvention active_note_octave_convention =
    DEFAULT_NOTE_OCTAVE_CONVENTION;
}  // namespace detail

inline void setActiveNoteOctaveConvention(
    NoteOctaveConvention convention
) {
    if (validNoteOctaveConvention(convention)) {
        detail::active_note_octave_convention = convention;
    }
}

inline NoteOctaveConvention activeNoteOctaveConvention() {
    return detail::active_note_octave_convention;
}

inline int midiNoteOctave(uint8_t midiNote) {
    return midiNoteOctave(midiNote, activeNoteOctaveConvention());
}

}  // namespace core::midi

#pragma once

#include <oc/state/Signal.hpp>

#include "midi/MidiNoteNaming.hpp"

namespace core::state {

struct MidiNoteDisplayState {
    oc::state::Signal<core::midi::NoteOctaveConvention, 4> octaveConvention{
        core::midi::DEFAULT_NOTE_OCTAVE_CONVENTION
    };

    bool setOctaveConvention(core::midi::NoteOctaveConvention convention) {
        if (!core::midi::validNoteOctaveConvention(convention)) return false;
        // Keep synchronous Signal observers on the same convention as the
        // allocation-free formatting path they may invoke.
        core::midi::setActiveNoteOctaveConvention(convention);
        octaveConvention.set(convention);
        return true;
    }

    // Publish a decoded or otherwise direct Signal value to the formatter.
    void syncFormatter() const {
        core::midi::setActiveNoteOctaveConvention(octaveConvention.get());
    }

    void reset() {
        setOctaveConvention(core::midi::DEFAULT_NOTE_OCTAVE_CONVENTION);
    }
};

}  // namespace core::state

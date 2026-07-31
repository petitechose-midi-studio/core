#pragma once

#include <oc/note/sequencer/StepSequencerChord.hpp>

namespace core::state::sequencer {

inline bool chordSpecEqualsSanitized(oc::note::sequencer::StepSequencerChordSpec lhs,
                                     oc::note::sequencer::StepSequencerChordSpec rhs) {
    return oc::note::sequencer::chordSpecsEqualCanonical(lhs, rhs);
}

}  // namespace core::state::sequencer

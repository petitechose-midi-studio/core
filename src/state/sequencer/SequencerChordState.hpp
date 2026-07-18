#pragma once

#include <oc/note/sequencer/StepSequencerChord.hpp>

namespace core::state::sequencer {

inline bool chordSpecEqualsSanitized(oc::note::sequencer::StepSequencerChordSpec lhs,
                                     oc::note::sequencer::StepSequencerChordSpec rhs) {
    lhs.clamp();
    rhs.clamp();
    return lhs.voiceCount == rhs.voiceCount &&
           lhs.harmonyData == rhs.harmonyData &&
           lhs.voicingData == rhs.voicingData &&
           lhs.inversionData == rhs.inversionData &&
           lhs.strum == rhs.strum &&
           lhs.velocityCurve == rhs.velocityCurve;
}

}  // namespace core::state::sequencer

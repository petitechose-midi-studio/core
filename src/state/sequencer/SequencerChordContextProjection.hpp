#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerChordContextProjectionStats {
    uint16_t patternsVisited = 0;
    uint16_t localChordsVisited = 0;
    uint16_t projected = 0;
    uint16_t changed = 0;
    uint16_t exact = 0;
    uint16_t adapted = 0;
    uint16_t directionLimited = 0;
    uint16_t rangeLimited = 0;
    uint16_t failures = 0;
    uint16_t droppedVoices = 0;

    void merge(const SequencerChordContextProjectionStats& other);

    [[nodiscard]] bool hasChanges() const { return changed != 0U; }
    [[nodiscard]] bool hasAdaptations() const {
        return adapted != 0U || directionLimited != 0U ||
               rangeLimited != 0U ||
               droppedVoices != 0U || failures != 0U;
    }
};

/**
 * Re-encodes every local Chord formula whose effective interval basis crosses
 * the supplied context boundary. Degree-to-degree and semitone-to-semitone
 * transitions retain their raw formula. The graph is mutated in place and its
 * revision is bumped at most once.
 */
SequencerChordContextProjectionStats projectPatternChordContext(
    SequencerPatternState& pattern,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    oc::note::sequencer::StepSequencerScaleSettings targetScale
);

/**
 * Projects a Pattern while its pitch-context policy itself changes. This is
 * the canonical DEG/ST boundary path used by Pitch Context. Callers must
 * project before mutating the Pattern-owned pitch mode so the source basis
 * remains unambiguous.
 */
SequencerChordContextProjectionStats projectPatternChordContext(
    SequencerPatternState& pattern,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    oc::note::sequencer::StepSequencerScaleSettings targetScale,
    SequencerPitchEditMode sourceMode,
    SequencerPitchEditMode targetMode
);

/**
 * Projects the published Pattern and, when a Chord draft is active, its
 * current local formula as one logical Chord slot. The Pattern mutation stays
 * independently undoable; saving or discarding the Chord draft only decides
 * which projected formula remains at its owner node.
 */
SequencerChordContextProjectionStats projectPatternChordContext(
    SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    oc::note::sequencer::StepSequencerScaleSettings targetScale,
    SequencerPitchEditMode sourceMode,
    SequencerPitchEditMode targetMode
);

/**
 * Applies a Project-scale transition to the active editor and every inactive
 * bank Track that inherits Project scale. Pattern overrides are untouched.
 */
SequencerChordContextProjectionStats projectInheritedChordContexts(
    SequencerTrackBankState& bank,
    SequencerState& active,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    oc::note::sequencer::StepSequencerScaleSettings targetScale
);

}  // namespace core::state::sequencer

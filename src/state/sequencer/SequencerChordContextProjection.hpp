#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerChordContextProjectionStats {
    uint32_t patternsVisited = 0;
    uint32_t localChordsVisited = 0;
    uint32_t projected = 0;
    uint32_t changed = 0;
    uint32_t exact = 0;
    uint32_t adapted = 0;
    uint32_t directionLimited = 0;
    uint32_t rangeLimited = 0;
    uint32_t failures = 0;
    uint32_t droppedVoices = 0;

    void merge(const SequencerChordContextProjectionStats& other);

    [[nodiscard]] bool hasChanges() const { return changed != 0U; }
    [[nodiscard]] bool hasAdaptations() const {
        return adapted != 0U || directionLimited != 0U ||
               rangeLimited != 0U ||
               droppedVoices != 0U || failures != 0U;
    }
};

// Read-only projection: the visitor receives only changed formulas. The same
// traversal drives live edits and exact, detached Project-scale history.
using SequencerProjectedChordVisitor = void (*)(
    void*, uint16_t,
    const oc::note::sequencer::StepSequencerChordSpec&,
    const oc::note::sequencer::StepSequencerChordSpec&);
SequencerChordContextProjectionStats visitProjectedPatternChords(
    const std::array<uint8_t, SequencerPatternState::MAX_STEPS>& notes,
    uint8_t length, const oc::note::sequencer::StepSequencerGraph* graph,
    SequencerPitchEditMode mode,
    oc::note::sequencer::StepSequencerScaleSettings source,
    oc::note::sequencer::StepSequencerScaleSettings target,
    SequencerProjectedChordVisitor visitor, void* context);

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

}  // namespace core::state::sequencer

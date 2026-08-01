#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerChordContextProjection.hpp"

namespace core::state::sequencer {

struct SequencerProjectScaleChoice {
    oc::note::sequencer::StepSequencerScaleSettings target{};
    bool valid = false;
    bool changes = false;
};

/**
 * Resolves and clamps one Project-scale selector choice without mutating state.
 */
SequencerProjectScaleChoice resolveProjectScaleChoice(
    oc::note::sequencer::StepSequencerScaleSettings current,
    uint8_t row,
    int choiceIndex
);

struct SequencerProjectScaleMutationResult {
    bool changed = false;
    SequencerChordContextProjectionStats projection{};
};

/**
 * Applies one already-resolved Project-scale transition. This operation is
 * allocation-free and presence-preserving for canonical Pattern payloads, so
 * the identical operation can be run on prepared staging and live state.
 */
SequencerProjectScaleMutationResult applyProjectScaleTransition(
    SequencerTrackBankState& bank,
    SequencerState& active,
    oc::note::sequencer::StepSequencerScaleSettings target
);

}  // namespace core::state::sequencer

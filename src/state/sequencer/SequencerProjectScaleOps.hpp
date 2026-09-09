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

}  // namespace core::state::sequencer

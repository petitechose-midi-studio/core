#pragma once

#include <oc/note/sequencer/StepSequencerGraph.hpp>

namespace core::state::sequencer::graph_canonical_policy {

/**
 * Pure domain canonicality shared by musical asset validation and durable
 * codecs. These predicates know no record sizes, byte order or file format.
 */
bool sequenceIsCanonical(
    const oc::note::sequencer::StepSequencerSequence& sequence
);

bool stepNodeIsCanonical(
    const oc::note::sequencer::StepSequencerStepNode& node
);

}  // namespace core::state::sequencer::graph_canonical_policy

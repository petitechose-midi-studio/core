#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

namespace core::persistence::sequencer_graph_record_codec {

inline constexpr uint16_t SEQUENCE_RECORD_SIZE = 5;
inline constexpr uint16_t STEP_NODE_RECORD_SIZE = 28;
inline constexpr uint16_t CYCLE_SET_RECORD_SIZE = 4;

/**
 * Canonical fixed-record boundary shared by project envelopes and Step Graph
 * Preset files.
 *
 * These functions map domain objects directly to bytes. Persistence record
 * DTOs are deliberately private so callers cannot introduce a second
 * Domain-to-Record mapping. Pure graph canonicality belongs to
 * state/sequencer/SequencerGraphCanonicalPolicy.
 */
bool encodeSequence(
    const oc::note::sequencer::StepSequencerSequence& sequence,
    uint8_t* out,
    uint16_t size
);

bool decodeSequence(
    const uint8_t* data,
    uint16_t size,
    oc::note::sequencer::StepSequencerSequence& out
);

bool encodeStepNode(
    const oc::note::sequencer::StepSequencerStepNode& node,
    uint8_t* out,
    uint16_t size
);

bool decodeStepNode(
    const uint8_t* data,
    uint16_t size,
    oc::note::sequencer::StepSequencerStepNode& out
);

bool encodeCycleSet(
    const oc::note::sequencer::StepSequencerCycleStateSet& cycleSet,
    uint8_t* out,
    uint16_t size
);

bool decodeCycleSet(
    const uint8_t* data,
    uint16_t size,
    oc::note::sequencer::StepSequencerCycleStateSet& out
);

}  // namespace core::persistence::sequencer_graph_record_codec

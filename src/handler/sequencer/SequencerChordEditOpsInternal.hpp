#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerChord.hpp>

namespace core::state::sequencer {
struct SequencerState;
}

namespace core::handler::sequencer::chord_edit_ops::detail {

using Basis = oc::note::sequencer::StepSequencerChordIntervalBasis;
using Spec = oc::note::sequencer::StepSequencerChordSpec;

Basis contextBasis(bool scaleBased);
Spec semanticDefault(bool scaleBased);
void ensureSemantic(Spec& spec, bool scaleBased);
bool commitSpec(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    Spec spec
);

}  // namespace core::handler::sequencer::chord_edit_ops::detail

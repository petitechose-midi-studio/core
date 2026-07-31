#include "state/sequencer/SequencerGraphCanonicalPolicy.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer::graph_canonical_policy {

namespace {

using oc::note::sequencer::StepSequencerChordSpec;

FLASHMEM bool chordSpecIsCanonical(const StepSequencerChordSpec& spec) {
    auto canonical = spec;
    canonical.clamp();
    return oc::note::sequencer::chordSpecsEqual(spec, canonical);
}

}  // namespace

FLASHMEM bool sequenceIsCanonical(
    const oc::note::sequencer::StepSequencerSequence& sequence
) {
    using oc::note::sequencer::StepSequencerSequenceKind;
    return static_cast<uint8_t>(sequence.kind) <=
           static_cast<uint8_t>(StepSequencerSequenceKind::MicroSequence);
}

FLASHMEM bool stepNodeIsCanonical(
    const oc::note::sequencer::StepSequencerStepNode& node
) {
    using oc::note::sequencer::STEP_NODE_ALL_FLAGS;
    using oc::note::sequencer::StepSequencerChordMode;
    using oc::note::sequencer::StepSequencerVariationRanges;

    return (node.flags & static_cast<uint16_t>(~STEP_NODE_ALL_FLAGS)) == 0 &&
           static_cast<uint8_t>(node.chordMode) <=
               static_cast<uint8_t>(StepSequencerChordMode::Local) &&
           chordSpecIsCanonical(node.chordSpec) &&
           node.localVariation.pitchSemitones <=
               StepSequencerVariationRanges::MAX_PITCH_SEMITONES &&
           node.localVariation.velocity <=
               StepSequencerVariationRanges::MAX_VELOCITY &&
           node.localVariation.gatePercent <=
               StepSequencerVariationRanges::MAX_GATE_PERCENT &&
           node.localVariation.nudge <=
               StepSequencerVariationRanges::MAX_NUDGE;
}

}  // namespace core::state::sequencer::graph_canonical_policy

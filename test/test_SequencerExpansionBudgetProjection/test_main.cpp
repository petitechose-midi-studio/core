#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/sequencer/SequencerExpansionBudgetProjection.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace {

namespace seq = core::state::sequencer;
using oc::note::sequencer::StepSequencerChordIntervalBasis;
using oc::note::sequencer::StepSequencerChordHarmony;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;

seq::SequencerExpansionBudgetProjection projectMicroSequence(uint8_t length) {
    seq::SequencerState state;
    assert(state.pattern.setContentLength(1));
    const auto created = seq::createMicroSequence(
        state.pattern,
        seq::rootStepNodeId(0),
        length
    );
    assert(created.ok);
    return seq::projectSequencerExpansionBudget(state, {}, 0);
}

StepSequencerChordSpec eightVoiceChord() {
    auto chord = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        8,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    chord.setCustomIntervals({0, 2, 4, 6, 8, 10, 12, 14});
    return chord;
}

seq::SequencerExpansionBudgetProjection projectPublishedChordMicroSequence() {
    seq::SequencerState state;
    assert(state.pattern.setContentLength(1));
    const auto rootNode = seq::rootStepNodeId(0);
    const auto created = seq::createMicroSequence(state.pattern, rootNode, 3);
    assert(created.ok);
    assert(seq::setNodeChordMode(
        state.pattern,
        rootNode,
        StepSequencerChordMode::Local
    ));
    assert(seq::setNodeChordSpec(state.pattern, rootNode, eightVoiceChord()));
    return seq::projectSequencerExpansionBudget(state, {}, 0);
}

void test_projection_matches_exact_8_and_16_note_budgets() {
    for (const uint8_t count : {static_cast<uint8_t>(8), static_cast<uint8_t>(16)}) {
        const auto projection = projectMicroSequence(count);
        assert(projection.valid);
        assert(!projection.noteBudgetExceeded);
        assert(projection.emittedNoteCount == count);
        assert(projection.requestedNoteCount == count);
    }
    std::cout << "[PASS] test_projection_matches_exact_8_and_16_note_budgets\n";
}

void test_projection_saturates_at_17_without_retaining_more_notes() {
    const auto projection = projectPublishedChordMicroSequence();
    assert(projection.valid);
    assert(projection.noteBudgetExceeded);
    assert(projection.emittedNoteCount == 16);
    assert(projection.requestedNoteCount == 17);
    std::cout << "[PASS] test_projection_saturates_at_17_without_retaining_more_notes\n";
}

void test_projection_includes_unpublished_chord_draft() {
    seq::SequencerState state;
    assert(state.pattern.setContentLength(1));
    const auto sequence = seq::createMicroSequence(
        state.pattern,
        seq::rootStepNodeId(0),
        3
    );
    assert(sequence.ok);

    const auto rootNode = seq::rootStepNodeId(0);
    assert(seq::beginStepContentDraft(
        state,
        seq::SequencerStepContentDraftKind::CHORD,
        0,
        rootNode
    ));
    assert(seq::setAuthoringNodeChordMode(
        state,
        rootNode,
        StepSequencerChordMode::Local
    ));
    assert(seq::setAuthoringNodeChordSpec(state, rootNode, eightVoiceChord()));

    const auto projection =
        seq::projectSequencerExpansionBudget(state, {}, 0);
    assert(projection.valid);
    assert(projection.noteBudgetExceeded);
    assert(projection.emittedNoteCount == 16);
    assert(projection.requestedNoteCount == 17);
    assert(seq::graphView(state.pattern)->stepNode(rootNode)->chordMode !=
           StepSequencerChordMode::Local);
    std::cout << "[PASS] test_projection_includes_unpublished_chord_draft\n";
}

}  // namespace

int main() {
    test_projection_matches_exact_8_and_16_note_budgets();
    test_projection_saturates_at_17_without_retaining_more_notes();
    test_projection_includes_unpublished_chord_draft();
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/state/sequencer/SequencerUiState.hpp"

namespace {
using oc::note::sequencer::StepBitMask128;

void test_inline_feedback_expires_steps_independently() {
    core::state::sequencer::SequencerStepInlineFeedbackState state;

    state.show(1, core::state::sequencer::StepProperty::NOTE, 100);
    state.show(3, core::state::sequencer::StepProperty::VELOCITY, 200);

    assert(state.visible.get());
    assert(state.property.get() == core::state::sequencer::StepProperty::VELOCITY);
    assert(state.touchedMask.get() ==
           StepBitMask128::fromLower64((1ULL << 1) | (1ULL << 3)));

    state.update(799);
    assert(state.visible.get());
    assert(state.touchedMask.get() ==
           StepBitMask128::fromLower64((1ULL << 1) | (1ULL << 3)));

    state.update(800);
    assert(state.visible.get());
    assert(state.touchedMask.get() == StepBitMask128::fromLower64(1ULL << 3));
    assert(state.property.get() == core::state::sequencer::StepProperty::VELOCITY);

    state.update(900);
    assert(!state.visible.get());
    assert(state.touchedMask.get() == StepBitMask128{});

    std::cout << "[PASS] test_inline_feedback_expires_steps_independently\n";
}

void test_inline_feedback_reset_clears_state() {
    core::state::sequencer::SequencerStepInlineFeedbackState state;

    state.show(5, core::state::sequencer::StepProperty::PROBABILITY, 50);
    assert(state.visible.get());

    state.reset();

    assert(!state.visible.get());
    assert(state.touchedMask.get() == StepBitMask128{});
    assert(state.property.get() == core::state::sequencer::StepProperty::NOTE);
    for (uint32_t value : state.hideAtMs) {
        assert(value == 0);
    }

    std::cout << "[PASS] test_inline_feedback_reset_clears_state\n";
}

void test_range_selection_helpers_reflect_copy_paste_flow() {
    core::state::sequencer::SequencerRangeSelectionState state;

    assert(!state.active());
    assert(!state.selectingSourceRange());
    assert(!state.selectingPasteTarget());

    state.kind.set(core::state::sequencer::RangeSelectionKind::COPY);
    state.phase.set(core::state::sequencer::RangeSelectionPhase::SELECT_RANGE);
    assert(state.active());
    assert(state.selectingSourceRange());
    assert(!state.selectingPasteTarget());

    state.clipboard.valid = true;
    state.phase.set(core::state::sequencer::RangeSelectionPhase::PASTE_TARGET);
    assert(state.selectingPasteTarget());

    state.clipboard.count = 4;
    state.clipboard.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 2));
    assert(state.clipboard.isEnabled(0));
    assert(!state.clipboard.isEnabled(1));
    assert(state.clipboard.isEnabled(2));
    assert(!state.clipboard.isEnabled(4));

    state.reset();
    assert(!state.active());
    assert(!state.selectingSourceRange());
    assert(!state.selectingPasteTarget());
    assert(!state.clipboard.valid);
    assert(state.clipboard.count == 0);
    assert(state.clipboard.enabledMask == StepBitMask128{});

    std::cout << "[PASS] test_range_selection_helpers_reflect_copy_paste_flow\n";
}

}  // namespace

int main() {
    test_inline_feedback_expires_steps_independently();
    test_inline_feedback_reset_clears_state();
    test_range_selection_helpers_reflect_copy_paste_flow();

    std::cout << "\nAll SequencerUiState tests passed.\n";
    return 0;
}

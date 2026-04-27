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

}  // namespace

int main() {
    test_inline_feedback_expires_steps_independently();
    test_inline_feedback_reset_clears_state();

    std::cout << "\nAll SequencerUiState tests passed.\n";
    return 0;
}

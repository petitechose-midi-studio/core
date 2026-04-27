#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::StepBitMask128;

void setStep(SequencerState& sequencer,
             uint8_t step,
             uint8_t note,
             uint8_t velocity,
             uint16_t gate,
             int8_t nudge,
             uint8_t probability,
             bool enabled) {
    sequencer.note[step] = note;
    sequencer.velocity[step] = velocity;
    sequencer.gate[step] = gate;
    sequencer.nudge[step] = nudge;
    sequencer.probability[step] = probability;

    auto mask = sequencer.enabledMask.get();
    mask.setBit(step, enabled);
    sequencer.enabledMask.set(mask);
}

void assertDefaultStep(const SequencerState& sequencer, uint8_t step) {
    assert(sequencer.note[step] == SequencerState::DEFAULT_NOTE);
    assert(sequencer.velocity[step] == SequencerState::DEFAULT_VELOCITY);
    assert(sequencer.gate[step] == SequencerState::DEFAULT_GATE_PERCENT);
    assert(sequencer.nudge[step] == 0);
    assert(sequencer.probability[step] == SequencerState::DEFAULT_PROBABILITY);
    assert(!sequencer.isEnabled(step));
}

void assertStep(const SequencerState& sequencer,
                uint8_t step,
                uint8_t note,
                uint8_t velocity,
                uint16_t gate,
                int8_t nudge,
                uint8_t probability,
                bool enabled) {
    assert(sequencer.note[step] == note);
    assert(sequencer.velocity[step] == velocity);
    assert(sequencer.gate[step] == gate);
    assert(sequencer.nudge[step] == nudge);
    assert(sequencer.probability[step] == probability);
    assert(sequencer.isEnabled(step) == enabled);
}

void test_clear_step_range_resets_payload_and_mask() {
    SequencerState sequencer;
    sequencer.length.set(16);
    setStep(sequencer, 2, 62, 90, 70, -3, 55, true);
    setStep(sequencer, 3, 63, 91, 71, 4, 56, true);

    const uint32_t revisionBefore = sequencer.stepDataRevision.get();
    assert(core::state::sequencer::clearStepRange(sequencer, 2, 3));

    assertDefaultStep(sequencer, 2);
    assertDefaultStep(sequencer, 3);
    assert(sequencer.focusedStep.get() == 2);
    assert(sequencer.page.get() == 0);
    assert(sequencer.stepDataRevision.get() == revisionBefore + 1);

    std::cout << "[PASS] test_clear_step_range_resets_payload_and_mask\n";
}

void test_insert_page_shifts_payloads_and_clears_inserted_page() {
    SequencerState sequencer;
    sequencer.length.set(16);
    setStep(sequencer, 8, 70, 110, 90, 5, 60, true);

    assert(core::state::sequencer::insertPage(sequencer, 1));

    assert(sequencer.length.get() == 24);
    assertDefaultStep(sequencer, 8);
    assertStep(sequencer, 16, 70, 110, 90, 5, 60, true);
    assert(sequencer.focusedStep.get() == 8);
    assert(sequencer.page.get() == 1);

    std::cout << "[PASS] test_insert_page_shifts_payloads_and_clears_inserted_page\n";
}

void test_remove_page_shifts_following_payloads() {
    SequencerState sequencer;
    sequencer.length.set(24);
    setStep(sequencer, 16, 72, 111, 91, -4, 61, true);

    assert(core::state::sequencer::removePage(sequencer, 1));

    assert(sequencer.length.get() == 16);
    assertStep(sequencer, 8, 72, 111, 91, -4, 61, true);
    assertDefaultStep(sequencer, 16);
    assert(sequencer.focusedStep.get() == 8);
    assert(sequencer.page.get() == 1);

    std::cout << "[PASS] test_remove_page_shifts_following_payloads\n";
}

void test_rotate_pattern_moves_payload_and_mask() {
    SequencerState sequencer;
    sequencer.length.set(4);
    sequencer.enabledMask.set(StepBitMask128{});
    setStep(sequencer, 0, 60, 90, 50, 0, 100, true);
    setStep(sequencer, 1, 61, 91, 51, 1, 80, false);

    assert(core::state::sequencer::rotatePattern(sequencer, 1));

    assertStep(sequencer, 1, 60, 90, 50, 0, 100, true);
    assertStep(sequencer, 2, 61, 91, 51, 1, 80, false);
    assert(!sequencer.isEnabled(0));
    assert(!sequencer.isEnabled(3));

    std::cout << "[PASS] test_rotate_pattern_moves_payload_and_mask\n";
}

}  // namespace

int main() {
    test_clear_step_range_resets_payload_and_mask();
    test_insert_page_shifts_payloads_and_clears_inserted_page();
    test_remove_page_shifts_following_payloads();
    test_rotate_pattern_moves_payload_and_mask();

    std::cout << "All SequencerSnapshotOps tests passed\n";
    return 0;
}

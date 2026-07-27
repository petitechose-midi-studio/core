#include <cassert>
#include <iostream>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerStepValueRowWorkflow.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "support/CoreStorages.hpp"

namespace {

namespace input_utils = core::handler::sequencer::input_utils;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace step_value_row_workflow =
    core::handler::sequencer::step_value_row_workflow;

core::state::CoreState makeState(test_support::CoreStorages& storage) {
    return core::state::CoreState(
        storage.settings
    );
}

void test_focused_property_row_edits_root_step_value() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.setContentLength(8);
    sequencer.pattern.note[1] = 60;
    sequencer.stepEdit.focusedRow.set(step_edit_rows::PROPERTY_OFFSET);

    step_value_row_workflow::setFocusedRowValue(sequencer, 1, {}, 1.0f);

    assert(sequencer.pattern.note[1] == 127);

    std::cout << "[PASS] test_focused_property_row_edits_root_step_value\n";
}

void test_local_variation_edit_targets_focused_property_without_base_edit() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.setContentLength(8);
    sequencer.pattern.note[2] = 64;
    sequencer.stepEdit.focusedRow.set(step_edit_rows::PROPERTY_OFFSET);
    sequencer.stepEdit.localVariationEditActive.set(true);

    assert(step_value_row_workflow::focusedRowSupportsLocalVariation(sequencer));
    step_value_row_workflow::setFocusedRowValue(sequencer, 2, {}, 1.0f);

    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(
        core::state::sequencer::nodeLocalVariationRange(
            *node,
            core::state::sequencer::StepProperty::NOTE
        ) ==
        input_utils::variationRangeMaxForProperty(core::state::sequencer::StepProperty::NOTE)
    );
    assert(sequencer.pattern.note[2] == 64);

    std::cout << "[PASS] test_local_variation_edit_targets_focused_property_without_base_edit\n";
}

void test_chance_row_does_not_support_local_variation() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.setContentLength(8);
    sequencer.stepEdit.focusedRow.set(
        static_cast<uint8_t>(step_edit_rows::PROPERTY_OFFSET + 4U)
    );

    assert(!step_value_row_workflow::focusedRowSupportsLocalVariation(sequencer));

    std::cout << "[PASS] test_chance_row_does_not_support_local_variation\n";
}

void test_chord_quick_row_sets_and_resets_root_chord() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.setContentLength(8);
    sequencer.stepEdit.focusedRow.set(step_edit_rows::CHORD);

    step_value_row_workflow::setFocusedRowValue(sequencer, 3, {}, 1.0f);

    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    assert(node != nullptr);
    assert(node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(node->chordSpec.voiceCount == oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES);

    assert(step_value_row_workflow::resetFocusedRowToDefault(sequencer, 3));
    graph = core::state::sequencer::graphView(sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(3)) : nullptr;
    assert(node == nullptr || !node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    std::cout << "[PASS] test_chord_quick_row_sets_and_resets_root_chord\n";
}

}  // namespace

int main() {
    test_focused_property_row_edits_root_step_value();
    test_local_variation_edit_targets_focused_property_without_base_edit();
    test_chance_row_does_not_support_local_variation();
    test_chord_quick_row_sets_and_resets_root_chord();

    std::cout << "\nAll SequencerStepValueRowWorkflow tests passed.\n";
    return 0;
}

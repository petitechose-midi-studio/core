#include <cassert>
#include <iostream>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "support/CoreStorages.hpp"

namespace {

core::state::CoreState makeState(test_support::CoreStorages& storage) {
    return core::state::CoreState(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );
}

void test_reset_root_property_to_default_also_resets_local_variation() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[2] = 74;

    const auto rootNode = core::state::sequencer::rootStepNodeId(2);
    assert(core::state::sequencer::setNodeLocalVariationRange(
        sequencer.pattern,
        rootNode,
        core::state::sequencer::StepProperty::NOTE,
        3
    ));

    assert(core::state::sequencer::resetActiveContentStepPropertyToDefault(
        sequencer,
        2,
        core::state::sequencer::StepProperty::NOTE
    ));
    assert(sequencer.pattern.note[2] == core::state::sequencer::SequencerState::DEFAULT_NOTE);

    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(rootNode);
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(
        *node,
        core::state::sequencer::StepProperty::NOTE
    ) == 0);

    std::cout << "[PASS] test_reset_root_property_to_default_also_resets_local_variation\n";
}

void test_reset_child_property_to_default_preserves_existing_revision_behavior() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.length.set(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        sequencer,
        rootNode,
        micro.id
    ));

    const auto childNode = core::state::sequencer::activeContentStepNodeId(sequencer, 0);
    assert(core::state::sequencer::setNodeNoteOffset(sequencer.pattern, childNode, 5));
    assert(core::state::sequencer::setNodeLocalVariationRange(
        sequencer.pattern,
        childNode,
        core::state::sequencer::StepProperty::NOTE,
        4
    ));

    const uint32_t beforeOffsetReset = sequencer.contentView.revision.get();
    assert(core::state::sequencer::resetActiveContentStepPropertyToDefault(
        sequencer,
        0,
        core::state::sequencer::StepProperty::NOTE
    ));
    assert(sequencer.contentView.revision.get() == beforeOffsetReset + 1U);

    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(childNode);
    assert(node != nullptr);
    assert(!node->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(node->noteOffset == 0);
    assert(core::state::sequencer::nodeLocalVariationRange(
        *node,
        core::state::sequencer::StepProperty::NOTE
    ) == 0);

    assert(core::state::sequencer::setNodeLocalVariationRange(
        sequencer.pattern,
        childNode,
        core::state::sequencer::StepProperty::NOTE,
        2
    ));
    const uint32_t beforeVariationOnlyReset = sequencer.contentView.revision.get();
    assert(core::state::sequencer::resetActiveContentStepPropertyToDefault(
        sequencer,
        0,
        core::state::sequencer::StepProperty::NOTE
    ));
    assert(sequencer.contentView.revision.get() == beforeVariationOnlyReset);

    std::cout << "[PASS] test_reset_child_property_to_default_preserves_existing_revision_behavior\n";
}

}  // namespace

int main() {
    test_reset_root_property_to_default_also_resets_local_variation();
    test_reset_child_property_to_default_preserves_existing_revision_behavior();

    std::cout << "\nAll SequencerContentStepOps tests passed.\n";
    return 0;
}

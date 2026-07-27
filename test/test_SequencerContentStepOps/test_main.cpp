#include <cassert>
#include <iostream>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "support/CoreStorages.hpp"

namespace {

core::state::CoreState makeState(test_support::CoreStorages& storage) {
    return core::state::CoreState(
        storage.settings
    );
}

void test_reset_root_property_to_default_also_resets_local_variation() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.setContentLength(8);
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
    sequencer.pattern.setContentLength(8);

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

void test_open_or_create_child_context_opens_existing_without_graph_mutation() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.setContentLength(8);

    const auto createdMicro = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer,
        0,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(createdMicro.opened);
    assert(createdMicro.created);
    assert(createdMicro.draft);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));

    assert(core::state::sequencer::publishStepContentDraft(sequencer));
    assert(core::state::sequencer::leaveContentView(sequencer));
    const uint32_t graphRevisionBeforeReopen = sequencer.pattern.graphRevision.get();
    const auto reopenedMicro = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer,
        0,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(reopenedMicro.opened);
    assert(!reopenedMicro.created);
    assert(reopenedMicro.contentId == createdMicro.contentId);
    assert(sequencer.pattern.graphRevision.get() == graphRevisionBeforeReopen);

    assert(core::state::sequencer::leaveContentView(sequencer));
    const auto createdCycle = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer,
        1,
        core::state::sequencer::StepContentChildKind::CYCLE_STATES,
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    assert(createdCycle.opened);
    assert(createdCycle.created);
    assert(createdCycle.draft);
    assert(core::state::sequencer::isCycleStatesContentView(sequencer));
    assert(core::state::sequencer::publishStepContentDraft(sequencer));

    std::cout << "[PASS] test_open_or_create_child_context_opens_existing_without_graph_mutation\n";
}

void test_copy_paste_and_clear_active_child_content() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    auto& clipboard = state.structureClipboard;
    sequencer.pattern.setContentLength(8);

    const auto createdMicro = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer,
        0,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(createdMicro.opened);
    assert(createdMicro.created);
    const auto sourceChildNode = core::state::sequencer::activeContentStepNodeId(sequencer, 0);
    assert(core::state::sequencer::setNodeNoteOffset(
        core::state::sequencer::authoringPattern(sequencer),
        sourceChildNode,
        5
    ));
    core::state::sequencer::notifyStepContentDraftMutation(sequencer);
    assert(core::state::sequencer::publishStepContentDraft(sequencer));
    assert(core::state::sequencer::leaveContentView(sequencer));

    assert(core::state::sequencer::activeContentStepHasChildContent(
        sequencer,
        0,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
    ));
    assert(core::state::sequencer::copyActiveContentChildToClipboard(
        sequencer,
        0,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        clipboard
    ));
    assert(core::state::sequencer::clipboardCanPasteActiveContentChild(
        clipboard,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
    ));
    assert(!core::state::sequencer::clipboardCanPasteActiveContentChild(
        clipboard,
        core::state::sequencer::StepContentChildKind::CYCLE_STATES
    ));

    const uint32_t revisionBeforePaste = sequencer.contentView.revision.get();
    assert(core::state::sequencer::pasteActiveContentChildFromClipboard(
        sequencer,
        1,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        clipboard
    ));
    assert(sequencer.contentView.revision.get() == revisionBeforePaste + 1U);
    assert(core::state::sequencer::activeContentStepHasChildContent(
        sequencer,
        1,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
    ));

    const auto reopenedMicro = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer,
        1,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(reopenedMicro.opened);
    assert(!reopenedMicro.created);
    const auto pastedChildNode = core::state::sequencer::activeContentStepNodeId(sequencer, 0);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(pastedChildNode);
    assert(node != nullptr);
    assert(node->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(node->noteOffset == 5);
    assert(core::state::sequencer::leaveContentView(sequencer));

    const uint32_t revisionBeforeClear = sequencer.contentView.revision.get();
    assert(core::state::sequencer::clearActiveContentChild(
        sequencer,
        1,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
    ));
    assert(sequencer.contentView.revision.get() > revisionBeforeClear);
    assert(!core::state::sequencer::activeContentStepHasChildContent(
        sequencer,
        1,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
    ));

    std::cout << "[PASS] test_copy_paste_and_clear_active_child_content\n";
}

}  // namespace

int main() {
    test_reset_root_property_to_default_also_resets_local_variation();
    test_reset_child_property_to_default_preserves_existing_revision_behavior();
    test_open_or_create_child_context_opens_existing_without_graph_mutation();
    test_copy_paste_and_clear_active_child_content();

    std::cout << "\nAll SequencerContentStepOps tests passed.\n";
    return 0;
}

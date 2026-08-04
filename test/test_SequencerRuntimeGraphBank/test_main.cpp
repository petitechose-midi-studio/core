#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/sequencer/SequencerRuntimeGraphBank.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"

namespace {

uint16_t createNestedNode(core::state::sequencer::SequencerPatternState& pattern,
                          uint8_t rootStep,
                          int8_t noteOffset) {
    const auto created = core::state::sequencer::createMicroSequence(
        pattern,
        core::state::sequencer::rootStepNodeId(rootStep),
        2
    );
    assert(created.ok);

    const auto* graph = core::state::sequencer::graphView(pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(created.id);
    assert(sequence != nullptr);
    const uint16_t childNode = static_cast<uint16_t>(sequence->firstStepNode + 1U);
    assert(core::state::sequencer::setNodeNoteOffset(pattern, childNode, noteOffset));
    return childNode;
}

void test_prepare_keeps_the_active_graph_until_publication() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    const uint16_t childNode = createNestedNode(sequencer.pattern, 0, 3);
    const auto* editorGraph = core::state::sequencer::graphView(sequencer.pattern);
    assert(editorGraph != nullptr);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    assert(runtimeGraphs.graphForTrack(0) == nullptr);
    runtimeGraphs.publishPrepared();
    const auto* firstRuntimeGraph = runtimeGraphs.graphForTrack(0);
    assert(firstRuntimeGraph != nullptr);
    assert(firstRuntimeGraph != editorGraph);
    assert(firstRuntimeGraph->stepNodes[childNode].noteOffset == 3);

    assert(core::state::sequencer::setNodeNoteOffset(sequencer.pattern, childNode, 7));
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 7);

    core::state::sequencer::clearGraph(sequencer.pattern);
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0) == nullptr);

    std::cout << "[PASS] test_prepare_keeps_the_active_graph_until_publication\n";
}

void test_publication_commits_simultaneous_track_changes_together() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    const uint16_t activeNode = createNestedNode(sequencer.pattern, 0, 2);
    const uint16_t inactiveNode = createNestedNode(trackBank.track(1), 0, 5);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[activeNode].noteOffset == 2);
    assert(runtimeGraphs.graphForTrack(1)->stepNodes[inactiveNode].noteOffset == 5);

    assert(core::state::sequencer::setNodeNoteOffset(sequencer.pattern, activeNode, 4));
    assert(core::state::sequencer::setNodeNoteOffset(trackBank.track(1), inactiveNode, 9));
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();

    assert(runtimeGraphs.graphForTrack(0)->stepNodes[activeNode].noteOffset == 4);
    assert(runtimeGraphs.graphForTrack(1)->stepNodes[inactiveNode].noteOffset == 9);

    std::cout << "[PASS] test_publication_commits_simultaneous_track_changes_together\n";
}

void test_micro_sequence_draft_is_runtime_only_until_apply_or_cancel() {
    namespace seq = core::state::sequencer;
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0) == nullptr);

    assert(seq::beginStepContentDraft(
        sequencer,
        seq::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0
    ));
    const auto created = seq::createMicroSequence(
        seq::authoringPattern(sequencer),
        seq::rootStepNodeId(0),
        3
    );
    assert(created.ok);
    seq::notifyStepContentDraftMutation(sequencer);
    assert(seq::graphView(sequencer.pattern) == nullptr);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    const auto* draftRuntime = runtimeGraphs.graphForTrack(0);
    assert(draftRuntime != nullptr);
    assert(draftRuntime->sequence(created.id) != nullptr);
    assert(draftRuntime != seq::graphView(seq::authoringPattern(sequencer)));

    seq::abandonStepContentDraft(sequencer);
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0) == nullptr);

    std::cout
        << "[PASS] test_micro_sequence_draft_is_runtime_only_until_apply_or_cancel\n";
}

void test_chord_draft_projects_without_mutating_the_published_graph() {
    namespace seq = core::state::sequencer;
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;
    constexpr uint16_t ownerNode = 0;

    assert(seq::beginStepContentDraft(
        sequencer,
        seq::SequencerStepContentDraftKind::CHORD,
        0,
        ownerNode
    ));
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 4;
    assert(seq::setAuthoringNodeChordSpec(sequencer, ownerNode, chord));
    seq::notifyStepContentDraftMutation(sequencer);
    assert(seq::graphView(sequencer.pattern) == nullptr);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    const auto* runtime = runtimeGraphs.graphForTrack(0);
    assert(runtime != nullptr);
    const auto* runtimeNode = runtime->stepNode(ownerNode);
    assert(runtimeNode != nullptr);
    assert(runtimeNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(runtimeNode->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
    assert(runtimeNode->chordSpec.voiceCount == 4);

    assert(seq::publishStepContentDraft(sequencer));
    const auto* published = seq::graphView(sequencer.pattern);
    assert(published != nullptr);
    assert(published->stepNode(ownerNode)->chordSpec.voiceCount == 4);
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNode(ownerNode)->chordSpec.voiceCount == 4);

    std::cout
        << "[PASS] test_chord_draft_projects_without_mutating_the_published_graph\n";
}

void test_quick_controls_draft_publishes_only_immutable_runtime_copies() {
    namespace seq = core::state::sequencer;
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    const uint16_t childNode = createNestedNode(sequencer.pattern, 0, 3);
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);

    const auto openingPath = seq::capturePreparedSequencerGraphContentPath(sequencer);
    assert(sequencer.quickControlsDraft.begin(
        sequencer.pattern,
        openingPath,
        sequencer.page.get(),
        sequencer.focusedStep.get()));
    auto& draft = seq::authoringPattern(sequencer);
    assert(&draft != &sequencer.pattern);
    assert(seq::setNodeNoteOffset(draft, childNode, 9));
    sequencer.patternQuickControls.bumpPreview();
    assert(seq::graphView(sequencer.pattern)->stepNodes[childNode].noteOffset == 3);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);
    runtimeGraphs.publishPrepared();
    const auto* previewRuntime = runtimeGraphs.graphForTrack(0);
    assert(previewRuntime != nullptr);
    assert(previewRuntime != seq::graphView(draft));
    assert(previewRuntime->stepNodes[childNode].noteOffset == 9);

    sequencer.quickControlsDraft.reset();
    sequencer.patternQuickControls.bumpPreview();
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 9);
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);

    std::cout
        << "[PASS] Quick Controls Graph preview uses immutable runtime generations\n";
}

void test_nested_quick_controls_preview_overrides_step_draft_projection() {
    namespace seq = core::state::sequencer;
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    assert(seq::beginStepContentDraft(
        sequencer,
        seq::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0U));
    auto& parent = *sequencer.stepContentDraft.pattern();
    const uint16_t childNode = createNestedNode(parent, 0U, 3);
    seq::notifyStepContentDraftMutation(sequencer);
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);

    const auto openingPath = seq::capturePreparedSequencerGraphContentPath(sequencer);
    assert(sequencer.quickControlsDraft.begin(
        parent,
        openingPath,
        sequencer.page.get(),
        sequencer.focusedStep.get()));
    auto& nested = seq::authoringPattern(sequencer);
    assert(&nested != &parent);
    assert(seq::setNodeNoteOffset(nested, childNode, 9));
    sequencer.patternQuickControls.bumpPreview();

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 9);
    assert(parent.graph->stepNodes[childNode].noteOffset == 3);

    sequencer.quickControlsDraft.reset();
    sequencer.patternQuickControls.bumpPreview();
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);

    std::cout
        << "[PASS] nested Quick Controls preview overrides Step draft projection\n";
}

}  // namespace

int main() {
    test_prepare_keeps_the_active_graph_until_publication();
    test_publication_commits_simultaneous_track_changes_together();
    test_micro_sequence_draft_is_runtime_only_until_apply_or_cancel();
    test_chord_draft_projects_without_mutating_the_published_graph();
    test_quick_controls_draft_publishes_only_immutable_runtime_copies();
    test_nested_quick_controls_preview_overrides_step_draft_projection();
    std::cout << "All SequencerRuntimeGraphBank tests passed\n";
    return 0;
}

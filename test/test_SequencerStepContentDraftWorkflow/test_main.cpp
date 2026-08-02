#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>

#include <iostream>
#include <utility>

#include "handler/sequencer/SequencerStepContentDraftWorkflow.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/sequencer/SequencerStepContentDraftTransitionLabels.hpp"

namespace {

namespace draft_workflow = core::handler::sequencer::step_content_draft_workflow;
namespace seq = core::state::sequencer;

bool graphHasChild(const oc::note::sequencer::StepSequencerGraph& graph, uint8_t step,
                   seq::StepContentChildKind kind) {
    const auto node = seq::rootStepNodeId(step);
    const auto* stepNode = graph.stepNode(node);
    if (stepNode == nullptr) return false;
    return kind == seq::StepContentChildKind::MICRO_SEQUENCE
               ? stepNode->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) &&
                     graph.sequence(stepNode->childSequenceId) != nullptr
               : stepNode->has(oc::note::sequencer::STEP_NODE_CYCLE_SET) &&
                     graph.cycleSet(stepNode->cycleSetId) != nullptr;
}

bool rootHasChild(const seq::SequencerPatternState& pattern, uint8_t step,
                  seq::StepContentChildKind kind) {
    const auto* graph = seq::graphView(pattern);
    if (graph == nullptr || graph->stepNode(seq::rootStepNodeId(step)) == nullptr) { return false; }
    return graphHasChild(*graph, step, kind);
}

struct HistoryRecorder {
    bool admit = true;
    uint8_t preparedCount = 0;
    seq::SequencerHistoryPatternChangePtr prepared;

    static bool canRecord(void* context, const seq::SequencerHistoryPatternChange& change) {
        auto& self = *static_cast<HistoryRecorder*>(context);
        return self.admit && !seq::sameMusicalHistorySnapshot(change.before, change.after);
    }

    static void recordPrepared(void* context, seq::SequencerHistoryPatternChangePtr change) {
        auto& self = *static_cast<HistoryRecorder*>(context);
        ++self.preparedCount;
        self.prepared = std::move(change);
    }

    core::handler::SequencerHistoryDomainServices services() {
        static constexpr core::handler::SequencerHistoryDomainServices::Operations operations{
            .canRecordPattern = &HistoryRecorder::canRecord,
            .recordPreparedPattern = &HistoryRecorder::recordPrepared,
        };
        return core::handler::SequencerHistoryDomainServices::fromStaticOperations<operations>(
            this);
    }
};

void test_new_micro_is_unpublished_until_one_prepared_apply() {
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState tracks;
    HistoryRecorder recorder;
    auto history = recorder.services();

    const auto result =
        seq::openOrCreateActiveContentChild(sequencer, 3, seq::StepContentChildKind::MICRO_SEQUENCE,
                                            seq::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(result.opened && result.created && result.draft);
    assert(sequencer.stepContentDraft.active.get());
    assert(!sequencer.stepContentDraft.modified());
    assert(!rootHasChild(sequencer.pattern, 3, seq::StepContentChildKind::MICRO_SEQUENCE));
    assert(rootHasChild(seq::authoringPattern(sequencer), 3,
                        seq::StepContentChildKind::MICRO_SEQUENCE));

    assert(seq::setActiveContentStepFromNormalized(sequencer, 0, seq::StepProperty::NOTE,
                                                   62.0f / 127.0f, sequencer.pattern.pitchEditMode,
                                                   {}));
    assert(sequencer.stepContentDraft.modified());
    assert(!rootHasChild(sequencer.pattern, 3, seq::StepContentChildKind::MICRO_SEQUENCE));

    assert(draft_workflow::apply(sequencer, tracks, history));
    assert(!sequencer.stepContentDraft.active.get());
    assert(recorder.preparedCount == 1);
    assert(recorder.prepared);
    assert(recorder.prepared->storage == seq::SequencerHistoryPatternStorage::FullGraph);
    assert(!recorder.prepared->before.graph ||
           !graphHasChild(*recorder.prepared->before.graph, 3,
                          seq::StepContentChildKind::MICRO_SEQUENCE));
    assert(recorder.prepared->after.graph);
    assert(graphHasChild(*recorder.prepared->after.graph, 3,
                         seq::StepContentChildKind::MICRO_SEQUENCE));
    assert(rootHasChild(sequencer.pattern, 3, seq::StepContentChildKind::MICRO_SEQUENCE));
}

void test_pristine_back_abandons_without_history() {
    seq::SequencerState sequencer;
    const auto result = seq::openOrCreateActiveContentChild(
        sequencer, 1, seq::StepContentChildKind::CYCLE_STATES, seq::DEFAULT_CYCLE_STATE_COUNT);
    assert(result.opened && result.draft);
    assert(!sequencer.stepContentDraft.modified());

    assert(draft_workflow::requestBack(sequencer) == draft_workflow::BackResult::DISCARDED);
    assert(!sequencer.stepContentDraft.active.get());
    assert(!rootHasChild(sequencer.pattern, 1, seq::StepContentChildKind::CYCLE_STATES));
}

void test_modified_back_defaults_to_save_and_supports_continue_discard() {
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState tracks;
    HistoryRecorder recorder;
    auto history = recorder.services();

    const auto result =
        seq::openOrCreateActiveContentChild(sequencer, 2, seq::StepContentChildKind::MICRO_SEQUENCE,
                                            seq::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(result.opened && result.draft);
    assert(seq::setActiveContentStepFromNormalized(sequencer, 0, seq::StepProperty::VELOCITY, 1.0f,
                                                   sequencer.pattern.pitchEditMode, {}));

    assert(draft_workflow::requestBack(sequencer) == draft_workflow::BackResult::CONTINUE_EDITING);
    assert(sequencer.stepContentDraft.exitPromptVisible.get());
    assert(sequencer.stepContentDraft.exitChoice.get() ==
           seq::SequencerStepContentDraftExitChoice::SAVE);

    sequencer.stepContentDraft.exitChoice.set(seq::SequencerStepContentDraftExitChoice::CONTINUE);
    assert(draft_workflow::applyExitChoice(sequencer, tracks, history) ==
           draft_workflow::BackResult::CONTINUE_EDITING);
    assert(sequencer.stepContentDraft.active.get());
    assert(!sequencer.stepContentDraft.exitPromptVisible.get());

    assert(draft_workflow::requestBack(sequencer) == draft_workflow::BackResult::CONTINUE_EDITING);
    sequencer.stepContentDraft.exitChoice.set(seq::SequencerStepContentDraftExitChoice::DISCARD);
    assert(draft_workflow::applyExitChoice(sequencer, tracks, history) ==
           draft_workflow::BackResult::DISCARDED);
    assert(!sequencer.stepContentDraft.active.get());
    assert(recorder.preparedCount == 0);
    assert(!rootHasChild(sequencer.pattern, 2, seq::StepContentChildKind::MICRO_SEQUENCE));
}

void test_failed_preflight_preserves_draft_and_published_pattern() {
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState tracks;
    HistoryRecorder recorder;
    recorder.admit = false;
    auto history = recorder.services();

    const auto result = seq::openOrCreateActiveContentChild(
        sequencer, 0, seq::StepContentChildKind::CYCLE_STATES, seq::DEFAULT_CYCLE_STATE_COUNT);
    assert(result.opened && result.draft);
    assert(seq::setActiveContentStepFromNormalized(sequencer, 0, seq::StepProperty::NOTE, 1.0f,
                                                   sequencer.pattern.pitchEditMode, {}));

    assert(!draft_workflow::apply(sequencer, tracks, history));
    assert(sequencer.stepContentDraft.active.get());
    assert(sequencer.stepContentDraft.modified());
    assert(sequencer.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE);
    assert(recorder.preparedCount == 0);
    assert(!rootHasChild(sequencer.pattern, 0, seq::StepContentChildKind::CYCLE_STATES));
}

void test_track_switch_is_blocked_without_losing_the_active_draft() {
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState tracks;
    assert(seq::initializeTrackBankFromActive(tracks, sequencer));

    const auto result =
        seq::openOrCreateActiveContentChild(sequencer, 1, seq::StepContentChildKind::MICRO_SEQUENCE,
                                            seq::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(result.opened && result.draft);
    assert(seq::setActiveContentStepFromNormalized(sequencer, 0, seq::StepProperty::NOTE, 1.0f,
                                                   sequencer.pattern.pitchEditMode, {}));

    assert(!seq::switchActiveTrack(tracks, sequencer, 1));
    assert(tracks.activeTrackIndex() == 0);
    assert(sequencer.stepContentDraft.active.get());
    assert(sequencer.stepContentDraft.modified());
    assert(sequencer.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(sequencer.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::TRACK);
    assert(!rootHasChild(sequencer.pattern, 1, seq::StepContentChildKind::MICRO_SEQUENCE));
}

void test_unpublishable_flat_draft_mutation_is_rejected_explicitly() {
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState tracks;
    HistoryRecorder recorder;
    auto history = recorder.services();
    const uint8_t publishedNote = sequencer.pattern.note[0];

    const auto result =
        seq::openOrCreateActiveContentChild(sequencer, 0, seq::StepContentChildKind::MICRO_SEQUENCE,
                                            seq::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(result.opened && result.draft);
    seq::authoringPattern(sequencer).note[0] = 99;

    assert(!draft_workflow::apply(sequencer, tracks, history));
    assert(sequencer.stepContentDraft.active.get());
    assert(sequencer.pattern.note[0] == publishedNote);
    assert(sequencer.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::UNPUBLISHABLE_MUTATION);
    assert(recorder.preparedCount == 0);
}

void test_chord_draft_sanitizes_invalid_mode_without_allocating_graph_scratch() {
    seq::SequencerState sequencer;
    const auto nodeId = seq::rootStepNodeId(0);

    assert(seq::beginStepContentDraft(sequencer, seq::SequencerStepContentDraftKind::CHORD, 0,
                                      nodeId));
    assert(!sequencer.stepContentDraft.scratch);
    assert(seq::setAuthoringNodeChordMode(
        sequencer, nodeId, static_cast<oc::note::sequencer::StepSequencerChordMode>(0xFFU)));

    bool modePresent = false;
    bool localPresent = false;
    auto mode = oc::note::sequencer::StepSequencerChordMode::Local;
    oc::note::sequencer::StepSequencerChordSpec spec{};
    assert(seq::resolveStepContentDraftChord(sequencer, nodeId, modePresent, localPresent, mode,
                                             spec));
    assert(modePresent);
    assert(mode == oc::note::sequencer::StepSequencerChordMode::Single);
    seq::abandonStepContentDraft(sequencer);
}

void test_transition_rejection_is_active_only_idempotent_and_exactly_labelled() {
    using Transition = seq::SequencerStepContentDraftBlockedTransition;

    seq::SequencerState sequencer;
    const uint32_t inactiveRevision = sequencer.stepContentDraft.revision.get();
    const uint32_t inactiveContentRevision = sequencer.contentView.revision.get();
    assert(!sequencer.stepContentDraft.rejectTransitionIfActive(Transition::HISTORY));
    assert(sequencer.stepContentDraft.revision.get() == inactiveRevision);
    assert(sequencer.contentView.revision.get() == inactiveContentRevision);
    assert(sequencer.stepContentDraft.failure == seq::SequencerStepContentDraftFailure::NONE);

    assert(seq::beginStepContentDraft(
        sequencer,
        seq::SequencerStepContentDraftKind::CHORD,
        0U,
        seq::rootStepNodeId(0U)
    ));
    const uint32_t activeRevision = sequencer.stepContentDraft.revision.get();
    const uint32_t activeContentRevision = sequencer.contentView.revision.get();

    assert(sequencer.stepContentDraft.rejectTransitionIfActive(Transition::STRUCTURE_EDIT));
    assert(sequencer.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(sequencer.stepContentDraft.blockedTransition == Transition::STRUCTURE_EDIT);
    assert(sequencer.stepContentDraft.revision.get() == activeRevision + 1U);
    assert(sequencer.contentView.revision.get() == activeContentRevision + 1U);

    assert(sequencer.stepContentDraft.rejectTransitionIfActive(Transition::STRUCTURE_EDIT));
    assert(sequencer.stepContentDraft.revision.get() == activeRevision + 1U);
    assert(sequencer.contentView.revision.get() == activeContentRevision + 1U);

    assert(sequencer.stepContentDraft.rejectTransitionIfActive(Transition::HISTORY));
    assert(sequencer.stepContentDraft.blockedTransition == Transition::HISTORY);
    assert(sequencer.stepContentDraft.revision.get() == activeRevision + 2U);
    assert(sequencer.contentView.revision.get() == activeContentRevision + 2U);

    assert(std::strcmp(
               core::ui::sequencer::standaloneStepContentDraftTransitionLabel(
                   Transition::STRUCTURE_EDIT),
               "APPLY BEFORE STRUCTURE EDIT") == 0);
    assert(std::strcmp(
               core::ui::sequencer::standaloneStepContentDraftTransitionLabel(
                   Transition::HISTORY),
               "APPLY BEFORE UNDO/REDO") == 0);
    assert(std::strcmp(
               core::ui::sequencer::propertyOverlayStepContentDraftTransitionLabel(
                   Transition::STRUCTURE_EDIT),
               "Apply before structure edit") == 0);
    assert(std::strcmp(
               core::ui::sequencer::propertyOverlayStepContentDraftTransitionLabel(
                   Transition::HISTORY),
               "Apply before undo/redo") == 0);
}

}  // namespace

int main() {
    test_new_micro_is_unpublished_until_one_prepared_apply();
    test_pristine_back_abandons_without_history();
    test_modified_back_defaults_to_save_and_supports_continue_discard();
    test_failed_preflight_preserves_draft_and_published_pattern();
    test_track_switch_is_blocked_without_losing_the_active_draft();
    test_unpublishable_flat_draft_mutation_is_rejected_explicitly();
    test_chord_draft_sanitizes_invalid_mode_without_allocating_graph_scratch();
    test_transition_rejection_is_active_only_idempotent_and_exactly_labelled();
    std::cout << "All SequencerStepContentDraftWorkflow tests passed.\n";
    return 0;
}
